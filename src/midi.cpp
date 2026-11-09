#include "midi.h"

#include <RtMidi.h>

#include <chrono>

MidiInput::MidiInput() {
  try {
    in_ = std::make_unique<RtMidiIn>(RtMidi::UNSPECIFIED, "DATRACKARXD");
    in_->ignoreTypes(true, true, true);  // no sysex, clock or active sensing
    in_->setCallback(&MidiInput::callback, this);
  } catch (const RtMidiError& e) {
    in_.reset();
    error_ = e.getMessage();
  }
}

MidiInput::~MidiInput() { close(); }

void MidiInput::callback(double, std::vector<unsigned char>* msg, void* user) {
  if (!msg || msg->empty()) return;
  MidiInput* self = static_cast<MidiInput*>(user);
  MidiMessage m{(*msg)[0], msg->size() > 1 ? (*msg)[1] : (uint8_t)0, msg->size() > 2 ? (*msg)[2] : (uint8_t)0,
                std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count()};
  std::lock_guard<std::mutex> lock(self->mutex_);
  if (self->queue_.size() < 4096) self->queue_.push_back(m);
}

std::vector<std::string> MidiInput::ports() {
  std::vector<std::string> names;
  if (!in_) return names;
  try {
    unsigned n = in_->getPortCount();
    for (unsigned i = 0; i < n; i++) names.push_back(in_->getPortName(i));
  } catch (const RtMidiError& e) {
    error_ = e.getMessage();
  }
  return names;
}

bool MidiInput::open(const std::string& name) {
  close();
  if (!in_) return false;
  std::vector<std::string> list = ports();
  for (size_t i = 0; i < list.size(); i++) {
    if (list[i] != name) continue;
    try {
      in_->openPort((unsigned)i, "DATRACKARXD input");
      name_ = name;
      error_.clear();
      return true;
    } catch (const RtMidiError& e) {
      error_ = e.getMessage();
      return false;
    }
  }
  error_ = "MIDI port not found: " + name;
  return false;
}

void MidiInput::close() {
  if (in_ && !name_.empty()) {
    try {
      in_->closePort();
    } catch (const RtMidiError&) {
    }
  }
  name_.clear();
  std::lock_guard<std::mutex> lock(mutex_);
  queue_.clear();
}

std::vector<MidiMessage> MidiInput::drain() {
  std::vector<MidiMessage> out;
  std::lock_guard<std::mutex> lock(mutex_);
  out.swap(queue_);
  return out;
}

void MidiInput::receiveClock(bool on) {
  if (in_) in_->ignoreTypes(true, !on, true);
}

MidiOutput::MidiOutput() {
  try {
    out_ = std::make_unique<RtMidiOut>(RtMidi::UNSPECIFIED, "DATRACKARXD");
  } catch (const RtMidiError& e) {
    out_.reset();
    error_ = e.getMessage();
  }
}

MidiOutput::~MidiOutput() { close(); }

std::vector<std::string> MidiOutput::ports() {
  std::vector<std::string> names;
  if (!out_) return names;
  try {
    unsigned n = out_->getPortCount();
    for (unsigned i = 0; i < n; i++) names.push_back(out_->getPortName(i));
  } catch (const RtMidiError& e) {
    error_ = e.getMessage();
  }
  return names;
}

bool MidiOutput::open(const std::string& name) {
  close();
  if (!out_) return false;
  std::vector<std::string> list = ports();
  for (size_t i = 0; i < list.size(); i++) {
    if (list[i] != name) continue;
    try {
      std::lock_guard<std::mutex> lock(mutex_);
      out_->openPort((unsigned)i, "DATRACKARXD output");
      name_ = name;
      error_.clear();
      return true;
    } catch (const RtMidiError& e) {
      error_ = e.getMessage();
      return false;
    }
  }
  error_ = "MIDI port not found: " + name;
  return false;
}

void MidiOutput::close() {
  std::lock_guard<std::mutex> lock(mutex_);
  if (out_ && !name_.empty()) {
    try {
      out_->closePort();
    } catch (const RtMidiError&) {
    }
  }
  name_.clear();
}

void MidiOutput::send(const uint8_t* bytes, size_t n) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (!out_ || name_.empty() || n == 0) return;
  try {
    out_->sendMessage(bytes, n);
  } catch (const RtMidiError&) {
  }
}

MidiClockSender::MidiClockSender(MidiOutput& out) : out_(out), thread_([this] { run(); }) {}

MidiClockSender::~MidiClockSender() {
  quit_ = true;
  if (thread_.joinable()) thread_.join();
}

void MidiClockSender::update(bool enabled, bool playing, double bpm) {
  enabled_ = enabled;
  playing_ = playing;
  bpm_ = std::max(20.0, std::min(bpm, 400.0));
}

void MidiClockSender::run() {
  using clock = std::chrono::steady_clock;
  bool wasPlaying = false;
  clock::time_point next = clock::now();
  while (!quit_) {
    bool on = enabled_, playing = on && playing_;
    if (playing != wasPlaying) {
      uint8_t m = playing ? 0xFA : 0xFC;  // start / stop
      out_.send(&m, 1);
      wasPlaying = playing;
      next = clock::now();
    }
    if (!playing) {
      std::this_thread::sleep_for(std::chrono::milliseconds(2));
      continue;
    }
    std::this_thread::sleep_until(next);
    uint8_t pulse = 0xF8;
    out_.send(&pulse, 1);
    next += std::chrono::duration_cast<clock::duration>(std::chrono::duration<double>(60.0 / (bpm_ * 24.0)));
    if (next < clock::now() - std::chrono::milliseconds(50)) next = clock::now();  // fell behind: catch up
  }
  if (wasPlaying) {
    uint8_t m = 0xFC;
    out_.send(&m, 1);
  }
}
