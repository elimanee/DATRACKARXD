#include "chips.h"

#include <algorithm>
#include <cmath>

#include "ymfm_opl.h"

namespace {

// One-pole DC blocker: the chips output 0..level, the mix wants it centered.
struct DcBlock {
  float r = 0.9995f, x1 = 0, y1 = 0;
  void init(int sampleRate) { r = 1.0f - 2.0f * 3.14159265f * 8.0f / sampleRate; }
  float run(float x) {
    float y = x - x1 + r * y1;
    x1 = x;
    y1 = y;
    return y;
  }
};

// ---------------------------------------------------------------------------
// AY-3-8910 / YM2149
// ---------------------------------------------------------------------------

// Measured AY-3-8910 volume levels (16 steps, roughly 3 dB apart).
const float AY_VOL[16] = {0.0f,    0.0106f, 0.0150f, 0.0222f, 0.0320f, 0.0466f, 0.0665f, 0.1039f,
                          0.1237f, 0.1986f, 0.2803f, 0.3548f, 0.4702f, 0.6030f, 0.7530f, 1.0f};

// YM2149: 32 levels 1.5 dB apart (the envelope uses all of them, the
// volume register every other one).
struct YmTable {
  float v[32];
  YmTable() {
    v[0] = 0;
    for (int i = 1; i < 32; i++) v[i] = std::pow(10.0f, (i - 31) * 1.5f / 20.0f);
  }
};
const YmTable YM_VOL;

class AYVoice : public ChipVoice {
 public:
  explicit AYVoice(int sampleRate) : ChipVoice(INS_AY), sr_(sampleRate) { dc_.init(sampleRate); }

  void update(const ChipFrame& f) override {
    if (!f.ins) {
      vol_ = 0;
      useEnv_ = false;
      return;
    }
    const ChipSettings& c = f.ins->chip;
    clock_ = chipClockHz(INS_AY, c.clock);
    ym_ = c.ayModel == 1;
    double fr = std::max(f.freq, 1.0);
    period_ = std::clamp((int)std::lround(clock_ / (16 * fr)), 1, 4095);
    int mix = f.mixer >= 0 ? f.mixer : (c.ayTone ? 1 : 0) | (c.ayNoise ? 2 : 0) | (c.ayEnv ? 4 : 0);
    tone_ = mix & 1;
    noise_ = (mix & 2) != 0;
    useEnv_ = (mix & 4) != 0;
    noisePeriod_ = std::max(1, std::clamp(f.duty >= 0 ? f.duty : c.ayNoiseFreq, 0, 31));
    if (c.ayEnvAuto > 0) {
      // The "buzzer": the envelope repeats at a multiple of the note's pitch.
      static const double ratio[6] = {0, 0.25, 0.5, 1, 2, 4};
      envPeriod_ = std::clamp((int)std::lround(clock_ / (256 * fr * ratio[std::clamp(c.ayEnvAuto, 1, 5)])), 1, 65535);
    } else {
      envPeriod_ = std::clamp(c.ayEnvPeriod, 1, 65535);
    }
    vol_ = std::clamp(f.vol, 0, 15);
    if (f.trigger != lastTrigger_) {
      // Like a tracker writing R13 on every note: the envelope restarts.
      lastTrigger_ = f.trigger;
      writeShape(c.ayEnvShape);
    }
  }

  float render() override {
    acc_ += clock_ / 8.0 / sr_;
    float sum = 0;
    int n = 0;
    while (acc_ >= 1.0) {
      acc_ -= 1.0;
      step();
      sum += level();
      n++;
    }
    float v = n ? sum / n : last_;
    last_ = v;
    return dc_.run(v) * 2.0f;
  }

 private:
  void writeShape(int shape) {
    shape_ = shape & 15;
    envAttack_ = (shape_ & 4) != 0;
    envPos_ = 0;
    envHold_ = false;
    envLevel_ = envAttack_ ? 0 : 31;
    envCnt_ = 0;
  }

