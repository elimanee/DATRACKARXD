// Playback engine and chiptune synthesizer.
#pragma once
#include <array>
#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

#include "song.h"

constexpr int SCOPE_LEN = 1024;

struct MacroState {
  int pos = 0;
  bool finished = false;
  bool hasValue = false;
  int value = 0;
};

struct ChannelState {
  bool active = false;  // oscillator is sounding
  bool released = false;
  int note = -1;
  int ins = -1;
  int vol = 0x7f;  // 00..7F from the volume column

  double pitch = 0;  // semitones, before arp/vibrato/macros
  double portaTarget = -1;
  int portaSpeed = 0;
  int pitchSlide = 0;
  int arpX = 0, arpY = 0, arpTick = 0;
  int vibSpeed = 0, vibDepth = 0, vibPos = 0;
  int volSlide = 0;
  int panL = 15, panR = 15;
  int dutyOverride = -1, waveOverride = -1;
  int cutTick = -1;
  int delayTick = -1;
  Cell delayedCell;

  MacroState macros[MACRO_COUNT];
  int pitchMacroAcc = 0;
  bool justTriggered = false;  // macros already stepped this tick

  // Values computed each tick and consumed by the oscillator.
  double freq = 0;
  float targetGain = 0;
  int duty = 2;
  int wave = WAVE_PULSE;

  // Oscillator state.
  double phase = 0;
  double noisePhase = 0;
  uint32_t lfsr = 1;
  float gain = 0;  // smoothed gain to avoid clicks

  std::array<float, SCOPE_LEN> scope{};
};

class Engine {
 public:
  Engine(const Song& song, int sampleRate);

  // All public methods must be called with `mutex` held when another thread
  // may be rendering (the audio callback locks it).
  std::mutex mutex;

  void play(int order, int row);
  void stop();
  bool playing() const { return playing_; }
  void reset();  // silence everything

  // Live note preview, used while editing.
  void noteOn(int ch, int note, int ins);
  void noteOff(int ch);

  // Renders interleaved stereo float frames.
  void render(float* out, int frames);

  int order() const { return order_; }
  int row() const { return row_; }
  int speed() const { return speed_; }
  int loops() const { return loops_; }
  bool loopPattern = false;  // keep repeating the current pattern

  const ChannelState& channel(int c) const { return ch_[c]; }
  int scopePos() const { return scopePos_; }
  const std::array<float, SCOPE_LEN>& masterScope() const { return master_; }

  int sampleRate() const { return sampleRate_; }

 private:
  const Song& song_;
  int sampleRate_;
  std::array<ChannelState, MAX_CHANNELS> ch_;

  bool playing_ = false;
  int order_ = 0, row_ = 0, tick_ = 0, speed_ = 6;
  int nextOrder_ = -1, nextRow_ = -1;  // pending jump/break
  int loops_ = 0;
  std::vector<bool> visited_;
  double samplesToTick_ = 0;

  float dcL_ = 0, dcR_ = 0, dcPrevL_ = 0, dcPrevR_ = 0;
  std::array<float, SCOPE_LEN> master_{};
  int scopePos_ = 0;

  void doTick();
  void processRow();
  void processCell(int c, const Cell& cell, bool fromDelay);
  void triggerNote(int c, int note);
  void releaseNote(int c);
  void advanceRow();
  void updateChannel(int c);
  void stepMacros(int c);
  const Instrument* instrument(int c) const;
  float oscillate(ChannelState& s);
};

// Renders the song from the start to a 16-bit stereo WAV file.
bool exportWav(const Song& song, const std::string& path, int sampleRate, int loops, std::string& err);
