// Microphone / line-in recording through SDL's capture devices.
#pragma once
#include <SDL.h>

#include <mutex>
#include <string>
#include <vector>

class Recorder {
 public:
  ~Recorder();
  static std::vector<std::string> devices();
  // device: a name from devices(), or empty for the default input.
  bool start(const std::string& device, int rate, std::string& err);
  std::vector<float> stop();  // the recording, mono
  bool recording() const { return dev_ != 0; }
  float takePeak();  // loudest level since the last call (for a meter)
  double seconds() const;
  int rate() const { return rate_; }

 private:
  static void callback(void* user, Uint8* stream, int len);
  SDL_AudioDeviceID dev_ = 0;
  mutable std::mutex mutex_;
  std::vector<float> data_;
  float peak_ = 0;
  int rate_ = 44100;
};