  // One tick of the chip at clock / 8.
  void step() {
    if (++toneCnt_ >= period_) {
      toneCnt_ = 0;
      toneOut_ = !toneOut_;
    }
    if (++noiseCnt_ >= noisePeriod_ * 2) {
      noiseCnt_ = 0;
      uint32_t bit = (lfsr_ ^ (lfsr_ >> 3)) & 1;  // 17-bit LFSR
      lfsr_ = (lfsr_ >> 1) | (bit << 16);
    }
    if (++envCnt_ >= envPeriod_) {
      envCnt_ = 0;
      envStep();
    }
  }

  // 32 envelope steps per ramp (the AY uses every other one).
  void envStep() {
    if (envHold_) return;
    if (++envPos_ > 31) {
      if (!(shape_ & 8)) {  // one shot, then silence
        envHold_ = true;
        envLevel_ = 0;
        return;
      }
      if (shape_ & 1) {  // hold the end (or its opposite with ALT)
        envHold_ = true;
        envLevel_ = envAttack_ ? 31 : 0;
        if (shape_ & 2) envLevel_ = 31 - envLevel_;
        return;
      }
      envPos_ = 0;
      if (shape_ & 2) envAttack_ = !envAttack_;
    }
    envLevel_ = envAttack_ ? envPos_ : 31 - envPos_;
  }

  float level() const {
    // A disabled tone or noise counts as "on", like the real mixer.
    bool on = (toneOut_ || !tone_) && ((lfsr_ & 1) || !noise_);
    if (!on) return 0;
    if (useEnv_) return ym_ ? YM_VOL.v[envLevel_] : AY_VOL[envLevel_ >> 1];
    return ym_ ? YM_VOL.v[vol_ ? vol_ * 2 + 1 : 0] : AY_VOL[vol_];
  }

  int sr_;
  double clock_ = 1773400, acc_ = 0;
  bool ym_ = false, tone_ = true, noise_ = false, useEnv_ = false;
  int period_ = 1, noisePeriod_ = 16, envPeriod_ = 1000, vol_ = 0;
  int toneCnt_ = 0, noiseCnt_ = 0, envCnt_ = 0;
  bool toneOut_ = false;
  uint32_t lfsr_ = 1;
  int shape_ = 0, envPos_ = 0, envLevel_ = 0;
  bool envAttack_ = false, envHold_ = true;
  uint32_t lastTrigger_ = 0;
  float last_ = 0;
  DcBlock dc_;
};

// ---------------------------------------------------------------------------
// SN76489
// ---------------------------------------------------------------------------

struct SnTable {
  float v[16];  // by attenuation, 2 dB steps, 15 = off
  SnTable() {
    for (int i = 0; i < 15; i++) v[i] = std::pow(10.0f, -2.0f * i / 20.0f);
    v[15] = 0;
  }
};
const SnTable SN_VOL;

class SNVoice : public ChipVoice {
 public:
  explicit SNVoice(int sampleRate) : ChipVoice(INS_SN), sr_(sampleRate) { dc_.init(sampleRate); }

  void update(const ChipFrame& f) override {
    if (!f.ins) {
      vol_ = 0;
      return;
    }
    const ChipSettings& c = f.ins->chip;
    clock_ = chipClockHz(INS_SN, c.clock);
    noise_ = c.snNoise;
    white_ = f.duty >= 0 ? (f.duty & 1) != 0 : c.snWhite;
    double fr = std::max(f.freq, 1.0);
    // Noise shifts on each rise of the tone counter; periodic noise loops
    // every 16 shifts, so its pitch is 16 times lower than the tone's.
    period_ = std::clamp((int)std::lround(clock_ / ((noise_ ? 512 : 32) * fr)), 1, 1023);
    vol_ = std::clamp(f.vol, 0, 15);
    if (f.trigger != lastTrigger_) {
      lastTrigger_ = f.trigger;
      if (noise_) lfsr_ = 0x8000;  // writing the noise register resets it
    }
  }

