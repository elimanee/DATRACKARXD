#include "engine.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <fstream>

namespace {

constexpr double PI = 3.14159265358979323846;
constexpr double PITCH_UNIT = 1.0 / 32.0;  // semitones per effect step
const float DUTY_TABLE[4] = {0.125f, 0.25f, 0.5f, 0.75f};

double noteFreq(double pitch) { return 440.0 * std::pow(2.0, (pitch - 57.0) / 12.0); }

// PolyBLEP residual, softens the edges of pulse and saw waves.
float polyBlep(double t, double dt) {
  if (dt <= 0) return 0;
  if (t < dt) {
    t /= dt;
    return (float)(t + t - t * t - 1.0);
  }
  if (t > 1.0 - dt) {
    t = (t - 1.0) / dt;
    return (float)(t * t + t + t + 1.0);
  }
  return 0;
}

// Lookup tables for FM: sine and attenuation (dB) to amplitude.
constexpr int SIN_LEN = 4096;
constexpr int DB_STEPS = 16;  // table entries per dB
constexpr int DB_MAX = 200;
struct Tables {
  float sine[SIN_LEN];
  float amp[DB_MAX * DB_STEPS];
  Tables() {
    for (int i = 0; i < SIN_LEN; i++) sine[i] = (float)std::sin(i * 2.0 * PI / SIN_LEN);
    for (int i = 0; i < DB_MAX * DB_STEPS; i++) amp[i] = (float)std::pow(10.0, -(i / (double)DB_STEPS) / 20.0);
  }
};
const Tables& tables() {
  static Tables t;
  return t;
}
inline float dbToAmp(float db) {
  int i = (int)(db * DB_STEPS);
  if (i <= 0) return 1.0f;
  if (i >= DB_MAX * DB_STEPS) return 0.0f;
  return tables().amp[i];
}
inline float sinCycles(double cycles) {
  double f = cycles - std::floor(cycles);
  return tables().sine[(int)(f * SIN_LEN) & (SIN_LEN - 1)];
}

// Seconds for an OPN envelope to move 96 dB at effective rate r (0-63).
double envTime(int r) {
  if (r <= 0) return 1e9;
  return 12.0 * std::pow(2.0, -(r - 4) / 4.0);
}

// Which operators are carriers for each algorithm.
const bool CARRIERS[8][4] = {
    {0, 0, 0, 1}, {0, 0, 0, 1}, {0, 0, 0, 1}, {0, 0, 0, 1}, {0, 1, 0, 1}, {0, 1, 1, 1}, {0, 1, 1, 1}, {1, 1, 1, 1},
};

void linearPan(int p, float& l, float& r) {
  l = std::min(1.0f, (255 - p) / 128.0f);
  r = std::min(1.0f, p / 128.0f);
}

}  // namespace

Engine::Engine(const Song& song, int sampleRate) : song_(song), sampleRate_(sampleRate) {
  tables();
  reset();
}

void Engine::resetChannel(int c) {
  std::array<float, SCOPE_LEN> scope = ch_[c].scope;
  ch_[c] = ChannelState();
  ch_[c].scope = scope;
  if (c < song_.channelCount()) linearPan(song_.channels[c].pan, ch_[c].panL, ch_[c].panR);
}

void Engine::reset() {
  for (int c = 0; c < MAX_CHANNELS; c++) resetChannel(c);
  playing_ = false;
  speeds_ = song_.speeds;
  tickRate_ = song_.tickRate;
  samplesToTick_ = 0;
}

int Engine::speed() const {
  const std::vector<int>& sp = playing_ ? speeds_ : song_.speeds;
  if (sp.empty()) return 6;
  return std::max(1, sp[grooveIdx_ % sp.size()]);
}

void Engine::play(int order, int row) {
  for (int c = 0; c < MAX_CHANNELS; c++) resetChannel(c);
  order_ = std::clamp(order, 0, (int)song_.orders.size() - 1);
  row_ = std::clamp(row, 0, song_.patternLength - 1);
  tick_ = 0;
  speeds_ = song_.speeds;
  if (speeds_.empty()) speeds_ = {6};
  grooveIdx_ = 0;
  tickRate_ = song_.tickRate;
  nextOrder_ = nextRow_ = -1;
  loopJump_ = false;
  rowRepeat_ = 0;
  skipRow_ = false;
  loops_ = 0;
  visited_.assign(MAX_ORDERS, false);
  visited_[order_] = true;
  samplesToTick_ = 0;
  playing_ = true;
}

void Engine::stop() {
  playing_ = false;
  for (int c = 0; c < MAX_CHANNELS; c++) releaseNote(c);
}

