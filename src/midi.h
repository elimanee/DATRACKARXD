// MIDI keyboard input (RtMidi). Messages arrive on RtMidi's thread and are
// queued until the UI drains them once per frame.
#pragma once
#include <cstdint>
#include <memory>
#include <atomic>
#include <mutex>
#include <thread>
#include <string>
#include <vector>

class RtMidiIn;
class RtMidiOut;

struct MidiMessage {
  uint8_t status, data1, data2;
  double time = 0;  // seconds (steady clock) when it arrived
};

class MidiInput {
 public:
  MidiInput();
  ~MidiInput();

  // Names of the input ports present right now (empty without a backend).
  std::vector<std::string> ports();
  // Opens the port with this name; false (and error() set) if it fails.
  bool open(const std::string& name);
  void close();
  const std::string& portName() const { return name_; }
  bool isOpen() const { return !name_.empty(); }
  const std::string& error() const { return error_; }

  // Messages received since the last call, oldest first.
  std::vector<MidiMessage> drain();
  // Let MIDI clock messages through (start, stop, continue, 24 per beat).
  void receiveClock(bool on);

 private:
  static void callback(double, std::vector<unsigned char>* msg, void* user);
  std::unique_ptr<RtMidiIn> in_;
  std::mutex mutex_;
  std::vector<MidiMessage> queue_;
  std::string name_, error_;
};

// MIDI output: notes to an external synth, and the MIDI clock. send() can be
// called from any thread.
class MidiOutput {
 public:
  MidiOutput();
  ~MidiOutput();
  std::vector<std::string> ports();
  bool open(const std::string& name);
  void close();
  const std::string& portName() const { return name_; }
  bool isOpen() const { return !name_.empty(); }
  const std::string& error() const { return error_; }
  void send(const uint8_t* bytes, size_t n);

 private:
  std::unique_ptr<RtMidiOut> out_;
  std::mutex mutex_;
  std::string name_, error_;
};

// Sends the MIDI clock (24 pulses per beat, start / stop) from its own
// thread, so the pulses keep an even pace whatever the UI does.
class MidiClockSender {
 public:
  explicit MidiClockSender(MidiOutput& out);
  ~MidiClockSender();
  // Called every frame: whether to send, whether the song plays, its tempo.
  void update(bool enabled, bool playing, double bpm);

 private:
  void run();
  MidiOutput& out_;
  std::thread thread_;
  std::atomic<bool> quit_{false}, enabled_{false}, playing_{false};
  std::atomic<double> bpm_{120};
};