  float render() override {
    acc_ += clock_ / 16.0 / sr_;
    float sum = 0;
    int n = 0;
    while (acc_ >= 1.0) {
      acc_ -= 1.0;
      if (--cnt_ <= 0) {
        cnt_ = period_;
        out_ = !out_;
        if (noise_ && out_) {
          // Sega variant: 16 bits, white noise taps bits 0 and 3.
          uint32_t fb = white_ ? ((lfsr_ & 1) ^ ((lfsr_ >> 3) & 1)) : (lfsr_ & 1);
          lfsr_ = (lfsr_ >> 1) | (fb << 15);
        }
      }
      bool bit = noise_ ? (lfsr_ & 1) != 0 : out_;
      sum += bit ? SN_VOL.v[15 - vol_] : 0.0f;
      n++;
    }
    float v = n ? sum / n : last_;
    last_ = v;
    return dc_.run(v) * 2.0f;
  }

 private:
  int sr_;
  double clock_ = 3579545, acc_ = 0;
  bool noise_ = false, white_ = true, out_ = false;
  int period_ = 1, cnt_ = 1, vol_ = 0;
  uint32_t lfsr_ = 0x8000, lastTrigger_ = 0;
  float last_ = 0;
  DcBlock dc_;
};

// ---------------------------------------------------------------------------
// Game Boy APU (one of its pulse, wave or noise channels)
// ---------------------------------------------------------------------------

const uint8_t GB_DUTY[4][8] = {
    {0, 0, 0, 0, 0, 0, 0, 1}, {1, 0, 0, 0, 0, 0, 0, 1}, {1, 0, 0, 0, 0, 1, 1, 1}, {0, 1, 1, 1, 1, 1, 1, 0}};

class GBVoice : public ChipVoice {
 public:
  explicit GBVoice(int sampleRate) : ChipVoice(INS_GB), sr_(sampleRate) { dc_.init(sampleRate); }

  void update(const ChipFrame& f) override {
    if (!f.ins) return;
    const Instrument& ins = *f.ins;
    const ChipSettings& c = ins.chip;
    kind_ = std::clamp(c.gbChannel, 0, 2);
    double fr = std::max(f.freq, 1.0);
    int reg = std::clamp((int)std::lround(2048 - (kind_ == 1 ? 65536.0 : 131072.0) / fr), 0, 2047);
    if (f.trigger != lastTrigger_) {
      lastTrigger_ = f.trigger;
      envVol_ = std::clamp(ins.volume, 0, 15);
      envDir_ = c.gbEnvDir;
      envLen_ = c.gbEnvLen;
      envCnt_ = 0;
      sweepTime_ = c.gbSweepTime;
      sweepDir_ = c.gbSweepDir;
      sweepShift_ = c.gbSweepShift;
      sweepCnt_ = 0;
      shadow_ = reg;
      muted_ = false;
      lfsr_ = 0x7FFF;
      phase_ = 0;
    }
    // A running sweep owns the frequency; otherwise it follows the note.
    if (!(kind_ == 0 && sweepTime_ > 0)) shadow_ = reg;
    duty_ = (f.duty >= 0 ? f.duty : ins.duty) & 3;
    shortNoise_ = f.duty >= 0 ? (f.duty & 1) != 0 : c.gbShortNoise;
    chanScale_ = std::clamp(f.chanScale, 0.0f, 1.0f);
    macroVol_ = f.macroVol;
    if (kind_ == 2) {
      // Pick the noise clock (shift s, divider r) closest to the note.
      double want = fr * 64;
      double best = 1e30;
      for (int s = 0; s < 14; s++)
        for (int r = 0; r < 8; r++) {
          double rate = 524288.0 / (r == 0 ? 0.5 : r) / (1 << (s + 1));
          double d = std::abs(std::log(rate / want));
          if (d < best) {
            best = d;
            noiseRate_ = rate;
          }
        }
    }
    if (kind_ == 1) {
      for (int i = 0; i < WAVETABLE_LEN; i++) {
        float v = (float)ins.wavetable[i];
        if (f.morph >= 0) v += (ins.morph.table[i] - v) * f.morph;
        wave_[i] = std::clamp((int)std::lround(v), 0, 15);
      }
      // The wave channel only has 4 volume settings: off, 25%, 50%, 100%.
      int lvl = (int)std::lround((macroVol_ >= 0 ? macroVol_ : ins.volume) * chanScale_);
      waveShift_ = lvl == 0 ? 4 : lvl <= 4 ? 2 : lvl <= 10 ? 1 : 0;
    }
  }

