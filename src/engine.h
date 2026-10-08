// Playback engine: pattern sequencing, effects, macros and synthesis
// (chip waves, 4-operator FM and samples).
#pragma once
#include <array>
#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

#include "effects.h"
#include "song.h"

constexpr int SCOPE_LEN = 1024;

struct MacroState {
  int pos = 0;
  bool finished = false;
  bool hasValue = false;
  int value = 0;
};

struct FMOpState {
  double phase = 0;
  int stage = 4;     // 0 attack, 1 decay, 2 sustain, 3 release, 4 off
  float att = 96;    // envelope attenuation in dB
  float out = 0;
  double inc = 0;    // phase increment per sample
  float atkK = 0, decStep = 0, susStep = 0, relStep = 0, slDb = 0, tlDb = 0;
};

struct ChannelState {
  bool active = false;  // producing sound
  bool released = false;
  int note = -1;
  int ins = -1;
  int vol = 0x7f;  // 00..7F from the volume column
  int volFrac = 0;  // 1/256 fraction for slow volume slides

  double pitch = 0;  // semitones, before arp/vibrato/macros
  double portaTarget = -1;
  int portaSpeed = 0;
  int pitchSlide = 0;
  int arpX = 0, arpY = 0, arpTick = 0;
  int vibSpeed = 0, vibDepth = 0, vibPos = 0;
  int tremSpeed = 0, tremDepth = 0, tremPos = 0;
  int volSlide = 0;
  float panL = 1, panR = 1;
  int dutyOverride = -1, waveOverride = -1;
  int cutTick = -1;
  int delayTick = -1;
  Cell delayedCell;
  int retrigTicks = 0, retrigCount = 0;
  int loopRow = 0, loopCount = 0;
  float fade = 1;

  MacroState macros[MACRO_COUNT];
  int pitchMacroAcc = 0;
  bool justTriggered = false;  // macros already stepped this tick
  uint32_t triggers = 0;       // counts note starts (for MIDI export)

  // Values computed each tick and consumed by the oscillators.
  double freq = 0;
  float targetGain = 0;
  float fmAtt = 0;  // extra carrier attenuation in dB (FM volume)
  int duty = 2;
  int wave = WAVE_PULSE;
  int table = -1;  // song wavetable, -1 = the instrument's own
  int type = INS_STANDARD;

  // Chip oscillator.
  double phase = 0;
  double noisePhase = 0;
  uint32_t lfsr = 1;
  float gain = 0;  // smoothed gain to avoid clicks

  // Sample playback.
  int smp = -1;
  double smpPos = 0;
  bool smpBackward = false;
  bool smpPlaying = false;
  double smpStep = 0;

  // SID-style extras.
  SVFilter filter;
  bool filterOn = false, ring = false, sync = false, wrapped = false;
  int filterMode = 0;
  int cutoffOverride = -1;  // 13xx
  float pw = -1;            // fine pulse width (0..1), -1 = duty steps
  int pwPos = 128, pwDir = 1;

  // FM.
  FMOpState fm[4];
  float fbHist[2] = {0, 0};

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
  // vol: 00..7F for the channel volume, -1 to keep it.
  void noteOn(int ch, int note, int ins, int vol = -1);
  void noteOff(int ch);
  void resetPan(int ch);
  // Runs one tick without rendering audio (MIDI export).
  void tick() { doTick(); }  // back to the channel's default panning

  // Renders interleaved stereo float frames.
  void render(float* out, int frames);

  int order() const { return order_; }
  int row() const { return row_; }
  int speed() const;
  float tickRate() const { return tickRate_; }
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
  int order_ = 0, row_ = 0, tick_ = 0;
  std::vector<int> speeds_;
  int grooveIdx_ = 0;
  float tickRate_ = 60;
  int nextOrder_ = -1, nextRow_ = -1;  // pending jump/break
  bool loopJump_ = false;              // pending jump comes from a pattern loop
  int rowRepeat_ = 0;                  // pattern delay rows left
  bool skipRow_ = false;
  int loops_ = 0;
  std::vector<bool> visited_;
  double samplesToTick_ = 0;

  float dcL_ = 0, dcR_ = 0, dcPrevL_ = 0, dcPrevR_ = 0;
  Reverb reverb_;
  StereoDelay delay_;
  SVFilter filterL_, filterR_;
  bool reverbWasOn_ = false, delayWasOn_ = false, filterWasOn_ = false;
  void clearEffects();
  std::array<float, SCOPE_LEN> master_{};
  int scopePos_ = 0;

  void resetChannel(int c);
  void doTick();
  void processRow();
  void processCell(int c, const Cell& cell, bool fromDelay);
  void triggerNote(int c, int note, int offset);
  void releaseNote(int c);
  void advanceRow();
  void updateChannel(int c);
  void stepMacros(int c);
  const Instrument* instrument(int c) const;
  float oscillate(ChannelState& s);
  float playSample(ChannelState& s);
  float playFM(ChannelState& s, const Instrument& ins);
  void fmKeyOn(int c);
  void fmKeyOff(int c);
  void fmUpdateRates(int c);
};

// Renders the song from the start to a 16-bit stereo WAV file.
// Plays the song n times (plus one second of tail) to interleaved stereo.
std::vector<float> renderSong(const Song& song, int sampleRate, int loops);
bool exportWav(const Song& song, const std::string& path, int sampleRate, int loops, std::string& err);
// Ogg Vorbis, quality 0..10 (5 is about 160 kbit/s).
bool exportOgg(const Song& song, const std::string& path, int sampleRate, int loops, int quality, std::string& err);