const Instrument* Engine::instrument(int c) const {
  int i = ch_[c].ins;
  if (i < 0 || i >= (int)song_.instruments.size()) return nullptr;
  return &song_.instruments[i];
}

void Engine::noteOn(int c, int note, int ins, int vol) {
  if (c < 0 || c >= MAX_CHANNELS) return;
  if (ins >= 0) ch_[c].ins = ins;
  if (vol >= 0) ch_[c].vol = std::min(vol, 0x7f);
  ch_[c].portaTarget = -1;
  ch_[c].pitchSlide = 0;
  triggerNote(c, note, 0);
  updateChannel(c);
}

void Engine::noteOff(int c) {
  if (c < 0 || c >= MAX_CHANNELS) return;
  releaseNote(c);
}

// ---------------------------------------------------------------------------
// FM
// ---------------------------------------------------------------------------

void Engine::fmUpdateRates(int c) {
  ChannelState& s = ch_[c];
  const Instrument* ins = instrument(c);
  if (!ins) return;
  int keyCode = std::clamp((int)(s.pitch / 3.0), 0, 31);
  for (int o = 0; o < 4; o++) {
    const FMOperator& op = ins->fm.ops[o];
    FMOpState& st = s.fm[o];
    int ks = keyCode >> (3 - std::clamp(op.ksr, 0, 3));
    auto rate = [&](int r) { return r == 0 ? 0 : std::min(63, r + ks); };
    double sr = sampleRate_;
    int ar = rate(op.ar * 2);
    st.atkK = ar >= 62 ? 1.0f : (float)(std::log(13.0) / (envTime(ar) / 3.0 * sr));
    st.decStep = (float)(96.0 / (envTime(rate(op.dr * 2)) * sr));
    st.susStep = (float)(96.0 / (envTime(rate(op.d2r * 2)) * sr));
    st.relStep = (float)(96.0 / (envTime(rate(op.rr * 4 + 2)) * sr));
    st.slDb = op.sl >= 15 ? 96.0f : op.sl * 3.0f;
    st.tlDb = op.tl * 0.75f;
  }
}

void Engine::fmKeyOn(int c) {
  ChannelState& s = ch_[c];
  const Instrument* ins = instrument(c);
  if (!ins) return;
  fmUpdateRates(c);
  for (int o = 0; o < 4; o++) {
    FMOpState& st = s.fm[o];
    if (!ins->fm.ops[o].enabled) {
      st.stage = 4;
      st.att = 96;
      continue;
    }
    st.phase = 0;
    st.stage = st.atkK >= 1.0f ? 1 : 0;
    if (st.stage == 1) st.att = 0;
  }
  s.fbHist[0] = s.fbHist[1] = 0;
}

void Engine::fmKeyOff(int c) {
  for (auto& st : ch_[c].fm)
    if (st.stage < 3) st.stage = 3;
}

float Engine::playFM(ChannelState& s, const Instrument& ins) {
  int alg = std::clamp(ins.fm.alg, 0, 7);
  float out[4];
  auto run = [&](int i, double mod) {
    FMOpState& st = s.fm[i];
    switch (st.stage) {
      case 0:
        st.att -= st.atkK * (st.att + 8.0f);
        if (st.att <= 0) {
          st.att = 0;
          st.stage = 1;
        }
        break;
      case 1:
        st.att += st.decStep;
        if (st.att >= st.slDb) {
          st.att = st.slDb;
          st.stage = 2;
        }
        break;
      case 2:
        st.att = std::min(96.0f, st.att + st.susStep);
        break;
      case 3:
        st.att += st.relStep;
        if (st.att >= 96) {
          st.att = 96;
          st.stage = 4;
        }
        break;
      default:
        break;
    }
    float v = 0;
    if (st.stage != 4) {
      float db = st.att + st.tlDb + (CARRIERS[alg][i] ? s.fmAtt : 0.0f);
      v = sinCycles(st.phase + mod) * dbToAmp(db);
    }
    st.phase += st.inc;
    if (st.phase >= 1.0) st.phase -= std::floor(st.phase);
    out[i] = v;
    return v;
  };
  const double M = 4.0;  // phase deviation, in cycles, of a full-level modulator
  double fbMod = ins.fm.fb ? (s.fbHist[0] + s.fbHist[1]) * std::pow(2.0, ins.fm.fb - 7) : 0.0;
  float o0 = run(0, fbMod);
  s.fbHist[1] = s.fbHist[0];
  s.fbHist[0] = o0;
  float result = 0;
  switch (alg) {
    case 0: run(1, o0 * M); run(2, out[1] * M); result = run(3, out[2] * M); break;
    case 1: run(1, 0); run(2, (o0 + out[1]) * M); result = run(3, out[2] * M); break;
    case 2: run(1, 0); run(2, out[1] * M); result = run(3, (o0 + out[2]) * M); break;
    case 3: run(1, o0 * M); run(2, 0); result = run(3, (out[1] + out[2]) * M); break;
    case 4: result = run(1, o0 * M); run(2, 0); result += run(3, out[2] * M); break;
    case 5: result = run(1, o0 * M) + run(2, o0 * M) + run(3, o0 * M); break;
    case 6: result = run(1, o0 * M) + run(2, 0) + run(3, 0); break;
    default: result = o0 + run(1, 0) + run(2, 0) + run(3, 0); break;
  }
  return result * 0.6f;
}