  float render() override {
    // Frame sequencer at 512 Hz: envelope at 64 Hz, sweep at 128 Hz.
    seqAcc_ += 512.0 / sr_;
    while (seqAcc_ >= 1.0) {
      seqAcc_ -= 1.0;
      seqStep_ = (seqStep_ + 1) & 7;
      if (seqStep_ == 7) envClock();
      if (seqStep_ == 2 || seqStep_ == 6) sweepClock();
    }
    const int OS = 4;  // oversampling
    double freq = kind_ == 1 ? 65536.0 / (2048 - shadow_) : 131072.0 / (2048 - shadow_);
    float sum = 0;
    for (int k = 0; k < OS; k++) {
      int lvl = 0;
      if (!muted_) {
        if (kind_ == 0) {
          lvl = GB_DUTY[duty_][(int)(phase_ * 8) & 7] ? volume() : 0;
        } else if (kind_ == 1) {
          lvl = waveShift_ >= 4 ? 0 : wave_[(int)(phase_ * 32) & 31] >> waveShift_;
        } else {
          noiseAcc_ += noiseRate_ / (sr_ * OS);
          int guard = 0;
          while (noiseAcc_ >= 1.0 && guard++ < 64) {
            noiseAcc_ -= 1.0;
            uint32_t bit = (lfsr_ ^ (lfsr_ >> 1)) & 1;
            lfsr_ = (lfsr_ >> 1) | (bit << 14);
            if (shortNoise_) lfsr_ = (lfsr_ & ~0x40u) | (bit << 6);
          }
          if (noiseAcc_ >= 1.0) noiseAcc_ = std::fmod(noiseAcc_, 1.0);
          lvl = (~lfsr_ & 1) ? volume() : 0;
        }
      }
      sum += lvl / 15.0f;
      phase_ += freq / (sr_ * OS);
      if (phase_ >= 1.0) phase_ -= std::floor(phase_);
    }
    return dc_.run(sum / OS) * 2.0f;
  }

  bool finished() const override {
    if (muted_) return true;
    if (kind_ == 1 || macroVol_ >= 0) return false;
    return envVol_ == 0 && envDir_ == 0;
  }

 private:
  int volume() const {
    int base = macroVol_ >= 0 ? macroVol_ : envVol_;
    return std::clamp((int)std::lround(base * chanScale_), 0, 15);
  }
  void envClock() {
    if (envLen_ == 0) return;
    if (++envCnt_ >= envLen_) {
      envCnt_ = 0;
      if (envDir_ && envVol_ < 15) envVol_++;
      else if (!envDir_ && envVol_ > 0) envVol_--;
    }
  }
  void sweepClock() {
    if (kind_ != 0 || sweepTime_ == 0) return;
    if (++sweepCnt_ >= sweepTime_) {
      sweepCnt_ = 0;
      int d = shadow_ >> sweepShift_;
      int next = sweepDir_ ? shadow_ - d : shadow_ + d;
      if (next > 2047) muted_ = true;
      else if (sweepShift_) shadow_ = std::max(0, next);
    }
  }

