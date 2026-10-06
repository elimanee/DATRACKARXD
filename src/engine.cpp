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

}  // namespace

Engine::Engine(const Song& song, int sampleRate) : song_(song), sampleRate_(sampleRate) { reset(); }

void Engine::reset() {
  for (auto& s : ch_) s = ChannelState();
  playing_ = false;
  speed_ = song_.speed;
  samplesToTick_ = 0;
}

void Engine::play(int order, int row) {
  for (int c = 0; c < MAX_CHANNELS; c++) {
    std::array<float, SCOPE_LEN> scope = ch_[c].scope;
    ch_[c] = ChannelState();
    ch_[c].scope = scope;
  }
  order_ = std::clamp(order, 0, (int)song_.orders.size() - 1);
  row_ = std::clamp(row, 0, song_.patternLength - 1);
  tick_ = 0;
  speed_ = song_.speed;
  nextOrder_ = nextRow_ = -1;
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

void Engine::noteOn(int c, int note, int ins) {
  if (c < 0 || c >= MAX_CHANNELS) return;
  if (ins >= 0) ch_[c].ins = ins;
  ch_[c].portaTarget = -1;
  ch_[c].pitchSlide = 0;
  triggerNote(c, note);
  updateChannel(c);
}

void Engine::noteOff(int c) {
  if (c < 0 || c >= MAX_CHANNELS) return;
  releaseNote(c);
}

void Engine::triggerNote(int c, int note) {
  ChannelState& s = ch_[c];
  s.note = note;
  s.pitch = note;
  s.active = true;
  s.released = false;
  s.arpTick = 0;
  s.vibPos = 0;
  s.pitchMacroAcc = 0;
  s.dutyOverride = -1;
  s.waveOverride = -1;
  s.lfsr = 1;
  for (auto& m : s.macros) m = MacroState();
  stepMacros(c);
  s.justTriggered = true;
}

void Engine::releaseNote(int c) {
  ChannelState& s = ch_[c];
  if (!s.active) return;
  s.released = true;
  const Instrument* ins = instrument(c);
  // With a release point the volume macro carries on (fade out), otherwise cut.
  if (!ins || ins->macros[MACRO_VOL].release < 0) s.active = false;
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
  for (int e = 0; e < cols; e++)
    if (cell.fx[e].cmd == 0x03 && cell.fx[e].val > 0) tonePorta = true;

  if (cell.ins >= 0) s.ins = cell.ins;
  if (cell.vol >= 0) s.vol = cell.vol;

  if (cell.note == NOTE_OFF) {
    releaseNote(c);
  } else if (cell.note >= 0) {
    if (tonePorta && s.active) {
      s.portaTarget = cell.note;
      s.note = cell.note;
    } else {
      s.portaTarget = -1;
      triggerNote(c, cell.note);
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
      case 0x04: s.vibSpeed = x; s.vibDepth = y; break;
      case 0x08: s.panL = x; s.panR = y; break;
      case 0x0A: s.volSlide = x ? x : -y; break;
      case 0x0B:
        if (playing_ && !fromDelay) {
          nextOrder_ = val;
          if (nextRow_ < 0) nextRow_ = 0;
        }
        break;
      case 0x0D:
        if (playing_ && !fromDelay) {
          if (nextOrder_ < 0) nextOrder_ = order_ + 1;
          nextRow_ = val;
        }
        break;
      case 0x0F:
        if (val > 0) speed_ = val;
        break;
      case 0x10: s.waveOverride = std::min(val, WAVE_COUNT - 1); break;
      case 0x12: s.dutyOverride = val & 3; break;
      case 0xEC: s.cutTick = val; break;
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

  // Count a loop each time playback comes back to an order it already played.
  if (!loopPattern && (newOrder != order_ || newRow <= row_)) {
    if (visited_[newOrder]) {
      loops_++;
      visited_.assign(MAX_ORDERS, false);
    }
    visited_[newOrder] = true;
  }
  order_ = newOrder;
  row_ = newRow;
}

void Engine::doTick() {
  int nch = song_.channelCount();
  if (playing_) {
    if (song_.orders.empty()) {
      playing_ = false;
    } else {
      if (order_ >= (int)song_.orders.size()) order_ = 0;
      if (tick_ == 0) processRow();
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

    // Continuous effects.
    if (s.pitchSlide) s.pitch = std::clamp(s.pitch + s.pitchSlide * PITCH_UNIT, 0.0, 131.0);
    if (s.portaTarget >= 0 && s.portaSpeed > 0) {
      double step = s.portaSpeed * PITCH_UNIT;
      if (s.pitch < s.portaTarget)
        s.pitch = std::min(s.pitch + step, s.portaTarget);
      else
        s.pitch = std::max(s.pitch - step, s.portaTarget);
    }
    if (s.volSlide && tick_ > 0) s.vol = std::clamp(s.vol + s.volSlide, 0, 0x7f);
    if (!s.justTriggered) stepMacros(c);
    s.justTriggered = false;
    updateChannel(c);
    s.arpTick++;
    s.vibPos = (s.vibPos + s.vibSpeed) & 63;
  }

  if (playing_) {
    tick_++;
    if (tick_ >= speed_) {
      tick_ = 0;
      advanceRow();
    }
  }
}

void Engine::updateChannel(int c) {
  ChannelState& s = ch_[c];
  const Instrument* ins = instrument(c);

  double pitch = s.pitch;
  if (s.arpX || s.arpY) {
    int phase = s.arpTick % 3;
    pitch += phase == 1 ? s.arpX : phase == 2 ? s.arpY : 0;
  }
  if (s.vibDepth) pitch += std::sin(s.vibPos * 2.0 * PI / 64.0) * s.vibDepth / 16.0;
  if (s.macros[MACRO_ARP].hasValue) pitch += s.macros[MACRO_ARP].value;
  pitch += s.pitchMacroAcc * PITCH_UNIT;
  pitch = std::clamp(pitch, 0.0, 131.0);
  s.freq = noteFreq(pitch);

  s.wave = ins ? ins->wave : WAVE_PULSE;
  if (s.waveOverride >= 0) s.wave = s.waveOverride;
  if (s.macros[MACRO_WAVE].hasValue) s.wave = std::clamp(s.macros[MACRO_WAVE].value, 0, WAVE_COUNT - 1);

  s.duty = ins ? ins->duty : 2;
  if (s.dutyOverride >= 0) s.duty = s.dutyOverride;
  if (s.macros[MACRO_DUTY].hasValue) s.duty = s.macros[MACRO_DUTY].value & 3;

  float gain = s.vol / 127.0f;
  if (ins) gain *= ins->volume / 15.0f;
  if (s.macros[MACRO_VOL].hasValue) gain *= s.macros[MACRO_VOL].value / 15.0f;
  s.targetGain = s.active ? gain : 0.0f;
}

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
      out = (float)std::sin(s.phase * 2.0 * PI);
      break;
    case WAVE_NOISE: {
      // 15-bit LFSR clocked from the note frequency; odd duty = short "metallic" mode.
      s.noisePhase += dt * 32.0;
      int clocks = 0;
      while (s.noisePhase >= 1.0 && clocks < 64) {
        s.noisePhase -= 1.0;
        int tap = (s.duty & 1) ? 6 : 1;
        uint32_t bit = (s.lfsr ^ (s.lfsr >> tap)) & 1;
        s.lfsr = (s.lfsr >> 1) | (bit << 14);
        clocks++;
      }
      if (s.noisePhase >= 1.0) s.noisePhase = std::fmod(s.noisePhase, 1.0);
      out = (s.lfsr & 1) ? 1.0f : -1.0f;
      break;
    }
    case WAVE_TABLE: {
      if (s.ins < 0 || s.ins >= (int)song_.instruments.size()) break;
      int idx = (int)(s.phase * WAVETABLE_LEN) % WAVETABLE_LEN;
      out = song_.instruments[s.ins].wavetable[idx] / 7.5f - 1.0f;
      break;
    }
    default:
      break;
  }
  s.phase += dt;
  if (s.phase >= 1.0) s.phase -= std::floor(s.phase);
  return out;
}

void Engine::render(float* out, int frames) {
  const float smooth = 1.0f - std::exp(-1.0f / (0.002f * sampleRate_));  // ~2 ms
  const float dcR = 1.0f - 20.0f / sampleRate_;
  int nch = song_.channelCount();
  float tickRate = std::max(song_.tickRate, 1.0f);

  for (int i = 0; i < frames; i++) {
    if (samplesToTick_ <= 0) {
      doTick();
      samplesToTick_ += sampleRate_ / tickRate;
    }
    samplesToTick_ -= 1.0;

    float l = 0, r = 0;
    for (int c = 0; c < nch; c++) {
      ChannelState& s = ch_[c];
      s.gain += (s.targetGain - s.gain) * smooth;
      float v = 0;
      if (s.gain > 1e-4f || s.active) v = oscillate(s) * s.gain;
      s.scope[scopePos_] = v;
      if (song_.channels[c].muted) continue;
      l += v * s.panL / 15.0f;
      r += v * s.panR / 15.0f;
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
  while (engine.loops() < std::max(loops, 1) && frames < maxFrames) {
    engine.render(buf, chunk);
    for (int i = 0; i < chunk * 2; i++) data.push_back((int16_t)std::lround(std::clamp(buf[i], -1.0f, 1.0f) * 32767));
    frames += chunk;
  }
  // Let released notes ring out briefly.
  engine.stop();
  for (int n = 0; n < sampleRate / 2; n += chunk) {
    engine.render(buf, chunk);
    for (int i = 0; i < chunk * 2; i++) data.push_back((int16_t)std::lround(std::clamp(buf[i], -1.0f, 1.0f) * 32767));
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
