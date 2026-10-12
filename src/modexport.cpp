#include "modexport.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <fstream>
#include <map>
#include <sstream>

#include "engine.h"
#include "import.h"
#include "sampleops.h"

namespace {

// ---------------------------------------------------------------------------
// Format-neutral module
// ---------------------------------------------------------------------------

struct MSample {
  std::string name;
  std::vector<int16_t> data;
  int rate = 8363;  // at our C-5
  int loopStart = 0, loopEnd = 0;
  int loopMode = 0;  // 0 none, 1 forward, 2 ping-pong
  int volume = 64;   // 0-64
};

struct MInstrument {
  std::string name;
  std::vector<MSample> samples;
  std::vector<int> keymap = std::vector<int>(96, 0);  // XM notes C-0..B-7 (our 12..107) -> sample
  int fadeout = 0;  // XM units (0-4095), 0 = cut at note off
};

struct MFx {
  TFx type;
  int param;
};

struct MCell {
  int note = -1;  // -1 none, -2 note off, else our numbering
  int ins = -1;   // 0-based
  int vol = -1;   // 0-64
  std::vector<MFx> fx;  // most important first
};

struct MPattern {
  int rows = 64;
  std::vector<MCell> cells;  // rows * channels
};

struct Module {
  std::string name;
  int channels = 4;
  int speed = 6, tempo = 125;
  std::vector<int> orders;  // one per song order
  std::vector<MPattern> patterns;
  std::vector<MInstrument> instruments;
  std::vector<int> pan;  // per channel, 0-255
  std::map<std::string, int> dropped;  // what didn't convert, with counts
};

// ---------------------------------------------------------------------------
// Instruments
// ---------------------------------------------------------------------------

std::vector<int16_t> toPcm(const std::vector<float>& in) {
  std::vector<int16_t> out(in.size());
  for (size_t i = 0; i < in.size(); i++) out[i] = (int16_t)std::lround(std::clamp(in[i], -1.0f, 1.0f) * 32767);
  return out;
}

MSample fromSample(const Sample& s) {
  MSample m;
  m.name = s.name;
  m.data = toPcm(s.data);
  m.rate = s.rate;
  int len = (int)s.data.size();
  if (s.loopMode && s.loopEnd > s.loopStart && s.loopEnd <= len) {
    m.loopStart = s.loopStart;
    m.loopEnd = s.loopEnd;
    m.loopMode = s.loopMode == 2 ? 2 : 1;  // backward loops play forward
  }
  m.volume = std::clamp(s.volume, 0, 64);
  return m;
}

// Plays one held C-5 of the instrument and keeps it as a sample. If the note
// still sounds at the end of the hold, the sample ends with a loop over its
// last cycles (found by correlation); otherwise it is the note to its end.
MSample bake(const Song& song, int ins, int rate, float hold) {
  Song tmp;
  tmp.reset(1);
  tmp.tickRate = song.tickRate;
  tmp.speeds = song.speeds;
  tmp.instruments = song.instruments;
  tmp.samples = song.samples;
  tmp.wavetables = song.wavetables;
  tmp.channels[0].pan = 128;
  Engine e(tmp, rate);
  e.bakeMode = true;
  e.noteOn(0, 60, ins, 0x7f);
  const int chunk = 256;
  float buf[chunk * 2];
  std::vector<float> mono;
  auto run = [&](int frames) {
    for (int n = 0; n < frames; n += chunk) {
      int k = std::min(chunk, frames - n);
      e.render(buf, k);
      for (int i = 0; i < k; i++) mono.push_back((buf[i * 2] + buf[i * 2 + 1]) * 0.5f);
    }
  };
  int holdN = std::max(chunk, (int)(hold * rate));
  run(holdN);
  auto rms = [&](int from, int to) {
    double sum = 0;
    for (int i = std::max(0, from); i < to; i++) sum += mono[i] * mono[i];
    return std::sqrt(sum / std::max(1, to - from));
  };
  int tail = rate / 20;
  bool sustain = rms(holdN - tail, holdN) > 0.002;

  MSample m;
  m.name = song.instruments[ins].name;
  m.rate = rate;
  if (sustain) {
    // Loop length: the shift where the end of the note best matches itself.
    int win = std::min(1024, holdN / 4);
    int lo = std::max(16, rate / 20), hi = std::min(holdN - win - 1, rate / 5);
    int best = lo;
    double bestScore = -2;
    for (int L = lo; L <= hi; L++) {
      double ab = 0, aa = 0, bb = 0;
      for (int i = holdN - win; i < holdN; i++) {
        float a = mono[i], b = mono[i - L];
        ab += a * b;
        aa += a * a;
        bb += b * b;
      }
      double score = ab / std::sqrt(aa * bb + 1e-12);
      if (score > bestScore + 1e-4) {
        bestScore = score;
        best = L;
      }
    }
    mono.resize(holdN);
    m.loopStart = holdN - best;
    m.loopEnd = holdN;
    m.loopMode = 1;
  } else {
    // Let it ring out after a note off, then cut the silence.
    e.noteOff(0);
    for (int n = 0; n < rate * 2; n += chunk) {
      run(chunk);
      if (rms((int)mono.size() - tail, (int)mono.size()) < 1e-4) break;
    }
    size_t end = mono.size();
    while (end > 1 && std::abs(mono[end - 1]) < 1e-4f) end--;
    mono.resize(end);
  }
  // Only scale down when it would clip; the loudness stays in the data.
  float peak = 0;
  for (float v : mono) peak = std::max(peak, std::abs(v));
  if (peak > 0.99f)
    for (float& v : mono) v *= 0.99f / peak;
  m.data = toPcm(mono);
  return m;
}

// XM-style fadeout (volume lost per tick, out of 32768) from the release.
int fadeoutFor(const Instrument& ins, float tickRate) {
  if (ins.env.on && ins.env.release > 0) return std::clamp((int)std::lround(32768.0 / (ins.env.release * tickRate)), 1, 4095);
  if (ins.fadeout > 0) return std::clamp((int)std::lround(ins.fadeout * 32768.0), 1, 4095);
  return 0;
}

// ---------------------------------------------------------------------------
// Patterns
// ---------------------------------------------------------------------------

// Effects here keep running until stopped; the classic trackers need them
// on every row. This follows each channel like the engine does.
struct ChanState {
  int arp = 0, slide = 0, portaSpeed = 0, vibS = 0, vibD = 0, volSlide = 0, tremS = 0, tremD = 0, retrig = 0;
  bool porta = false;
  int vol = 127, ins = -1;
  std::string key() const {
    char b[96];
    std::snprintf(b, sizeof(b), "%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d;", arp, slide, portaSpeed, vibS, vibD, volSlide, tremS,
                  tremD, retrig, porta, vol, ins);
    return b;
  }
};

struct Converter {
  const Song& song;
  Module& m;
  double slideScale;  // our pitch units per module unit
  std::vector<int> sampleVol;  // default volume per instrument (for the volume column)