  int sr_;
  int kind_ = 0, duty_ = 2;
  int shadow_ = 0;  // 11-bit frequency register
  double phase_ = 0, seqAcc_ = 0, noiseAcc_ = 0, noiseRate_ = 1000;
  int seqStep_ = 0;
  int envVol_ = 0, envDir_ = 0, envLen_ = 0, envCnt_ = 0;
  int sweepTime_ = 0, sweepDir_ = 0, sweepShift_ = 0, sweepCnt_ = 0;
  bool muted_ = false, shortNoise_ = false;
  uint32_t lfsr_ = 0x7FFF, lastTrigger_ = 0;
  int wave_[WAVETABLE_LEN] = {};
  int waveShift_ = 0;
  float chanScale_ = 1;
  int macroVol_ = -1;
  DcBlock dc_;
};

// ---------------------------------------------------------------------------
// OPL3 through ymfm: one chip per voice, using channel 0 (and 3 in 4-op mode)
// ---------------------------------------------------------------------------

constexpr uint32_t OPL3_CLOCK = 14318180;

class OPLVoice : public ChipVoice, public ymfm::ymfm_interface {
 public:
  explicit OPLVoice(int sampleRate) : ChipVoice(INS_OPL), chip_(*this), sr_(sampleRate) {
    chip_.reset();
    std::fill(std::begin(regs_), std::end(regs_), -1);
    write(0x105, 0x01);  // OPL3 mode: 8 waveforms, 4-op, stereo bits
    chipRate_ = chip_.sample_rate(OPL3_CLOCK);
  }

  void update(const ChipFrame& f) override {
    if (!f.ins) return;
    const OPLParams& p = f.ins->opl;
    bool four = p.fourOp;
    write(0x104, four ? 0x01 : 0x00);
    int alg = std::clamp(p.alg, 0, four ? 3 : 1);
    bool carrier[4] = {false, true, false, false};
    if (!four) {
      carrier[0] = alg == 1;
    } else {
      static const bool C4[4][4] = {{0, 0, 0, 1}, {1, 0, 0, 1}, {0, 1, 0, 1}, {1, 0, 1, 1}};
      for (int k = 0; k < 4; k++) carrier[k] = C4[alg][k];
    }
    static const int SLOT[4] = {0x00, 0x03, 0x08, 0x0B};
    int att = (127 - std::clamp(f.vol, 0, 127)) / 2;
    for (int k = 0; k < (four ? 4 : 2); k++) {
      const OPLOperator& op = p.ops[k];
      int s = SLOT[k];
      int tl = carrier[k] ? std::min(63, op.tl + att) : op.tl;
      int ws = f.mixer >= 0 ? (f.mixer & 7) : op.ws;
      write(0x20 + s, (op.am ? 0x80 : 0) | (op.vib ? 0x40 : 0) | (op.sus ? 0x20 : 0) | (op.ksr ? 0x10 : 0) | (op.mult & 15));
      write(0x40 + s, ((op.ksl & 3) << 6) | (tl & 63));
      write(0x60 + s, ((op.ar & 15) << 4) | (op.dr & 15));
      write(0x80 + s, ((op.sl & 15) << 4) | (op.rr & 15));
      write(0xE0 + s, ws & 7);
    }
    write(0xC0, 0x30 | ((p.fb & 7) << 1) | (alg & 1));
    if (four) write(0xC3, 0x30 | ((alg >> 1) & 1));
    write(0xBD, (p.deepAm ? 0x80 : 0) | (p.deepVib ? 0x40 : 0));

    // F-number and block: the smallest block that fits.
    double fr = std::max(f.freq, 1.0);
    int block = 0, fnum = 0;
    for (block = 0; block < 8; block++) {
      fnum = (int)std::lround(fr * (1 << (20 - block)) / chipRate_);
      if (fnum < 1024) break;
    }
    block = std::min(block, 7);
    fnum = std::clamp(fnum, 0, 1023);

    if (f.trigger != lastTrigger_) {
      lastTrigger_ = f.trigger;
      if (keyOn_) {
        // Key off for one sample so the envelopes restart.
        write(0xB0, (block << 2) | (fnum >> 8));
        generate();
        keyOn_ = false;
      }
    }
    keyOn_ = f.gate;
    if (keyOn_) quiet_ = 0;
    write(0xA0, fnum & 0xFF);
    write(0xB0, (keyOn_ ? 0x20 : 0) | (block << 2) | (fnum >> 8));
  }