// ---------------------------------------------------------------------------
// Notes, macros, effects
// ---------------------------------------------------------------------------

void Engine::triggerNote(int c, int note, int offset) {
  ChannelState& s = ch_[c];
  const Instrument* ins = instrument(c);
  s.note = note;
  s.pitch = note;
  s.active = true;
  s.released = false;
  s.arpTick = 0;
  s.vibPos = 0;
  s.tremPos = 0;
  s.pitchMacroAcc = 0;
  s.dutyOverride = -1;
  s.waveOverride = -1;
  s.fade = 1;
  s.lfsr = 1;
  s.type = ins ? ins->type : INS_STANDARD;
  for (auto& m : s.macros) m = MacroState();
  stepMacros(c);
  s.justTriggered = true;

  if (s.type == INS_SAMPLE) {
    int idx = ins->sample;
    if (!ins->sampleMap.empty() && note >= 0 && note < NOTE_COUNT && ins->sampleMap[note] >= 0) idx = ins->sampleMap[note];
    s.smp = idx;
    s.smpPlaying = idx >= 0 && idx < (int)song_.samples.size() && !song_.samples[idx].data.empty();
    s.smpBackward = false;
    s.smpPos = s.smpPlaying ? std::min<double>(offset, song_.samples[idx].data.size() - 1) : 0;
    if (!s.smpPlaying) s.active = false;
  } else if (s.type == INS_FM) {
    updateChannel(c);
    fmKeyOn(c);
  }
}

void Engine::releaseNote(int c) {
  ChannelState& s = ch_[c];
  if (!s.active) return;
  s.released = true;
  const Instrument* ins = instrument(c);
  if (s.type == INS_FM) {
    fmKeyOff(c);
  } else if (!ins || (ins->macros[MACRO_VOL].release < 0 && ins->fadeout <= 0)) {
    // With a release point or a fadeout the sound carries on, otherwise cut.
    s.active = false;
  }
  for (int m = 0; m < MACRO_COUNT; m++) {
    if (!ins) break;
    const Macro& mac = ins->macros[m];
    if (mac.release >= 0 && s.macros[m].pos <= mac.release && !s.macros[m].finished)
      s.macros[m].pos = std::min(mac.release + 1, (int)mac.values.size() - 1);
  }
}

void Engine::stepMacros(int c) {
  ChannelState& s = ch_[c];
  const Instrument* ins = instrument(c);
  for (int m = 0; m < MACRO_COUNT; m++) {
    MacroState& ms = s.macros[m];
    if (!ins || ins->macros[m].values.empty()) {
      ms.hasValue = false;
      continue;
    }
    const Macro& mac = ins->macros[m];
    int n = (int)mac.values.size();
    if (ms.pos >= n) ms.pos = n - 1;
    ms.value = mac.values[ms.pos];
    ms.hasValue = true;
    if (m == MACRO_PITCH) s.pitchMacroAcc += ms.value;
    if (ms.finished) continue;

    // Hold at the release point (or loop before it) until the note is released.
    if (!s.released && mac.release >= 0 && ms.pos == mac.release) {
      if (mac.loop >= 0 && mac.loop < mac.release) ms.pos = mac.loop;
      continue;
    }
    ms.pos++;
    if (ms.pos >= n) {
      bool canLoop = mac.loop >= 0 && (mac.release < 0 || mac.loop > mac.release || !s.released);
      if (canLoop) {
        ms.pos = mac.loop;
      } else {
        ms.pos = n - 1;
        ms.finished = true;
      }
    }
  }
  // The pitch macro is cumulative; a finished one stops adding.
  if (s.macros[MACRO_PITCH].finished) s.macros[MACRO_PITCH].hasValue = false;
}

