// Emulated sound chips, one voice per tracker channel: AY-3-8910 / YM2149,
// SN76489, the Game Boy APU and the OPL3 (through ymfm). Each voice works at
// the register level (periods, LFSRs, volume tables, hardware envelopes),
// so its sound has the real chip's quirks: coarse high notes, 4-bit volume
// steps, the AY "buzzer" envelope.
#pragma once
#include <cstdint>
#include <memory>

#include "song.h"

// What the engine wants from the chip this tick.
struct ChipFrame {
  const Instrument* ins = nullptr;
  double freq = 440;       // Hz
  int vol = 15;            // AY / SN: 0-15, OPL: 0-127
  float chanScale = 1;     // Game Boy: channel volume 0..1 applied to the envelope
  int macroVol = -1;       // Game Boy: volume macro value (software envelope), -1 = hardware envelope
  bool gate = false;       // key held
  uint32_t trigger = 0;    // changes on each new note
  int duty = -1;           // duty macro / 12xx: AY noise period, SN noise mode, GB duty
  int mixer = -1;          // wave macro / 10xx: AY mixer bits (1 tone, 2 noise, 4 envelope), OPL waveform
  float morph = -1;        // wavetable morph position (GB wave channel), -1 = off
};

class ChipVoice {
 public:
  explicit ChipVoice(int type) : type_(type) {}
  virtual ~ChipVoice() = default;
  int type() const { return type_; }
  virtual void update(const ChipFrame& f) = 0;  // once or more per tick
  virtual float render() = 0;                   // one sample at the engine's rate
  // True when the voice can't make a sound until the next note
  // (OPL release done, Game Boy envelope down to 0).
  virtual bool finished() const { return false; }

 private:
  int type_;
};

bool isChipType(int type);
std::unique_ptr<ChipVoice> makeChipVoice(int type, int sampleRate);

// Clock choices per chip type (index stored in ChipSettings::clock).
int chipClockCount(int type);
const char* chipClockName(int type, int index);
double chipClockHz(int type, int index);
