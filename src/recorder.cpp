#include "recorder.h"

#include <algorithm>
#include <cmath>

Recorder::~Recorder() {
  if (dev_) SDL_CloseAudioDevice(dev_);
}

std::vector<std::string> Recorder::devices() {
  std::vector<std::string> out;
  int n = SDL_GetNumAudioDevices(1);
  for (int i = 0; i < n; i++)
    if (const char* name = SDL_GetAudioDeviceName(i, 1)) out.push_back(name);
  return out;
}

bool Recorder::start(const std::string& device, int rate, std::string& err) {
  if (dev_) return true;
  SDL_AudioSpec want{}, have{};
  want.freq = rate;
  want.format = AUDIO_F32SYS;
  want.channels = 1;
  want.samples = 1024;
  want.callback = callback;
  want.userdata = this;
  dev_ = SDL_OpenAudioDevice(device.empty() ? nullptr : device.c_str(), 1, &want, &have, SDL_AUDIO_ALLOW_FREQUENCY_CHANGE);
  if (!dev_) {
    err = SDL_GetError();
    if (err.empty()) err = "No input device";
    return false;
  }
  {
    std::lock_guard<std::mutex> lock(mutex_);
    data_.clear();
    peak_ = 0;
    rate_ = have.freq;
  }
  SDL_PauseAudioDevice(dev_, 0);
  return true;
}

std::vector<float> Recorder::stop() {
  if (dev_) {
    SDL_CloseAudioDevice(dev_);  // waits for the callback to finish
    dev_ = 0;
  }
  std::lock_guard<std::mutex> lock(mutex_);
  return std::move(data_);
}

float Recorder::takePeak() {
  std::lock_guard<std::mutex> lock(mutex_);
  float p = peak_;
  peak_ = 0;
  return p;
}

double Recorder::seconds() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return data_.size() / (double)std::max(rate_, 1);
}

void Recorder::callback(void* user, Uint8* stream, int len) {
  Recorder* self = static_cast<Recorder*>(user);
  const float* in = reinterpret_cast<const float*>(stream);
  int n = len / (int)sizeof(float);
  std::lock_guard<std::mutex> lock(self->mutex_);
  // At most 10 minutes.
  if (self->data_.size() > (size_t)self->rate_ * 600) return;
  for (int i = 0; i < n; i++) {
    float v = std::clamp(in[i], -1.0f, 1.0f);
    self->data_.push_back(v);
    self->peak_ = std::max(self->peak_, std::abs(v));
  }
}
