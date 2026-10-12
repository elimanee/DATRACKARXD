// Playback engine: pattern sequencing, effects, macros and synthesis
// (chip waves, 4-operator FM, samples and emulated sound chips).
#pragma once
#include <array>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "chips.h"
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
  double bend = 0, bendTarget = 0, bendStep = 0;  // E8xx / MIDI pitch wheel, semitones
  double fine = 0;                                // E5xx fine pitch, semitones
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

  // ADSR envelope (run per sample) and LFOs / wavetable morph (per tick).
  bool envOn = false;
  int envStage = 4;  // 0 attack, 1 decay, 2 sustain, 3 release, 4 off
  float envLevel = 0;
  float envAtk = 1, envDecK = 0, envSus = 1, envRelK = 0;
  double noteTime = 0;  // seconds since the note started
  double lfoPhase[INS_LFOS] = {0, 0};
  float lfoHold[INS_LFOS] = {0, 0};  // random shape: the value held this cycle
  float lfoPan = 0;                   // -1..1 added to the panning
  bool morphOn = false;
  float morphPos = 0;  // 0 = the instrument's wavetable, 1 = the morph target

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

  // countIn: rows of metronome clicks before the song starts (recording).
  void play(int order, int row, int countIn = 0);
  void stop();
  bool playing() const { return playing_; }
  void reset();  // silence everything

  // Live note preview, used while editing.
  // vol: 00..7F for the channel volume, -1 to keep it.
  void noteOn(int ch, int note, int ins, int vol = -1);
  void noteOff(int ch);
  void resetPan(int ch);  // back to the channel's default panning
  // Pitch bend in semitones, applied at once (MIDI pitch wheel).
  void setBend(int ch, double semitones);
  // Live MPE controls: filter cutoff (like 13xx) and channel volume (00-7F).
  void setCutoff(int ch, int value);
  void setVolume(int ch, int vol);
  // Runs one tick without rendering audio (MIDI export).
  void tick() { doTick(); }

  // Renders interleaved stereo float frames.
  void render(float* out, int frames);

  int order() const { return order_; }
  int row() const { return row_; }
  int speed() const;
  float tickRate() const { return tickRate_; }
  int loops() const { return loops_; }
  bool loopPattern = false;  // keep repeating the current pattern
  // Loop over orders first..last (inclusive); -1 = no loop range.
  void setLoopRange(int first, int last);
  int loopFirst() const { return loopFirst_; }
  int loopLast() const { return loopLast_; }
  bool metronome = false;   // clicks on the beats while playing (never in exports)
  // Outputs the plain channel mix (no master effects, DC filter or soft clip):
  // used to render instruments into samples.
  bool bakeMode = false;
  float metronomeVolume = 0.5f;
  bool countingIn() const { return playing_ && countIn_ > 0; }
  int currentTick() const { return tick_; }

  const ChannelState& channel(int c) const { return ch_[c]; }
  const ChipVoice* chipVoice(int c) const { return c >= 0 && c < MAX_CHANNELS ? chips_[c].get() : nullptr; }
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
  int loopFirst_ = -1, loopLast_ = -1;
  int countIn_ = 0, countInRow_ = 0;
  // Metronome click.
  float clickEnv_ = 0, clickFreq_ = 1000;
  double clickPhase_ = 0;
  void click(bool accent);
  // Automation values for this tick; NaN = not automated.
  float autoVol_[MAX_CHANNELS], autoPan_[MAX_CHANNELS], autoCut_[MAX_CHANNELS], autoRev_[MAX_CHANNELS], autoDel_[MAX_CHANNELS];
  float autoFx_[MAX_CHANNELS][MAX_CHANNEL_FX][4];
  float autoMasterVol_ = 0, autoMasterCut_ = 0;
  float volSmooth_[MAX_CHANNELS];  // automated volume, smoothed per sample
  void clearAutomation();
  void applyAutomation();
  std::vector<bool> visited_;
  double samplesToTick_ = 0;

  float dcL_ = 0, dcR_ = 0, dcPrevL_ = 0, dcPrevR_ = 0;
  Reverb reverb_;
  StereoDelay delay_;
  SVFilter filterL_, filterR_;
  std::array<std::array<ChannelFxUnit, MAX_CHANNEL_FX>, MAX_CHANNELS> chanFx_;
  std::array<std::unique_ptr<ChipVoice>, MAX_CHANNELS> chips_;  // emulated chip per channel, made on its first note
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
  void advanceLfos(int c);
  // The LFO's shape value (-1..1), and how far it has faded in (0..1).
  float lfoValue(const ChannelState& s, int l, const InsLfo& lfo, float& fade) const;
  uint32_t rng_ = 0x2545F491;  // random LFO shape (an engine-owned generator keeps renders identical)
  float randomUnit();          // -1..1
  const Instrument* instrument(int c) const;
  float oscillate(ChannelState& s);
  float playSample(ChannelState& s);
  float envelope(ChannelState& s);
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