void Engine::processCell(int c, const Cell& cell, bool fromDelay) {
  ChannelState& s = ch_[c];
  int cols = std::clamp(song_.channels[c].effectCols, 1, MAX_EFFECTS);

  // Note delay: defer the whole cell.
  if (!fromDelay) {
    for (int e = 0; e < cols; e++) {
      if (cell.fx[e].cmd == 0xED && cell.fx[e].val > 0) {
        s.delayTick = cell.fx[e].val;
        s.delayedCell = cell;
        return;
      }
    }
  }

  bool tonePorta = false;
  int offset = 0;
  for (int e = 0; e < cols; e++) {
    int cmd = cell.fx[e].cmd, val = std::max<int>(cell.fx[e].val, 0);
    if ((cmd == 0x03 && val > 0) || cmd == 0x06) tonePorta = true;
    if (cmd == 0x90) offset |= val;
    if (cmd == 0x91) offset |= val << 8;
    if (cmd == 0x92) offset |= val << 16;
  }

  if (cell.ins >= 0) {
    s.ins = cell.ins;
    const Instrument* ins = instrument(c);
    if (song_.insResetsVolume && ins) {
      int vol = 0x7f;
      if (ins->type == INS_SAMPLE) {
        int idx = ins->sample;
        int n = cell.note >= 0 ? cell.note : s.note;
        if (!ins->sampleMap.empty() && n >= 0 && n < NOTE_COUNT && ins->sampleMap[n] >= 0) idx = ins->sampleMap[n];
        if (idx >= 0 && idx < (int)song_.samples.size()) vol = song_.samples[idx].volume * 127 / 64;
      }
      s.vol = vol;
    }
  }
  if (cell.vol >= 0) {
    s.vol = cell.vol;
    s.volFrac = 0;
  }

  if (cell.note == NOTE_OFF) {
    releaseNote(c);
  } else if (cell.note >= 0) {
    if (tonePorta && s.active) {
      s.portaTarget = cell.note;
      s.note = cell.note;
    } else {
      s.portaTarget = -1;
      triggerNote(c, cell.note, offset);
    }
  }

  for (int e = 0; e < cols; e++) {
    int cmd = cell.fx[e].cmd;
    int val = std::max<int>(cell.fx[e].val, 0);
    int x = val >> 4, y = val & 15;
    switch (cmd) {
      case 0x00: s.arpX = x; s.arpY = y; break;
      case 0x01: s.pitchSlide = val; s.portaTarget = -1; break;
      case 0x02: s.pitchSlide = -val; s.portaTarget = -1; break;
      case 0x03:
        s.portaSpeed = val;
        s.pitchSlide = 0;
        if (val == 0) s.portaTarget = -1;
        break;
      case 0x04:
        if (x) s.vibSpeed = x;
        s.vibDepth = y;
        if (!x && !y) s.vibSpeed = 0;
        break;
      case 0x05: s.volSlide = x ? x : -y; break;  // vibrato continues
      case 0x06: s.volSlide = x ? x : -y; break;  // portamento continues
      case 0x07:
        if (x) s.tremSpeed = x;
        s.tremDepth = y;
        break;
      case 0x08: s.panL = x / 15.0f; s.panR = y / 15.0f; break;
      case 0x80: linearPan(val, s.panL, s.panR); break;
      case 0x0A: s.volSlide = x ? x : -y; break;
      case 0x0B:
        if (playing_ && !fromDelay) {
          nextOrder_ = val;
          if (nextRow_ < 0) nextRow_ = 0;
        }
        break;
      case 0x0C: s.retrigTicks = val; s.retrigCount = 0; break;
      case 0x0D:
        if (playing_ && !fromDelay) {
          if (nextOrder_ < 0) nextOrder_ = order_ + 1;
          nextRow_ = val;
        }
        break;
      case 0x0F:
        if (val > 0) speeds_ = {val};
        break;
      case 0x10: s.waveOverride = val; break;
      case 0x12: s.dutyOverride = val & 3; break;
      case 0xE6:
        if (!playing_ || fromDelay) break;
        if (val == 0) {
          s.loopRow = row_;
        } else {
          if (s.loopCount == 0) s.loopCount = val + 1;
          if (--s.loopCount > 0) {
            nextOrder_ = order_;
            nextRow_ = s.loopRow;
            loopJump_ = true;
          }
        }
        break;
      case 0xC0: case 0xC1: case 0xC2: case 0xC3: {
        int hz = ((cmd & 3) << 8) | val;
        if (hz > 0) tickRate_ = (float)hz;
        break;
      }
      case 0xE1: case 0xE2:
        // Note slide: y semitones up/down at speed x.
        if (y) {
          s.portaTarget = std::clamp(s.pitch + (cmd == 0xE1 ? y : -y), 0.0, 131.0);
          s.portaSpeed = std::max(1, x * 4);
          s.pitchSlide = 0;
        }
        break;
      case 0xEC: s.cutTick = val; break;
      case 0xEE:
        if (playing_ && !skipRow_ && rowRepeat_ == 0) rowRepeat_ = val;
        break;
      case 0xF0:
        if (val > 0) tickRate_ = val * 2.0f / 5.0f;
        break;
      case 0xF1: s.pitch = std::min(131.0, s.pitch + val * PITCH_UNIT); break;
      case 0xF2: s.pitch = std::max(0.0, s.pitch - val * PITCH_UNIT); break;
      case 0xF3: s.vol = std::min(0x7f, s.vol + val); s.volFrac = 0; break;
      case 0xF4: s.vol = std::max(0, s.vol - val); s.volFrac = 0; break;
      default: break;
    }
  }
}