  void drop(const std::string& what) { m.dropped[what]++; }

  MCell convert(int c, const Cell* cell, ChanState& st, int row) {
    MCell out;
    std::vector<MFx> once;
    bool note = cell && cell->note >= 0;
    bool tonePorta = false;
    auto scaled = [&](int v) { return std::clamp((int)std::lround(std::abs(v) / slideScale), v ? 1 : 0, 255); };
    if (cell) {
      if (cell->note == NOTE_OFF) out.note = -2;
      else if (note) out.note = cell->note;
      if (cell->ins >= 0) {
        out.ins = cell->ins;
        st.ins = cell->ins;
      }
      int cols = std::clamp(song.channels[c].effectCols, 1, MAX_EFFECTS);
      for (int e = 0; e < cols; e++) {
        int cmd = cell->fx[e].cmd, val = std::max<int>(cell->fx[e].val, 0);
        if (cmd < 0) continue;
        int x = val >> 4, y = val & 15;
        switch (cmd) {
          case 0x00: st.arp = val; break;
          case 0x01: st.slide = val; st.porta = false; break;
          case 0x02: st.slide = -val; st.porta = false; break;
          case 0x03:
            if (val) st.portaSpeed = val;
            st.slide = 0;
            tonePorta = val > 0;
            if (!val) st.porta = false;
            break;
          case 0x04:
            if (x) st.vibS = x;
            st.vibD = y;
            if (!x && !y) st.vibS = 0;
            break;
          case 0x05:
          case 0x0A: st.volSlide = x ? x : -y; break;
          case 0x06:
            st.volSlide = x ? x : -y;
            tonePorta = true;
            break;
          case 0x07:
            if (x) st.tremS = x;
            st.tremD = y;
            break;
          case 0x08: once.push_back({TFx::Pan, std::clamp(128 + (y - x) * 9, 0, 255)}); break;
          case 0x80: once.push_back({TFx::Pan, val}); break;
          case 0x0B: once.push_back({TFx::Jump, val}); break;
          case 0x0C: st.retrig = val; break;
          case 0x0D: once.push_back({TFx::Break, val}); break;
          case 0x0F:
            if (val) once.push_back({TFx::Speed, val});
            break;
          case 0xF0:
            if (val) once.push_back({TFx::Tempo, std::clamp((int)std::lround(val), 32, 255)});
            break;
          case 0xC0: case 0xC1: case 0xC2: case 0xC3: {
            int hz = ((cmd & 3) << 8) | val;
            if (hz > 0) once.push_back({TFx::Tempo, std::clamp((int)std::lround(hz * 2.5), 32, 255)});
            break;
          }
          case 0x90: break;  // below 256 samples: too fine for the module's offset
          case 0x91:
            if (st.ins >= 0 && st.ins < (int)song.instruments.size() && song.instruments[st.ins].type == INS_SAMPLE)
              once.push_back({TFx::Offset, val});
            else
              drop("9xx sample offset on a rendered instrument");
            break;
          case 0xE6: once.push_back({TFx::Loop, std::min(val, 15)}); break;
          case 0xEC: once.push_back({TFx::Cut, std::min(val, 15)}); break;
          case 0xED: once.push_back({TFx::Delay, std::min(val, 15)}); break;
          case 0xEE: once.push_back({TFx::PatternDelay, std::min(val, 15)}); break;
          case 0xF1: once.push_back({TFx::FinePortaUp, std::min(scaled(val), 15)}); break;
          case 0xF2: once.push_back({TFx::FinePortaDown, std::min(scaled(val), 15)}); break;
          case 0xF3: once.push_back({TFx::FineVolUp, std::min(val / 2, 15)}); break;
          case 0xF4: once.push_back({TFx::FineVolDown, std::min(val / 2, 15)}); break;
          default: {
            char name[16];
            std::snprintf(name, sizeof(name), "%02Xxx", cmd);
            drop(std::string(name) + " effect");
            break;
          }
        }
      }
      if (note) st.porta = tonePorta;  // a note without tone portamento starts fresh
      if (cell->vol >= 0) st.vol = cell->vol;
    }
    // Volume column: the module's note-with-instrument resets the volume, so
    // the channel's volume is written again when it isn't full.
    int smpVol = st.ins >= 0 && st.ins < (int)sampleVol.size() ? sampleVol[st.ins] : 64;
    if ((cell && cell->vol >= 0) || (out.note >= 0 && out.ins >= 0 && st.vol != 127))
      out.vol = std::clamp((int)std::lround(st.vol * smpVol / 127.0), 0, 64);

    // Groove: the speed of each row.
    if (c == 0 && song.speeds.size() > 1) once.insert(once.begin(), {TFx::Speed, song.speeds[row % song.speeds.size()]});

    out.fx = once;
    int vs = st.volSlide > 0 ? st.volSlide << 4 : -st.volSlide;
    if (st.porta && st.volSlide) out.fx.push_back({TFx::PortaVolSlide, vs});
    else if (st.porta) out.fx.push_back({TFx::TonePorta, std::clamp((int)std::lround(st.portaSpeed / slideScale), 1, 255)});
    else if ((st.vibS || st.vibD) && st.volSlide) out.fx.push_back({TFx::VibVolSlide, vs});
    else if (st.vibS || st.vibD) out.fx.push_back({TFx::Vibrato, (st.vibS << 4) | st.vibD});
    if (st.slide) out.fx.push_back({st.slide > 0 ? TFx::PortaUp : TFx::PortaDown, scaled(st.slide)});
    if (st.arp) out.fx.push_back({TFx::Arp, st.arp});
    if (st.volSlide && !st.porta && !(st.vibS || st.vibD)) out.fx.push_back({TFx::VolSlide, vs});
    if (st.tremS || st.tremD) out.fx.push_back({TFx::Tremolo, (st.tremS << 4) | st.tremD});
    if (st.retrig) out.fx.push_back({TFx::Retrig, std::min(st.retrig, 15)});
    return out;
  }

