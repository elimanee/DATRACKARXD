#include "midi.h"

#include <RtMidi.h>

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
  MidiMessage m{(*msg)[0], msg->size() > 1 ? (*msg)[1] : (uint8_t)0, msg->size() > 2 ? (*msg)[2] : (uint8_t)0};
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