void Engine::processRow() {
  for (int c = 0; c < song_.channelCount(); c++) {
    ch_[c].cutTick = -1;
    ch_[c].delayTick = -1;
    const Cell* cell = song_.cell(c, order_, row_);
    if (cell) processCell(c, *cell, false);
  }
}

void Engine::advanceRow() {
  int numOrders = (int)song_.orders.size();
  int newOrder = order_, newRow = row_ + 1;
  bool loopJump = loopJump_;
  loopJump_ = false;
  if (nextOrder_ >= 0 || nextRow_ >= 0) {
    newOrder = nextOrder_ >= 0 ? nextOrder_ : order_ + 1;
    newRow = std::max(nextRow_, 0);
    nextOrder_ = nextRow_ = -1;
  } else if (newRow >= song_.patternLength) {
    newRow = 0;
    newOrder = order_ + 1;
  }
  if (loopPattern) newOrder = order_;
  if (newOrder >= numOrders) newOrder = 0;
  if (newRow >= song_.patternLength) newRow = 0;
  if (newOrder != order_)
    for (auto& s : ch_) s.loopRow = 0;

  // Count a loop each time playback comes back to an order it already played.
  if (!loopPattern && !loopJump && (newOrder != order_ || newRow <= row_)) {
    if (visited_[newOrder]) {
      loops_++;
      visited_.assign(MAX_ORDERS, false);
    }
    visited_[newOrder] = true;
  }
  order_ = newOrder;
  row_ = newRow;
  grooveIdx_++;
}

void Engine::doTick() {
  int nch = song_.channelCount();
  if (!playing_) tickRate_ = song_.tickRate;
  if (playing_) {
    if (song_.orders.empty()) {
      playing_ = false;
    } else {
      if (order_ >= (int)song_.orders.size()) order_ = 0;
      if (tick_ == 0 && !skipRow_) processRow();
    }
  }

  for (int c = 0; c < nch; c++) {
    ChannelState& s = ch_[c];
    if (s.delayTick >= 0 && tick_ == s.delayTick) {
      s.delayTick = -1;
      Cell cell = s.delayedCell;
      processCell(c, cell, true);
    }
    if (s.cutTick >= 0 && tick_ == s.cutTick) {
      s.active = false;
      s.cutTick = -1;
    }
    if (s.retrigTicks > 0 && s.active && ++s.retrigCount >= s.retrigTicks) {
      s.retrigCount = 0;
      double pitch = s.pitch;
      triggerNote(c, s.note, 0);
      s.pitch = pitch;
    }

    // Continuous effects.
    if (s.pitchSlide) s.pitch = std::clamp(s.pitch + s.pitchSlide * PITCH_UNIT, 0.0, 131.0);
    if (s.portaTarget >= 0 && s.portaSpeed > 0) {
      double step = s.portaSpeed * PITCH_UNIT;
      if (s.pitch < s.portaTarget)
        s.pitch = std::min(s.pitch + step, s.portaTarget);
      else
        s.pitch = std::max(s.pitch - step, s.portaTarget);
    }
    // Volume slides: by default 1/64 steps like MOD/XM, on a 00-7F scale.
    if (s.volSlide && tick_ > 0) {
      int unit = c < nch ? song_.channels[c].volSlideUnit : 512;
      int v = std::clamp(s.vol * 256 + s.volFrac + s.volSlide * unit, 0, 0x7f * 256);
      s.vol = v >> 8;
      s.volFrac = v & 255;
    }
    if (s.released && s.fade > 0) {
      const Instrument* ins = instrument(c);
      if (ins && ins->fadeout > 0) {
        s.fade -= ins->fadeout;
        if (s.fade <= 0) {
          s.fade = 0;
          s.active = false;
        }
      }
    }
    if (!s.justTriggered) stepMacros(c);
    s.justTriggered = false;
    updateChannel(c);
    s.arpTick++;
    s.vibPos = (s.vibPos + s.vibSpeed) & 63;
    s.tremPos = (s.tremPos + s.tremSpeed) & 63;
  }

  if (playing_) {
    tick_++;
    if (tick_ >= speed()) {
      tick_ = 0;
      if (rowRepeat_ > 0) {
        rowRepeat_--;
        skipRow_ = true;
      } else {
        skipRow_ = false;
        advanceRow();
      }
    }
  }
}