  void build() {
    int nch = song.channelCount();
    m.channels = nch;
    m.pan.resize(nch);
    for (int c = 0; c < nch; c++) m.pan[c] = song.channels[c].pan;
    std::vector<ChanState> state(nch);
    struct Cached {
      int index;
      std::vector<ChanState> exit;
    };
    std::map<std::string, Cached> cache;
    int rows = std::clamp(song.patternLength, 1, MAX_ROWS);
    for (int o = 0; o < (int)song.orders.size(); o++) {
      std::string key;
      for (int c = 0; c < nch; c++) key += std::to_string(song.orders[o][c]) + ":" + state[c].key();
      auto it = cache.find(key);
      if (it != cache.end()) {
        m.orders.push_back(it->second.index);
        state = it->second.exit;
        continue;
      }
      MPattern pat;
      pat.rows = rows;
      pat.cells.resize(rows * nch);
      for (int r = 0; r < rows; r++)
        for (int c = 0; c < nch; c++) pat.cells[r * nch + c] = convert(c, song.cell(c, o, r), state[c], r);
      int index = (int)m.patterns.size();
      m.patterns.push_back(std::move(pat));
      m.orders.push_back(index);
      cache[key] = {index, state};
    }
  }
};

Module buildModule(const Song& song, const ModuleExportOptions& opt) {
  Module m;
  m.name = song.name;
  m.speed = std::clamp(song.speeds.empty() ? 6 : song.speeds[0], 1, 31);
  m.tempo = std::clamp((int)std::lround(song.tickRate * 2.5f), 32, 255);
  if (std::abs(song.tickRate * 2.5f - m.tempo) > 1) m.dropped["tick rate outside 12.8-102 Hz (tempo clamped)"]++;
  // Song-wide processing the formats have no place for.
  if (song.fx.reverb || song.fx.delay || song.fx.filter) m.dropped["master reverb / delay / filter (not exported)"]++;
  if (std::any_of(song.automation.begin(), song.automation.end(), [](const AutomationLane& l) { return !l.points.empty(); }))
    m.dropped["automation curves (not exported)"]++;
  if (song.subsongCount() > 1) m.dropped["other subsongs (only the current one is exported)"]++;
  for (const ChannelInfo& ch : song.channels)
    if (!ch.effects.empty()) {
      m.dropped["channel effects (not exported)"]++;
      break;
    }

  // Which instruments the song plays.
  std::vector<bool> used(song.instruments.size(), false);
  for (int c = 0; c < song.channelCount(); c++)
    for (auto& o : song.orders) {
      const Pattern* p = song.pattern(c, o[c]);
      if (!p) continue;
      for (int r = 0; r < song.patternLength; r++)
        if (p->rows[r].ins >= 0 && p->rows[r].ins < (int)used.size()) used[p->rows[r].ins] = true;
    }
  bool anyUsed = std::find(used.begin(), used.end(), true) != used.end();
  if (!anyUsed && !used.empty()) used[0] = true;  // notes without an instrument play the first

  std::vector<int> sampleVol(song.instruments.size(), 64);
  for (size_t i = 0; i < song.instruments.size(); i++) {
    const Instrument& ins = song.instruments[i];
    MInstrument mi;
    mi.name = ins.name;
    if (!used[i]) {
      m.instruments.push_back(mi);
      continue;
    }
    if (ins.type == INS_SAMPLE) {
      std::vector<int> list;  // song sample indexes, in instrument order
      auto slot = [&](int s) {
        if (s < 0 || s >= (int)song.samples.size() || song.samples[s].data.empty()) return -1;
        auto f = std::find(list.begin(), list.end(), s);
        if (f != list.end()) return (int)(f - list.begin());
        if (list.size() >= 16) return 0;
        list.push_back(s);
        return (int)list.size() - 1;
      };
      int def = slot(ins.sample);
      for (int k = 0; k < 96; k++) {
        int note = k + 12, s = -1;
        if (!ins.sampleMap.empty() && note < NOTE_COUNT && ins.sampleMap[note] >= 0) s = slot(ins.sampleMap[note]);
        mi.keymap[k] = std::max(0, s >= 0 ? s : def);
      }
      for (int s : list) mi.samples.push_back(fromSample(song.samples[s]));
      if (!mi.samples.empty()) sampleVol[i] = mi.samples[std::max(def, 0)].volume;
    } else {
      mi.samples.push_back(bake(song, (int)i, opt.bakeRate, opt.holdSeconds));
    }
    mi.fadeout = fadeoutFor(ins, song.tickRate);
    m.instruments.push_back(std::move(mi));
  }

  double scale = opt.format == ModuleFormat::MOD ? 1.3 : 2.0;
  Converter conv{song, m, scale, sampleVol};
  conv.build();
  return m;
}

// ---------------------------------------------------------------------------
// Writers
// ---------------------------------------------------------------------------

struct Out {
  std::vector<uint8_t> b;
  void u8(int v) { b.push_back((uint8_t)v); }
  void u16(int v) {
    u8(v & 255);
    u8((v >> 8) & 255);
  }
  void u32(uint32_t v) {
    u16((int)(v & 0xffff));
    u16((int)(v >> 16));
  }
  void u16be(int v) {
    u8((v >> 8) & 255);
    u8(v & 255);
  }
  void str(const std::string& s, size_t n, char pad = 0) {
    for (size_t i = 0; i < n; i++) u8(i < s.size() ? (uint8_t)s[i] : (uint8_t)pad);
  }
  void put32At(size_t at, uint32_t v) {
    for (int i = 0; i < 4; i++) b[at + i] = (uint8_t)(v >> (8 * i));
  }
  void put16At(size_t at, int v) {
    b[at] = (uint8_t)(v & 255);
    b[at + 1] = (uint8_t)((v >> 8) & 255);
  }
};

// Shared by MOD and XM: classic effect number and parameter (E-effects as 0xE).
bool classicEffect(const MFx& f, int& cmd, int& prm, bool bcdBreak) {
  int p = std::clamp(f.param, 0, 255);
  switch (f.type) {
    case TFx::Arp: cmd = 0x0; prm = p; return true;
    case TFx::PortaUp: cmd = 0x1; prm = p; return true;
    case TFx::PortaDown: cmd = 0x2; prm = p; return true;
    case TFx::TonePorta: cmd = 0x3; prm = p; return true;
    case TFx::Vibrato: cmd = 0x4; prm = p; return true;
    case TFx::PortaVolSlide: cmd = 0x5; prm = p; return true;
    case TFx::VibVolSlide: cmd = 0x6; prm = p; return true;
    case TFx::Tremolo: cmd = 0x7; prm = p; return true;
    case TFx::Pan: cmd = 0x8; prm = p; return true;
    case TFx::Offset: cmd = 0x9; prm = p; return true;
    case TFx::VolSlide: cmd = 0xA; prm = p; return true;
    case TFx::Jump: cmd = 0xB; prm = p; return true;
    case TFx::Break: cmd = 0xD; prm = bcdBreak ? ((p / 10) << 4) | (p % 10) : p; return true;
    case TFx::Speed: cmd = 0xF; prm = std::clamp(p, 1, 31); return true;
    case TFx::Tempo: cmd = 0xF; prm = std::clamp(p, 32, 255); return true;
    case TFx::FinePortaUp: cmd = 0xE; prm = 0x10 | std::min(p, 15); return true;
    case TFx::FinePortaDown: cmd = 0xE; prm = 0x20 | std::min(p, 15); return true;
    case TFx::Loop: cmd = 0xE; prm = 0x60 | std::min(p, 15); return true;
    case TFx::Retrig: cmd = 0xE; prm = 0x90 | std::min(p, 15); return true;
    case TFx::FineVolUp: cmd = 0xE; prm = 0xA0 | std::min(p, 15); return true;
    case TFx::FineVolDown: cmd = 0xE; prm = 0xB0 | std::min(p, 15); return true;
    case TFx::Cut: cmd = 0xE; prm = 0xC0 | std::min(p, 15); return true;
    case TFx::Delay: cmd = 0xE; prm = 0xD0 | std::min(p, 15); return true;
    case TFx::PatternDelay: cmd = 0xE; prm = 0xE0 | std::min(p, 15); return true;
    default: return false;
  }
}

// Notes beyond the format's range move by octaves.
int fitNote(int note, int lo, int hi, Module& m) {
  if (note >= lo && note <= hi) return note;
  m.dropped["notes moved by an octave to fit the format's range"]++;
  while (note < lo) note += 12;
  while (note > hi) note -= 12;
  return note;
}

// Which effect goes in the effect column and which in the volume column
// (when it is free): an effect only the effect column can hold goes there,
// leaving the volume column to one it can take.
bool volColumnCapable(TFx t) { return t == TFx::VolSlide || t == TFx::Vibrato || t == TFx::TonePorta || t == TFx::Pan; }

template <typename Convertible>
void splitEffects(const std::vector<MFx>& fx, bool volFree, Convertible ok, int& fxIdx, int& volIdx) {
  fxIdx = volIdx = -1;
  if (volFree) {
    for (size_t k = 0; k < fx.size(); k++)
      if (ok(fx[k]) && !volColumnCapable(fx[k].type)) {
        fxIdx = (int)k;
        break;
      }
  }
  if (fxIdx < 0)
    for (size_t k = 0; k < fx.size(); k++)
      if (ok(fx[k])) {
        fxIdx = (int)k;
        break;
      }
  if (volFree)
    for (size_t k = 0; k < fx.size(); k++)
      if ((int)k != fxIdx && volColumnCapable(fx[k].type)) {
        volIdx = (int)k;
        break;
      }
}

// --- XM ---

std::vector<uint8_t> writeXM(Module& m) {
  Out o;
  o.str("Extended Module: ", 17);
  o.str(m.name, 20);
  o.u8(0x1a);
  o.str("DATRACKARXD", 20);
  o.u16(0x0104);
  o.u32(276);
  int nOrders = std::min((int)m.orders.size(), 256);
  o.u16(nOrders);
  o.u16(0);  // restart position
  o.u16(m.channels);
  o.u16((int)m.patterns.size());
  int nIns = std::min((int)m.instruments.size(), 128);
  if (m.instruments.size() > 128) m.dropped["instruments beyond 128"] += (int)m.instruments.size() - 128;
  o.u16(nIns);
  o.u16(1);  // linear frequencies
  o.u16(m.speed);
  o.u16(m.tempo);
  for (int i = 0; i < 256; i++) o.u8(i < nOrders ? m.orders[i] : 0);

  for (size_t pi = 0; pi < m.patterns.size(); pi++) {
    MPattern& pat = m.patterns[pi];
    o.u32(9);
    o.u8(0);
    o.u16(pat.rows);
    size_t sizeAt = o.b.size();
    o.u16(0);
    size_t start = o.b.size();
    for (int r = 0; r < pat.rows; r++) {
      for (int c = 0; c < m.channels; c++) {
        const MCell& cell = pat.cells[r * m.channels + c];
        int note = 0, ins = 0, vol = 0, cmd = 0, prm = 0;
        if (cell.note == -2) note = 97;
        else if (cell.note >= 0) note = fitNote(cell.note, 12, 107, m) - 11;
        if (cell.ins >= 0 && cell.ins < 128) ins = cell.ins + 1;
        if (cell.vol >= 0) vol = 0x10 + std::clamp(cell.vol, 0, 64);
        int fxIdx, volIdx;
        splitEffects(cell.fx, vol == 0, [](const MFx& f) { int a, b; return classicEffect(f, a, b, true); }, fxIdx, volIdx);
        if (fxIdx >= 0) classicEffect(cell.fx[fxIdx], cmd, prm, true);
        if (volIdx >= 0) {
          const MFx& f = cell.fx[volIdx];
          int p = std::clamp(f.param, 0, 255);
          if (f.type == TFx::VolSlide) vol = (p >> 4) ? 0x70 | std::min(p >> 4, 15) : 0x60 | std::min(p & 15, 15);
          else if (f.type == TFx::Vibrato) vol = 0xB0 | (p & 15);
          else if (f.type == TFx::TonePorta) vol = 0xF0 | std::clamp(p / 16, 1, 15);
          else vol = 0xC0 | (p >> 4);
        }
        int placed = (fxIdx >= 0) + (volIdx >= 0);
        if ((int)cell.fx.size() > placed) m.dropped["effects that didn't fit in the cell"] += (int)cell.fx.size() - placed;
        int mask = (note ? 1 : 0) | (ins ? 2 : 0) | (vol ? 4 : 0) | (cmd ? 8 : 0) | (prm ? 16 : 0);
        if (mask == 31) {
          o.u8(note);
          o.u8(ins);
          o.u8(vol);
          o.u8(cmd);
          o.u8(prm);
        } else {
          o.u8(0x80 | mask);
          if (note) o.u8(note);
          if (ins) o.u8(ins);
          if (vol) o.u8(vol);
          if (cmd) o.u8(cmd);
          if (prm) o.u8(prm);
        }
      }
    }
    o.put16At(sizeAt, (int)(o.b.size() - start));
  }

  for (int i = 0; i < nIns; i++) {
    const MInstrument& in = m.instruments[i];
    size_t hdr = o.b.size();
    int ns = std::min((int)in.samples.size(), 16);
    o.u32(ns ? 263 : 29);
    o.str(in.name, 22);
    o.u8(0);
    o.u16(ns);
    if (!ns) continue;
    o.u32(40);
    for (int k = 0; k < 96; k++) o.u8(std::clamp(in.keymap[k], 0, ns - 1));
    // A flat volume envelope with a sustain point, so the fadeout works.
    bool env = in.fadeout > 0;
    for (int k = 0; k < 12; k++) {
      o.u16(env && k == 1 ? 10 : 0);
      o.u16(env && k < 2 ? 64 : 0);
    }
    for (int k = 0; k < 12; k++) o.u32(0);  // panning envelope
    o.u8(env ? 2 : 0);
    o.u8(0);
    o.u8(0);  // volume sustain point
    o.u8(0);
    o.u8(0);
    o.u8(0);
    o.u8(0);
    o.u8(0);
    o.u8(env ? 3 : 0);  // on + sustain
    o.u8(0);
    o.u8(0);
    o.u8(0);
    o.u8(0);
    o.u8(0);
    o.u16(in.fadeout);
    while (o.b.size() < hdr + 263) o.u8(0);
    for (int s = 0; s < ns; s++) {
      const MSample& sm = in.samples[s];
      double semis = 12 * std::log2(std::max(sm.rate, 1) / 8363.0);
      int rel = (int)std::floor(semis);
      int fine = (int)std::lround((semis - rel) * 128);
      if (fine >= 128) {
        rel++;
        fine -= 128;
      }
      if (fine > 127) fine = 127;
      if (rel > 127 || rel < -128) m.dropped["sample rates outside the XM range"]++;
      o.u32((uint32_t)sm.data.size() * 2);
      o.u32((uint32_t)sm.loopStart * 2);
      o.u32((uint32_t)(sm.loopMode ? sm.loopEnd - sm.loopStart : 0) * 2);
      o.u8(sm.volume);
      o.u8((uint8_t)(int8_t)fine);
      o.u8((sm.loopMode == 2 ? 2 : sm.loopMode ? 1 : 0) | 0x10);
      o.u8(128);
      o.u8((uint8_t)(int8_t)std::clamp(rel, -128, 127));
      o.u8(0);
      o.str(sm.name, 22);
    }
    for (int s = 0; s < ns; s++) {
      int16_t prev = 0;
      for (int16_t v : in.samples[s].data) {
        o.u16((uint16_t)(int16_t)(v - prev));
        prev = v;
      }
    }
  }
  return o.b;
}

// --- MOD ---

const int MOD_PERIODS[36] = {856, 808, 762, 720, 678, 640, 604, 570, 538, 508, 480, 453, 428, 404, 381, 360, 339, 320,
                             302, 285, 269, 254, 240, 226, 214, 202, 190, 180, 170, 160, 151, 143, 135, 127, 120, 113};

std::vector<uint8_t> writeMOD(Module& m) {
  Out o;
  o.str(m.name, 20);
  // One sample per instrument, at 8363 Hz (the MOD's C), 8-bit.
  int nSmp = std::min((int)m.instruments.size(), 31);
  if (m.instruments.size() > 31) m.dropped["instruments beyond 31"] += (int)m.instruments.size() - 31;
  std::vector<std::vector<int8_t>> data(31);
  std::vector<int> loopS(31, 0), loopL(31, 0), vols(31, 0);
  for (int i = 0; i < nSmp; i++) {
    const MInstrument& in = m.instruments[i];
    if (in.samples.empty()) continue;
    if (in.samples.size() > 1) m.dropped["multi-sample maps (MOD keeps one sample)"]++;
    const MSample& sm = in.samples[0];
    Sample s;
    s.data.resize(sm.data.size());
    for (size_t k = 0; k < sm.data.size(); k++) s.data[k] = sm.data[k] / 32768.0f;
    s.rate = sm.rate;
    s.loopStart = sm.loopStart;
    s.loopEnd = sm.loopEnd;
    s.loopMode = sm.loopMode;
    sampleops::resample(s, 8363);
    size_t len = std::min<size_t>(s.data.size() & ~(size_t)1, 131070);
    if (s.data.size() > 131070) m.dropped["samples cut at 128 KB"]++;
    data[i].resize(len);
    for (size_t k = 0; k < len; k++) data[i][k] = (int8_t)std::clamp((int)std::lround(s.data[k] * 127), -128, 127);
    if (sm.loopMode && s.loopEnd > s.loopStart) {
      loopS[i] = std::min<int>(s.loopStart & ~1, (int)len);
      loopL[i] = std::min<int>((s.loopEnd - s.loopStart) & ~1, (int)len - loopS[i]);
    }
    vols[i] = sm.volume;
  }
  for (int i = 0; i < 31; i++) {
    o.str(i < nSmp ? m.instruments[i].name : "", 22);
    o.u16be((int)data[i].size() / 2);
    o.u8(0);  // finetune
    o.u8(vols[i]);
    o.u16be(loopS[i] / 2);
    o.u16be(loopL[i] > 2 ? loopL[i] / 2 : 1);
  }

  // Patterns of 64 rows: longer ones are split, shorter ones end with a break.
  std::vector<std::vector<uint8_t>> pats;
  std::vector<int> firstPat(m.patterns.size());
  for (size_t pi = 0; pi < m.patterns.size(); pi++) {
    MPattern& pat = m.patterns[pi];
    firstPat[pi] = (int)pats.size();
    for (int base = 0; base < pat.rows; base += 64) {
      std::vector<uint8_t> p(64 * m.channels * 4, 0);
      int used = std::min(64, pat.rows - base);
      for (int r = 0; r < used; r++) {
        bool last = r == used - 1 && used < 64;
        bool hasBreak = false;
        for (int c = 0; c < m.channels; c++) {
          MCell cell = pat.cells[(base + r) * m.channels + c];
          int period = 0, smp = 0, cmd = 0, prm = 0;
          if (cell.note >= 0) period = MOD_PERIODS[fitNote(cell.note, 48, 83, m) - 48];
          if (cell.ins >= 0 && cell.ins < 31) smp = cell.ins + 1;
          std::vector<MFx> fx = cell.fx;
          if (cell.note == -2) fx.insert(fx.begin(), {TFx::Cut, 0});
          bool placed = false;
          for (const MFx& f : fx) {
            // No volume column: a volume beats an effect that runs on its
            // own (it carries on from the rows around), not a one-shot.
            bool runs = f.type == TFx::Arp || f.type == TFx::PortaUp || f.type == TFx::PortaDown || f.type == TFx::TonePorta ||
                        f.type == TFx::Vibrato || f.type == TFx::PortaVolSlide || f.type == TFx::VibVolSlide ||
                        f.type == TFx::VolSlide || f.type == TFx::Tremolo || f.type == TFx::Retrig;
            if (cell.vol >= 0 && runs) break;
            if (classicEffect(f, cmd, prm, true)) {
              placed = true;
              break;
            }
          }
          if ((int)fx.size() > (placed ? 1 : 0)) m.dropped["effects that didn't fit in the cell"] += (int)fx.size() - (placed ? 1 : 0);
          // The volume goes in Cxx when the effect slot is free.
          if (cell.vol >= 0) {
            if (!placed) {
              cmd = 0xC;
              prm = cell.vol;
              placed = true;
            } else {
              m.dropped["volumes that didn't fit (MOD has no volume column)"]++;
            }
          }
          if (!placed) cmd = prm = 0;
          if (cmd == 0xD || cmd == 0xB) hasBreak = true;
          uint8_t* d = &p[(r * m.channels + c) * 4];
          d[0] = (uint8_t)((smp & 0xF0) | ((period >> 8) & 15));
          d[1] = (uint8_t)(period & 255);
          d[2] = (uint8_t)(((smp & 15) << 4) | (cmd & 15));
          d[3] = (uint8_t)prm;
        }
        if (last && !hasBreak) {
          // End the short pattern with D00 on the first free channel.
          int c = 0;
          for (; c < m.channels; c++)
            if ((p[(r * m.channels + c) * 4 + 2] & 15) == 0 && p[(r * m.channels + c) * 4 + 3] == 0) break;
          c = std::min(c, m.channels - 1);
          p[(r * m.channels + c) * 4 + 2] = (uint8_t)((p[(r * m.channels + c) * 4 + 2] & 0xF0) | 0xD);
          p[(r * m.channels + c) * 4 + 3] = 0;
        }
      }
      pats.push_back(std::move(p));
    }
  }
  std::vector<int> orders;
  for (int pi : m.orders) {
    int n = (m.patterns[pi].rows + 63) / 64;
    for (int k = 0; k < n; k++) orders.push_back(firstPat[pi] + k);
  }
  if (orders.size() > 128) {
    m.dropped["orders beyond 128"] += (int)orders.size() - 128;
    orders.resize(128);
  }
  if (pats.size() > 100) m.dropped["more than 100 patterns (some players stop at 64)"]++;
  o.u8((int)orders.size());
  o.u8(127);
  for (int i = 0; i < 128; i++) o.u8(i < (int)orders.size() ? orders[i] : 0);
  // The channel count tag.
  std::string tag = m.channels == 4 ? "M.K." : m.channels < 10 ? std::to_string(m.channels) + "CHN" : std::to_string(m.channels) + "CH";
  o.str(tag, 4);
  // Speed and tempo on the first row (MOD has no header for them): in a
  // free effect slot, else in place of a volume (Cxx).
  if (!pats.empty()) {
    std::vector<uint8_t>& first = pats[orders.empty() ? 0 : orders[0]];
    std::vector<bool> taken(m.channels, false);
    for (int want : {m.speed, m.tempo}) {
      // A free slot, else a volume, else any effect but a jump or speed.
      int slot = -1;
      for (int pass = 0; pass < 3 && slot < 0; pass++)
        for (int c = 0; c < m.channels && slot < 0; c++) {
          uint8_t* d = &first[c * 4];
          int cmd = d[2] & 15;
          bool freeSlot = cmd == 0 && d[3] == 0;
          bool ok = pass == 0 ? freeSlot : pass == 1 ? cmd == 0xC : (cmd != 0xB && cmd != 0xD && cmd != 0xF);
          if (!taken[c] && ok) slot = c;
        }
      if (slot >= 0 && (first[slot * 4 + 2] & 15) != 0xC && !((first[slot * 4 + 2] & 15) == 0 && first[slot * 4 + 3] == 0))
        m.dropped["effects that didn't fit in the cell"]++;
      if (slot < 0) {
        m.dropped["speed or tempo (no room on the first row)"]++;
        continue;
      }
      if ((first[slot * 4 + 2] & 15) == 0xC) m.dropped["volumes that didn't fit (MOD has no volume column)"]++;
      taken[slot] = true;
      first[slot * 4 + 2] = (uint8_t)((first[slot * 4 + 2] & 0xF0) | 0xF);
      first[slot * 4 + 3] = (uint8_t)want;
    }
  }
  for (auto& p : pats) o.b.insert(o.b.end(), p.begin(), p.end());
  for (auto& d : data)
    for (int8_t v : d) o.u8((uint8_t)v);
  return o.b;
}

// --- IT ---

bool itEffect(const MFx& f, int& cmd, int& prm) {
  int p = std::clamp(f.param, 0, 255);
  auto letter = [](char ch) { return ch - 'A' + 1; };
  switch (f.type) {
    case TFx::Speed: cmd = letter('A'); prm = p; return true;
    case TFx::Jump: cmd = letter('B'); prm = p; return true;
    case TFx::Break: cmd = letter('C'); prm = p; return true;
    case TFx::VolSlide: cmd = letter('D'); prm = p; return true;
    case TFx::PortaDown: cmd = letter('E'); prm = std::min(p, 0xDF); return true;
    case TFx::PortaUp: cmd = letter('F'); prm = std::min(p, 0xDF); return true;
    case TFx::TonePorta: cmd = letter('G'); prm = p; return true;
    case TFx::Vibrato: cmd = letter('H'); prm = p; return true;
    case TFx::Arp: cmd = letter('J'); prm = p; return true;
    case TFx::VibVolSlide: cmd = letter('K'); prm = p; return true;
    case TFx::PortaVolSlide: cmd = letter('L'); prm = p; return true;
    case TFx::Offset: cmd = letter('O'); prm = p; return true;
    case TFx::Retrig: cmd = letter('Q'); prm = std::min(p, 15); return true;
    case TFx::Tremolo: cmd = letter('R'); prm = p; return true;
    case TFx::Loop: cmd = letter('S'); prm = 0xB0 | std::min(p, 15); return true;
    case TFx::Cut: cmd = letter('S'); prm = 0xC0 | std::min(p, 15); return true;
    case TFx::Delay: cmd = letter('S'); prm = 0xD0 | std::min(p, 15); return true;
    case TFx::PatternDelay: cmd = letter('S'); prm = 0xE0 | std::min(p, 15); return true;
    case TFx::Tempo: cmd = letter('T'); prm = std::max(p, 32); return true;
    case TFx::Pan: cmd = letter('X'); prm = p; return true;
    case TFx::FinePortaUp: cmd = letter('F'); prm = 0xF0 | std::min(p, 15); return true;
    case TFx::FinePortaDown: cmd = letter('E'); prm = 0xF0 | std::min(p, 15); return true;
    case TFx::FineVolUp: cmd = letter('D'); prm = (std::min(p, 14) << 4) | 0xF; return true;
    case TFx::FineVolDown: cmd = letter('D'); prm = 0xF0 | std::min(p, 14); return true;
    default: return false;
  }
}

std::vector<uint8_t> writeIT(Module& m) {
  // Sample mode: one sample per instrument.
  int nSmp = std::min((int)m.instruments.size(), 99);
  if (m.instruments.size() > 99) m.dropped["instruments beyond 99"] += (int)m.instruments.size() - 99;
  // Patterns of up to 200 rows: longer ones are split.
  std::vector<std::pair<int, int>> chunks;  // (pattern, first row)
  std::vector<int> firstChunk(m.patterns.size());
  for (size_t pi = 0; pi < m.patterns.size(); pi++) {
    firstChunk[pi] = (int)chunks.size();
    for (int base = 0; base < m.patterns[pi].rows; base += 200) chunks.push_back({(int)pi, base});
  }
  std::vector<int> orders;
  for (int pi : m.orders) {
    int n = (m.patterns[pi].rows + 199) / 200;
    for (int k = 0; k < n; k++) orders.push_back(firstChunk[pi] + k);
  }
  if (orders.size() > 255) {
    m.dropped["orders beyond 255"] += (int)orders.size() - 255;
    orders.resize(255);
  }
  if (m.channels > 64) m.dropped["channels beyond 64"]++;

  Out o;
  o.str("IMPM", 4);
  o.str(m.name, 26);
  o.u16(0x1004);  // highlight
  o.u16((int)orders.size() + 1);
  o.u16(0);  // instruments
  o.u16(nSmp);
  o.u16((int)chunks.size());
  o.u16(0x0214);
  o.u16(0x0214);
  o.u16(1 | 8);  // stereo, linear slides
  o.u16(0);
  o.u8(128);  // global volume
  o.u8(48);   // mix volume
  o.u8(m.speed);
  o.u8(m.tempo);
  o.u8(128);  // separation
  o.u8(0);
  o.u16(0);
  o.u32(0);
  o.u32(0);
  for (int c = 0; c < 64; c++) o.u8(c < m.channels ? std::clamp((int)std::lround(m.pan[c] * 64 / 255.0), 0, 64) : 128 | 32);
  for (int c = 0; c < 64; c++) o.u8(64);
  for (int ord : orders) o.u8(ord);
  o.u8(255);
  size_t smpOffAt = o.b.size();
  for (int i = 0; i < nSmp; i++) o.u32(0);
  size_t patOffAt = o.b.size();
  for (size_t i = 0; i < chunks.size(); i++) o.u32(0);

  // Sample headers, then the data at the end.
  std::vector<size_t> dataPtrAt(nSmp, 0);
  for (int i = 0; i < nSmp; i++) {
    o.put32At(smpOffAt + i * 4, (uint32_t)o.b.size());
    const MInstrument& in = m.instruments[i];
    const MSample* sm = in.samples.empty() ? nullptr : &in.samples[0];
    if (in.samples.size() > 1) m.dropped["multi-sample maps (IT export keeps one sample)"]++;
    o.str("IMPS", 4);
    o.str("", 12);
    o.u8(0);
    o.u8(64);  // global volume
    int flags = sm && !sm->data.empty() ? 1 | 2 : 0;
    if (sm && sm->loopMode) flags |= 16 | (sm->loopMode == 2 ? 64 : 0);
    o.u8(flags);
    o.u8(sm ? sm->volume : 0);
    o.str(in.name, 26);
    o.u8(1);  // signed samples
    o.u8(0);
    o.u32(sm ? (uint32_t)sm->data.size() : 0);
    o.u32(sm ? (uint32_t)sm->loopStart : 0);
    o.u32(sm ? (uint32_t)sm->loopEnd : 0);
    o.u32(sm ? (uint32_t)sm->rate : 8363);
    o.u32(0);
    o.u32(0);
    dataPtrAt[i] = o.b.size();
    o.u32(0);
    o.u32(0);  // vibrato
  }

  for (size_t ci = 0; ci < chunks.size(); ci++) {
    o.put32At(patOffAt + ci * 4, (uint32_t)o.b.size());
    const MPattern& pat = m.patterns[chunks[ci].first];
    int base = chunks[ci].second, rows = std::min(200, pat.rows - base);
    size_t lenAt = o.b.size();
    o.u16(0);
    o.u16(rows);
    o.u32(0);
    size_t start = o.b.size();
    for (int r = 0; r < rows; r++) {
      for (int c = 0; c < std::min(m.channels, 64); c++) {
        const MCell& cell = pat.cells[(base + r) * m.channels + c];
        int note = -1, ins = 0, vol = -1, cmd = 0, prm = 0;
        // Note cut: in sample mode a note off would let a looped sample ring on.
        if (cell.note == -2) note = 254;
        else if (cell.note >= 0) note = cell.note;
        if (cell.ins >= 0 && cell.ins < 99) ins = cell.ins + 1;
        if (cell.vol >= 0) vol = std::clamp(cell.vol, 0, 64);
        int fxIdx, volIdx;
        splitEffects(cell.fx, vol < 0, [](const MFx& f) { int a, b; return itEffect(f, a, b); }, fxIdx, volIdx);
        bool haveFx = fxIdx >= 0 && itEffect(cell.fx[fxIdx], cmd, prm);
        if (volIdx >= 0) {
          // IT volume column: 85-94 slide up, 95-104 slide down, 128-192 pan,
          // 193-202 tone portamento, 203-212 vibrato depth.
          const MFx& f = cell.fx[volIdx];
          int p = std::clamp(f.param, 0, 255);
          if (f.type == TFx::VolSlide) vol = (p >> 4) ? 85 + std::min(p >> 4, 9) : 95 + std::min(p & 15, 9);
          else if (f.type == TFx::Vibrato) vol = 203 + std::min(p & 15, 9);
          else if (f.type == TFx::TonePorta) vol = 193 + std::clamp(p / 16, 1, 9);
          else vol = 128 + p * 64 / 255;
        }
        int placed = (fxIdx >= 0) + (volIdx >= 0);
        if ((int)cell.fx.size() > placed) m.dropped["effects that didn't fit in the cell"] += (int)cell.fx.size() - placed;
        int mask = (note >= 0 ? 1 : 0) | (ins ? 2 : 0) | (vol >= 0 ? 4 : 0) | (haveFx ? 8 : 0);
        if (!mask) continue;
        o.u8((c + 1) | 0x80);
        o.u8(mask);
        if (note >= 0) o.u8(note);
        if (ins) o.u8(ins);
        if (vol >= 0) o.u8(vol);
        if (haveFx) {
          o.u8(cmd);
          o.u8(prm);
        }
      }
      o.u8(0);
    }
    o.put16At(lenAt, (int)(o.b.size() - start));
  }
  for (int i = 0; i < nSmp; i++) {
    const MInstrument& in = m.instruments[i];
    if (in.samples.empty()) continue;
    o.put32At(dataPtrAt[i], (uint32_t)o.b.size());
    for (int16_t v : in.samples[0].data) o.u16((uint16_t)v);
  }
  return o.b;
}

}  // namespace

