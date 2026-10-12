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

// A voice's registers as the chip sees them (VGM export). Counters change
// whenever the voice restarts something the chip only restarts on a write.
struct ChipRegs {
  int type = INS_STANDARD;
  double clock = 0;
  // AY / YM2149: period 12-bit, noise 5-bit, envelope period and shape,
  // volume 0-15 or 16 for the envelope, mixer.
  int ayPeriod = 1, ayNoise = 1, ayEnvPeriod = 1, ayEnvShape = 0, ayVol = 0;
  bool ayTone = true, ayNoiseOn = false, ym = false;
  uint32_t ayShapeWrites = 0;
  // SN76489: 10-bit period, attenuation 0-15 (15 = off), noise mode.
  int snPeriod = 1, snAtten = 15;
  bool snNoise = false, snWhite = true;
  uint32_t snNoiseResets = 0;
  // Game Boy: channel kind, 11-bit frequency, duty, envelope at the
  // trigger, sweep (NR10), noise clock, wave RAM and its volume code.
  int gbKind = 0, gbFreq = 0, gbDuty = 2, gbEnvVol = 0, gbEnvDir = 0, gbEnvLen = 0, gbSweep = 0;
  int gbNoiseShift = 0, gbNoiseDiv = 0, gbWaveVol = 1, gbSoftVol = -1;
  bool gbShort = false;
  int gbWave[WAVETABLE_LEN] = {};
  uint32_t gbTriggers = 0;
  // OPL3, laid out on channel 0 (and 3 in 4-op mode): per operator the
  // 0x20, 0x40, 0x60, 0x80 and 0xE0 registers, then the channel registers.
  int oplOp[4][5] = {};
  int oplC0 = 0, oplC3 = 0, oplBD = 0, oplFnum = 0, oplBlock = 0;
  bool oplKey = false, oplFour = false;
  uint32_t oplRetriggers = 0;
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
  virtual void regs(ChipRegs& out) const = 0;

 private:
  int type_;
};

bool isChipType(int type);
std::unique_ptr<ChipVoice> makeChipVoice(int type, int sampleRate);

// Clock choices per chip type (index stored in ChipSettings::clock).
int chipClockCount(int type);
const char* chipClockName(int type, int index);
double chipClockHz(int type, int index);