void Engine::updateChannel(int c) {
  ChannelState& s = ch_[c];
  const Instrument* ins = instrument(c);
  int forced = c < song_.channelCount() ? song_.channels[c].forceWave : -1;

  double pitch = s.pitch;
  if (s.arpX || s.arpY) {
    int phase = s.arpTick % 3;
    pitch += phase == 1 ? s.arpX : phase == 2 ? s.arpY : 0;
  }
  if (s.vibDepth && s.vibSpeed) pitch += std::sin(s.vibPos * 2.0 * PI / 64.0) * s.vibDepth / 16.0;
  if (s.macros[MACRO_ARP].hasValue) {
    int v = s.macros[MACRO_ARP].value;
    if (v >= ARP_FIXED) pitch = (v - ARP_FIXED) + (pitch - s.pitch) + (s.pitch - s.note);
    else pitch += v;
  }
  pitch += s.pitchMacroAcc * PITCH_UNIT;
  int minNote = c < song_.channelCount() ? song_.channels[c].minNote : 0;
  pitch = std::clamp(pitch, (double)minNote, 131.0);
  s.freq = noteFreq(pitch);

  // Waveform: forced by the channel, else instrument / 10xx / wave macro.
  s.table = ins ? ins->songWave : -1;
  if (forced == WAVE_TABLE) {
    s.wave = WAVE_TABLE;
    if (s.waveOverride >= 0) s.table = s.waveOverride;
    if (s.macros[MACRO_WAVE].hasValue) s.table = s.macros[MACRO_WAVE].value;
  } else if (forced >= 0) {
    s.wave = forced;
  } else {
    s.wave = ins ? ins->wave : WAVE_PULSE;
    if (s.waveOverride >= 0) s.wave = std::min(s.waveOverride, WAVE_COUNT - 1);
    if (s.macros[MACRO_WAVE].hasValue) s.wave = std::clamp(s.macros[MACRO_WAVE].value, 0, WAVE_COUNT - 1);
  }

  s.duty = ins ? ins->duty : 2;
  if (s.dutyOverride >= 0) s.duty = s.dutyOverride;
  if (s.macros[MACRO_DUTY].hasValue) s.duty = s.macros[MACRO_DUTY].value & 3;
  if (c < song_.channelCount() && song_.channels[c].fixedDuty >= 0) s.duty = song_.channels[c].fixedDuty;

  int vol = s.vol;
  if (s.tremDepth && s.tremSpeed) vol += (int)std::lround(std::sin(s.tremPos * 2.0 * PI / 64.0) * s.tremDepth * 8);
  float lin = std::clamp(vol, 0, 0x7f) / 127.0f;
  bool hasVolMacro = s.macros[MACRO_VOL].hasValue;
  int macroVol = s.macros[MACRO_VOL].value;

  switch (s.type) {
    case INS_FM: {
      // FM volume attenuates the carriers like the Yamaha chips (0.75 dB steps).
      int outVol = hasVolMacro ? std::clamp(vol, 0, 127) * std::clamp(macroVol, 0, 127) / 127 : std::clamp(vol, 0, 127);
      s.fmAtt = (127 - outVol) * 0.75f;
      s.targetGain = s.active ? s.fade : 0.0f;
      for (int o = 0; o < 4 && ins; o++) {
        const FMOperator& op = ins->fm.ops[o];
        double mult = op.mult == 0 ? 0.5 : op.mult;
        int dt = op.dt == 7 ? 0 : op.dt - 3;
        s.fm[o].inc = s.freq * mult * std::pow(2.0, dt * 0.03 / 12.0) / sampleRate_;
      }
      if (s.active && s.released) {
        bool anyOn = false;
        for (auto& st : s.fm) anyOn |= st.stage != 4;
        if (!anyOn) s.active = false;
      }
      break;
    }
    case INS_SAMPLE: {
      float gain = lin;
      if (s.smp >= 0 && s.smp < (int)song_.samples.size()) gain *= song_.samples[s.smp].volume / 64.0f;
      if (hasVolMacro) gain *= std::clamp(macroVol, 0, 64) / 64.0f;
      s.targetGain = s.active ? gain * s.fade : 0.0f;
      if (s.smp >= 0 && s.smp < (int)song_.samples.size())
        s.smpStep = song_.samples[s.smp].rate * std::pow(2.0, (pitch - 60.0) / 12.0) / sampleRate_;
      break;
    }
    default: {
      float gain = lin;
      if (ins) gain *= ins->volume / 15.0f;
      if (hasVolMacro) gain *= std::clamp(macroVol, 0, 15) / 15.0f;
      s.targetGain = s.active ? gain * s.fade : 0.0f;
      break;
    }
  }
}