ModuleFormat moduleFormatFromPath(const std::string& path) {
  std::string ext;
  size_t dot = path.find_last_of('.');
  if (dot != std::string::npos) ext = path.substr(dot + 1);
  for (char& c : ext) c = (char)std::tolower((unsigned char)c);
  if (ext == "mod") return ModuleFormat::MOD;
  if (ext == "it") return ModuleFormat::IT;
  return ModuleFormat::XM;
}

bool exportModule(const Song& song, const std::string& path, const ModuleExportOptions& opt, std::string& err,
                  std::string& report) {
  if (song.channelCount() > 32 && opt.format != ModuleFormat::IT) {
    err = "XM and MOD hold at most 32 channels";
    return false;
  }
  Module m = buildModule(song, opt);
  std::vector<uint8_t> bytes = opt.format == ModuleFormat::XM    ? writeXM(m)
                               : opt.format == ModuleFormat::MOD ? writeMOD(m)
                                                                 : writeIT(m);
  std::ofstream f(path, std::ios::binary);
  if (!f) {
    err = "Cannot open " + path + " for writing";
    return false;
  }
  f.write((const char*)bytes.data(), (std::streamsize)bytes.size());
  if (!f) {
    err = "Write error on " + path;
    return false;
  }
  std::ostringstream ss;
  for (auto& [what, n] : m.dropped) ss << n << " x " << what << "\n";
  report = ss.str();
  return true;
}