  float render() override {
    pos_ += chipRate_ / sr_;
    while (pos_ >= 1.0) {
      pos_ -= 1.0;
      generate();
    }
    return (prev_ + (cur_ - prev_) * (float)pos_) * (1.0f / 6000.0f);
  }

  bool finished() const override { return !keyOn_ && quiet_ > 4096; }

 private:
  void write(int reg, int val) {
    if (regs_[reg & 0x1FF] == val) return;
    regs_[reg & 0x1FF] = val;
    if (reg & 0x100) chip_.write_address_hi((uint8_t)(reg & 0xFF));
    else chip_.write_address((uint8_t)(reg & 0xFF));
    chip_.write_data((uint8_t)val);
  }
  void generate() {
    ymfm::ymf262::output_data out;
    chip_.generate(&out, 1);
    prev_ = cur_;
    cur_ = (float)out.data[0];
    quiet_ = std::abs(cur_) < 4 ? quiet_ + 1 : 0;
  }

  ymfm::ymf262 chip_;
  int sr_;
  double chipRate_ = 49716, pos_ = 0;
  float prev_ = 0, cur_ = 0;
  int regs_[0x200];
  bool keyOn_ = false;
  int quiet_ = 1 << 20;
  uint32_t lastTrigger_ = 0;
};

struct ClockInfo {
  const char* name;
  double hz;
};
const ClockInfo AY_CLOCKS[] = {{"ZX Spectrum 1.7734 MHz", 1773400}, {"Amstrad CPC 1 MHz", 1000000},
                               {"Atari ST 2 MHz", 2000000}, {"MSX 1.7898 MHz", 1789773}};
const ClockInfo SN_CLOCKS[] = {{"NTSC 3.5795 MHz", 3579545}, {"PAL 3.5469 MHz", 3546893}, {"BBC Micro 4 MHz", 4000000}};
const ClockInfo GB_CLOCKS[] = {{"Game Boy 4.1943 MHz", 4194304}};
const ClockInfo OPL_CLOCKS[] = {{"OPL3 14.318 MHz", OPL3_CLOCK}};

const ClockInfo* clocks(int type, int& count) {
  switch (type) {
    case INS_AY: count = 4; return AY_CLOCKS;
    case INS_SN: count = 3; return SN_CLOCKS;
    case INS_GB: count = 1; return GB_CLOCKS;
    case INS_OPL: count = 1; return OPL_CLOCKS;
    default: count = 0; return nullptr;
  }
}

}  // namespace

bool isChipType(int type) { return type == INS_AY || type == INS_SN || type == INS_GB || type == INS_OPL; }

std::unique_ptr<ChipVoice> makeChipVoice(int type, int sampleRate) {
  switch (type) {
    case INS_AY: return std::make_unique<AYVoice>(sampleRate);
    case INS_SN: return std::make_unique<SNVoice>(sampleRate);
    case INS_GB: return std::make_unique<GBVoice>(sampleRate);
    case INS_OPL: return std::make_unique<OPLVoice>(sampleRate);
    default: return nullptr;
  }
}

int chipClockCount(int type) {
  int n = 0;
  clocks(type, n);
  return n;
}

const char* chipClockName(int type, int index) {
  int n = 0;
  const ClockInfo* c = clocks(type, n);
  return n ? c[std::clamp(index, 0, n - 1)].name : "";
}

double chipClockHz(int type, int index) {
  int n = 0;
  const ClockInfo* c = clocks(type, n);
  return n ? c[std::clamp(index, 0, n - 1)].hz : 1000000;
}