// ---------------------------------------------------------------------------
// Synthesis
// ---------------------------------------------------------------------------

float Engine::oscillate(ChannelState& s) {
  double dt = s.freq / sampleRate_;
  float out = 0;
  switch (s.wave) {
    case WAVE_PULSE: {
      double d = DUTY_TABLE[s.duty & 3];
      out = s.phase < d ? 1.0f : -1.0f;
      out += polyBlep(s.phase, dt);
      out -= polyBlep(std::fmod(s.phase + 1.0 - d, 1.0), dt);
      break;
    }
    case WAVE_TRIANGLE: {
      // 32-step triangle, like the NES.
      int step = (int)(s.phase * 32.0) & 31;
      int v = step < 16 ? step : 31 - step;
      out = v / 7.5f - 1.0f;
      break;
    }
    case WAVE_SAW:
      out = (float)(2.0 * s.phase - 1.0) - polyBlep(s.phase, dt);
      break;
    case WAVE_SINE:
      out = sinCycles(s.phase);
      break;
    case WAVE_NOISE: {
      if ((s.duty & 3) == 3) {
        // Periodic noise (SN76489 style): a thin pulse, tonal at the note pitch.
        out = s.phase < 1.0 / 16 ? 1.0f : -1.0f / 15;
        break;
      }
      // 15-bit LFSR clocked from the note frequency; duty 1 = short "metallic" mode.
      s.noisePhase += dt * 32.0;
      int clocks = 0;
      while (s.noisePhase >= 1.0 && clocks < 64) {
        s.noisePhase -= 1.0;
        int tap = (s.duty & 3) == 1 ? 6 : 1;
        uint32_t bit = (s.lfsr ^ (s.lfsr >> tap)) & 1;
        s.lfsr = (s.lfsr >> 1) | (bit << 14);
        clocks++;
      }
      if (s.noisePhase >= 1.0) s.noisePhase = std::fmod(s.noisePhase, 1.0);
      out = (s.lfsr & 1) ? 1.0f : -1.0f;
      break;
    }
    case WAVE_TABLE: {
      if (s.table >= 0 && s.table < (int)song_.wavetables.size() && !song_.wavetables[s.table].data.empty()) {
        const std::vector<float>& w = song_.wavetables[s.table].data;
        out = w[(size_t)(s.phase * w.size()) % w.size()];
      } else if (s.ins >= 0 && s.ins < (int)song_.instruments.size()) {
        int idx = (int)(s.phase * WAVETABLE_LEN) % WAVETABLE_LEN;
        out = song_.instruments[s.ins].wavetable[idx] / 7.5f - 1.0f;
      }
      break;
    }
    default:
      break;
  }
  s.phase += dt;
  if (s.phase >= 1.0) s.phase -= std::floor(s.phase);
  return out;
}

float Engine::playSample(ChannelState& s) {
  if (!s.smpPlaying || s.smp < 0 || s.smp >= (int)song_.samples.size()) return 0;
  const Sample& sm = song_.samples[s.smp];
  int len = (int)sm.data.size();
  if (len == 0) {
    s.smpPlaying = false;
    return 0;
  }
  bool loop = sm.loopMode != 0 && sm.loopEnd > sm.loopStart && sm.loopEnd <= len;
  int i = std::clamp((int)s.smpPos, 0, len - 1);
  int j = i + 1;
  if (loop && sm.loopMode == 1 && j >= sm.loopEnd) j = sm.loopStart;
  if (j >= len) j = len - 1;
  float fr = (float)(s.smpPos - std::floor(s.smpPos));
  float v = sm.data[i] + (sm.data[j] - sm.data[i]) * fr;

  s.smpPos += s.smpBackward ? -s.smpStep : s.smpStep;
  if (!loop) {
    if (s.smpPos >= len) s.smpPlaying = false;
  } else {
    double ls = sm.loopStart, le = sm.loopEnd, ll = le - ls;
    switch (sm.loopMode) {
      case 1:
        if (s.smpPos >= le) s.smpPos = ls + std::fmod(s.smpPos - ls, ll);
        break;
      case 2:
        if (!s.smpBackward && s.smpPos >= le) {
          s.smpPos = std::max(ls, le - (s.smpPos - le) - 1e-6);
          s.smpBackward = true;
        } else if (s.smpBackward && s.smpPos < ls) {
          s.smpPos = std::min(le - 1e-6, ls + (ls - s.smpPos));
          s.smpBackward = false;
        }
        break;
      default:
        if (!s.smpBackward && s.smpPos >= le) {
          s.smpBackward = true;
          s.smpPos = std::max(ls, le - (s.smpPos - le) - 1e-6);
        } else if (s.smpBackward && s.smpPos < ls) {
          s.smpPos = le - std::fmod(ls - s.smpPos, ll) - 1e-6;
        }
        break;
    }
  }
  if (s.smpPos < 0) s.smpPos = 0;
  return v;
}

