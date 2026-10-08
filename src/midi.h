// MIDI keyboard input (RtMidi). Messages arrive on RtMidi's thread and are
// queued until the UI drains them once per frame.
#pragma once
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

class RtMidiIn;

struct MidiMessage {
  uint8_t status, data1, data2;
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

 private:
  static void callback(double, std::vector<unsigned char>* msg, void* user);
  std::unique_ptr<RtMidiIn> in_;
  std::mutex mutex_;
  std::vector<MidiMessage> queue_;
  std::string name_, error_;
};