void Engine::render(float* out, int frames) {
  const float smooth = 1.0f - std::exp(-1.0f / (0.002f * sampleRate_));  // ~2 ms
  const float dcR = 1.0f - 20.0f / sampleRate_;
  int nch = song_.channelCount();

  for (int i = 0; i < frames; i++) {
    if (samplesToTick_ <= 0) {
      doTick();
      samplesToTick_ += sampleRate_ / std::max(tickRate_, 1.0f);
    }
    samplesToTick_ -= 1.0;

    float l = 0, r = 0;
    for (int c = 0; c < nch; c++) {
      ChannelState& s = ch_[c];
      s.gain += (s.targetGain - s.gain) * smooth;
      float v = 0;
      if (s.gain > 1e-4f || s.active) {
        if (s.type == INS_SAMPLE) {
          v = playSample(s) * s.gain;
        } else if (s.type == INS_FM) {
          const Instrument* ins = instrument(c);
          if (ins) v = playFM(s, *ins) * s.gain;
        } else {
          v = oscillate(s) * s.gain;
        }
      }
      v *= song_.channels[c].mix;
      s.scope[scopePos_] = v;
      if (song_.channels[c].muted) continue;
      l += v * s.panL;
      r += v * s.panR;
    }
    // DC blocker, master gain and soft clip.
    float hl = l - dcPrevL_ + dcR * dcL_;
    float hr = r - dcPrevR_ + dcR * dcR_;
    dcPrevL_ = l;
    dcPrevR_ = r;
    dcL_ = hl;
    dcR_ = hr;
    l = std::tanh(hl * 0.35f);
    r = std::tanh(hr * 0.35f);
    master_[scopePos_] = (l + r) * 0.5f;
    scopePos_ = (scopePos_ + 1) % SCOPE_LEN;
    out[i * 2] = l;
    out[i * 2 + 1] = r;
  }
}

// ---------------------------------------------------------------------------
// WAV export
// ---------------------------------------------------------------------------

namespace {
void put32(std::ofstream& f, uint32_t v) {
  char b[4] = {(char)(v & 255), (char)((v >> 8) & 255), (char)((v >> 16) & 255), (char)(v >> 24)};
  f.write(b, 4);
}
void put16(std::ofstream& f, uint16_t v) {
  char b[2] = {(char)(v & 255), (char)(v >> 8)};
  f.write(b, 2);
}
}  // namespace

bool exportWav(const Song& song, const std::string& path, int sampleRate, int loops, std::string& err) {
  Engine engine(song, sampleRate);
  engine.play(0, 0);
  std::vector<int16_t> data;
  const int chunk = 512;
  float buf[chunk * 2];
  const size_t maxFrames = (size_t)sampleRate * 60 * 20;  // 20 minutes safety limit
  size_t frames = 0;
  auto append = [&]() {
    for (int i = 0; i < chunk * 2; i++) data.push_back((int16_t)std::lround(std::clamp(buf[i], -1.0f, 1.0f) * 32767));
  };
  while (engine.loops() < std::max(loops, 1) && frames < maxFrames) {
    engine.render(buf, chunk);
    append();
    frames += chunk;
  }
  // Let released notes ring out briefly.
  engine.stop();
  for (int n = 0; n < sampleRate; n += chunk) {
    engine.render(buf, chunk);
    append();
  }

  std::ofstream f(path, std::ios::binary);
  if (!f) {
    err = "Cannot open " + path + " for writing";
    return false;
  }
  uint32_t bytes = (uint32_t)(data.size() * 2);
  f.write("RIFF", 4);
  put32(f, 36 + bytes);
  f.write("WAVEfmt ", 8);
  put32(f, 16);
  put16(f, 1);  // PCM
  put16(f, 2);  // stereo
  put32(f, sampleRate);
  put32(f, sampleRate * 4);
  put16(f, 4);
  put16(f, 16);
  f.write("data", 4);
  put32(f, bytes);
  for (int16_t s : data) put16(f, (uint16_t)s);
  if (!f) {
    err = "Write error on " + path;
    return false;
  }
  return true;
}
