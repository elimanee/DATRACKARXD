#include "song.h"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <cstring>
#include <sstream>

const char* const WAVE_NAMES[WAVE_COUNT] = {"Pulse", "Triangle", "Saw", "Noise", "Sine", "Wavetable"};
const char* const MACRO_NAMES[MACRO_COUNT] = {"Volume", "Arpeggio", "Duty", "Waveform", "Pitch"};
const char* const INS_TYPE_NAMES[INS_TYPE_COUNT] = {"Chip", "FM", "Sample"};
const char* const LOOP_NAMES[4] = {"No loop", "Forward", "Ping-pong", "Backward"};

bool Cell::empty() const {
  if (note != NOTE_EMPTY || ins >= 0 || vol >= 0) return false;
  for (const Effect& e : fx)
    if (e.cmd >= 0 || e.val >= 0) return false;
  return true;
}

bool Pattern::isEmpty() const {
  for (const Cell& c : rows)
    if (!c.empty()) return false;
  return true;
}

Instrument::Instrument() {
  // Default wavetable: a sine shape.
  for (int i = 0; i < WAVETABLE_LEN; i++)
    wavetable[i] = (int)std::lround(7.5 + 7.5 * std::sin(i * 2.0 * 3.14159265358979 / WAVETABLE_LEN));
}

void Instrument::macroRange(int macro, int& lo, int& hi) const {
  switch (macro) {
    case MACRO_VOL:
      lo = 0;
      hi = type == INS_FM ? 127 : type == INS_SAMPLE ? 64 : 15;
      break;
    case MACRO_ARP: lo = -60; hi = ARP_FIXED + NOTE_COUNT - 1; break;
    case MACRO_DUTY: lo = 0; hi = 3; break;
    case MACRO_WAVE: lo = 0; hi = 255; break;
    default: lo = -128; hi = 127; break;
  }
}

Song::Song() { reset(); }

void Song::reset(int channelCount) {
  name = "Untitled";
  author.clear();
  comment.clear();
  insResetsVolume = false;
  tickRate = 60.0f;
  speeds = {6};
  patternLength = 64;
  highlight1 = 4;
  highlight2 = 16;
  channels.clear();
  setChannelCount(channelCount);
  orders.assign(1, {});
  instruments.assign(1, Instrument());
  instruments[0].name = "Pulse";
  samples.clear();
  wavetables.clear();
}

void Song::cleanup() {
  for (int c = 0; c < channelCount(); c++) {
    std::vector<bool> used(MAX_PATTERNS, false);
    for (auto& o : orders) used[o[c]] = true;
    for (int p = 0; p < MAX_PATTERNS; p++) {
      Pattern& pat = channels[c].patterns[p];
      if (pat.allocated() && (!used[p] || pat.isEmpty())) pat.rows.clear();
    }
  }
}

void Song::setChannelCount(int n) {
  n = std::clamp(n, 1, MAX_CHANNELS);
  int old = (int)channels.size();
  channels.resize(n);
  for (int i = old; i < n; i++) {
    channels[i].name = "Channel " + std::to_string(i + 1);
    channels[i].patterns.resize(MAX_PATTERNS);
  }
}

Pattern* Song::pattern(int ch, int idx, bool create) {
  if (ch < 0 || ch >= channelCount() || idx < 0 || idx >= MAX_PATTERNS) return nullptr;
  Pattern& p = channels[ch].patterns[idx];
  if (!p.allocated()) {
    if (!create) return nullptr;
    p.rows.resize(MAX_ROWS);
  }
  return &p;
}

const Pattern* Song::pattern(int ch, int idx) const {
  if (ch < 0 || ch >= channelCount() || idx < 0 || idx >= MAX_PATTERNS) return nullptr;
  const Pattern& p = channels[ch].patterns[idx];
  return p.allocated() ? &p : nullptr;
}

Cell* Song::cell(int ch, int order, int row, bool create) {
  if (order < 0 || order >= (int)orders.size() || row < 0 || row >= MAX_ROWS) return nullptr;
  if (ch < 0 || ch >= channelCount()) return nullptr;
  Pattern* p = pattern(ch, orders[order][ch], create);
  return p ? &p->rows[row] : nullptr;
}

const Cell* Song::cell(int ch, int order, int row) const {
  if (order < 0 || order >= (int)orders.size() || row < 0 || row >= MAX_ROWS) return nullptr;
  if (ch < 0 || ch >= channelCount()) return nullptr;
  const Pattern* p = pattern(ch, orders[order][ch]);
  return p ? &p->rows[row] : nullptr;
}

std::string noteName(int note) {
  static const char* names[12] = {"C-", "C#", "D-", "D#", "E-", "F-", "F#", "G-", "G#", "A-", "A#", "B-"};
  if (note == NOTE_OFF) return "OFF";
  if (note < 0 || note > 119) return "---";
  return std::string(names[note % 12]) + std::to_string(note / 12);
}

// ---------------------------------------------------------------------------
// File format (.dtk): a line-based text format, easy to diff and hand-edit.
// ---------------------------------------------------------------------------

static const char* FILE_MAGIC = "DATRACKARXD";
static const int FILE_VERSION = 2;

static std::string restOfLine(std::istringstream& ss) {
  std::string s;
  std::getline(ss, s);
  size_t start = s.find_first_not_of(" \t");
  if (start == std::string::npos) return "";
  s = s.substr(start);
  while (!s.empty() && (s.back() == '\r' || s.back() == '\n')) s.pop_back();
  return s;
}

static const char B64[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

static std::string base64Encode(const std::vector<uint8_t>& in) {
  std::string out;
  out.reserve((in.size() + 2) / 3 * 4);
  for (size_t i = 0; i < in.size(); i += 3) {
    uint32_t v = in[i] << 16;
    if (i + 1 < in.size()) v |= in[i + 1] << 8;
    if (i + 2 < in.size()) v |= in[i + 2];
    out += B64[(v >> 18) & 63];
    out += B64[(v >> 12) & 63];
    out += i + 1 < in.size() ? B64[(v >> 6) & 63] : '=';
    out += i + 2 < in.size() ? B64[v & 63] : '=';
  }
  return out;
}

static std::vector<uint8_t> base64Decode(const std::string& in) {
  int table[256];
  std::fill(std::begin(table), std::end(table), -1);
  for (int i = 0; i < 64; i++) table[(unsigned char)B64[i]] = i;
  std::vector<uint8_t> out;
  uint32_t v = 0;
  int bits = 0;
  for (unsigned char c : in) {
    if (table[c] < 0) continue;
    v = (v << 6) | table[c];
    bits += 6;
    if (bits >= 8) {
      bits -= 8;
      out.push_back((uint8_t)(v >> bits));
    }
  }
  return out;
}

static void writeMacro(std::ostream& f, int type, const Macro& mac) {
  f << "ins.macro " << type << ' ' << mac.loop << ' ' << mac.release << ' ' << mac.values.size();
  for (int v : mac.values) f << ' ' << v;
  f << '\n';
}

bool Song::save(const std::string& path, std::string& err) const {
  std::ofstream f(path);
  if (!f) {
    err = "Cannot open " + path + " for writing";
    return false;
  }
  f.precision(9);  // enough digits for floats to load back exactly
  f << FILE_MAGIC << ' ' << FILE_VERSION << '\n';
  f << "name " << name << '\n';
  f << "author " << author << '\n';
  if (!comment.empty()) {
    std::string c = comment;
    for (char& ch : c)
      if (ch == '\n') ch = '\x1f';
    f << "comment " << c << '\n';
  }
  f << "tickrate " << tickRate << '\n';
  f << "speeds";
  for (int v : speeds) f << ' ' << v;
  f << '\n';
  f << "patlen " << patternLength << '\n';
  f << "highlight " << highlight1 << ' ' << highlight2 << '\n';
  if (insResetsVolume) f << "insresetvol 1\n";
  f << "channels " << channelCount() << '\n';
  for (int c = 0; c < channelCount(); c++) {
    const ChannelInfo& ch = channels[c];
    f << "channel " << c << ' ' << ch.effectCols << ' ' << ch.pan << ' ' << ch.forceWave << ' ' << ch.name << '\n';
    if (ch.volSlideUnit != 512) f << "chanslide " << c << ' ' << ch.volSlideUnit << '\n';
    if (ch.fixedDuty >= 0 || ch.minNote > 0 || ch.mix != 1.0f)
      f << "chanchip " << c << ' ' << ch.fixedDuty << ' ' << ch.minNote << ' ' << ch.mix << '\n';
  }

  for (size_t i = 0; i < instruments.size(); i++) {
    const Instrument& ins = instruments[i];
    f << "instrument " << i << ' ' << ins.name << '\n';
    f << "ins.type " << ins.type << '\n';
    f << "ins.wave " << ins.wave << '\n';
    f << "ins.duty " << ins.duty << '\n';
    f << "ins.volume " << ins.volume << '\n';
    f << "ins.wavetable";
    for (int v : ins.wavetable) f << ' ' << v;
    f << '\n';
    for (int m = 0; m < MACRO_COUNT; m++)
      if (!ins.macros[m].values.empty()) writeMacro(f, m, ins.macros[m]);
    if (ins.type == INS_FM) {
      f << "ins.fm " << ins.fm.alg << ' ' << ins.fm.fb << '\n';
      for (int o = 0; o < 4; o++) {
        const FMOperator& op = ins.fm.ops[o];
        f << "ins.fmop " << o << ' ' << op.enabled << ' ' << op.mult << ' ' << op.dt << ' ' << op.tl << ' ' << op.ar << ' '
          << op.dr << ' ' << op.sl << ' ' << op.d2r << ' ' << op.rr << ' ' << op.ksr << ' ' << op.ssg << '\n';
      }
    }
    if (ins.sample >= 0) f << "ins.sample " << ins.sample << '\n';
    if (ins.songWave >= 0) f << "ins.songwave " << ins.songWave << '\n';
    if (!ins.sampleMap.empty()) {
      f << "ins.samplemap";
      for (int v : ins.sampleMap) f << ' ' << v;
      f << '\n';
    }
    if (ins.fadeout > 0) f << "ins.fadeout " << ins.fadeout << '\n';
  }

  for (size_t i = 0; i < samples.size(); i++) {
    const Sample& smp = samples[i];
    f << "sample " << i << ' ' << smp.name << '\n';
    f << "smp.info " << smp.rate << ' ' << smp.loopStart << ' ' << smp.loopEnd << ' ' << smp.loopMode << ' ' << smp.volume << '\n';
    std::vector<uint8_t> bytes(smp.data.size() * 2);
    for (size_t k = 0; k < smp.data.size(); k++) {
      // Same scale as loading (/32768) so save/load round-trips exactly.
      int16_t v = (int16_t)std::clamp((long)std::lround(smp.data[k] * 32768.0f), -32768L, 32767L);
      bytes[k * 2] = (uint8_t)(v & 255);
      bytes[k * 2 + 1] = (uint8_t)((uint16_t)v >> 8);
    }
    f << "smp.data " << smp.data.size() << ' ' << base64Encode(bytes) << '\n';
  }

  for (size_t i = 0; i < wavetables.size(); i++) {
    f << "wave " << i << ' ' << wavetables[i].data.size();
    for (float v : wavetables[i].data) f << ' ' << v;
    f << '\n';
  }

  for (const auto& o : orders) {
    f << "order";
    for (int c = 0; c < channelCount(); c++) f << ' ' << (int)o[c];
    f << '\n';
  }

  for (int c = 0; c < channelCount(); c++) {
    int cols = channels[c].effectCols;
    for (int p = 0; p < MAX_PATTERNS; p++) {
      const Pattern* pat = pattern(c, p);
      if (!pat || pat->isEmpty()) continue;
      f << "pattern " << c << ' ' << p << '\n';
      for (int r = 0; r < MAX_ROWS; r++) {
        const Cell& cell = pat->rows[r];
        if (cell.empty()) continue;
        f << "row " << r << ' ' << cell.note << ' ' << cell.ins << ' ' << cell.vol;
        for (int e = 0; e < cols; e++) f << ' ' << cell.fx[e].cmd << ' ' << cell.fx[e].val;
        f << '\n';
      }
    }
  }
  f << "end\n";
  if (!f) {
    err = "Write error on " + path;
    return false;
  }
  return true;
}

bool Song::load(const std::string& path, std::string& err) {
  std::ifstream f(path);
  if (!f) {
    err = "Cannot open " + path;
    return false;
  }
  std::string line;
  if (!std::getline(f, line) || line.rfind(FILE_MAGIC, 0) != 0) {
    err = path + " is not a DATRACKARXD song";
    return false;
  }
  int version = 1;
  {
    std::istringstream hs(line.substr(std::strlen(FILE_MAGIC)));
    hs >> version;
  }

  Song s;
  s.instruments.clear();
  s.orders.clear();
  Instrument* curIns = nullptr;
  Sample* curSmp = nullptr;
  Pattern* curPat = nullptr;
  int lineNo = 1;

  auto clampVal = [](int v, int lo, int hi) { return std::clamp(v, lo, hi); };

  while (std::getline(f, line)) {
    lineNo++;
    std::istringstream ss(line);
    std::string key;
    if (!(ss >> key)) continue;

    if (key == "name") {
      s.name = restOfLine(ss);
    } else if (key == "author") {
      s.author = restOfLine(ss);
    } else if (key == "tickrate") {
      ss >> s.tickRate;
      s.tickRate = std::clamp(s.tickRate, 1.0f, 1000.0f);
    } else if (key == "comment") {
      s.comment = restOfLine(ss);
      for (char& ch : s.comment)
        if (ch == '\x1f') ch = '\n';
    } else if (key == "speed" || key == "speeds") {
      s.speeds.clear();
      int v;
      while (ss >> v && (int)s.speeds.size() < MAX_GROOVE) s.speeds.push_back(clampVal(v, 1, 255));
      if (s.speeds.empty()) s.speeds.push_back(6);
    } else if (key == "patlen") {
      ss >> s.patternLength;
      s.patternLength = clampVal(s.patternLength, 1, MAX_ROWS);
    } else if (key == "insresetvol") {
      int v = 0;
      ss >> v;
      s.insResetsVolume = v != 0;
    } else if (key == "highlight") {
      ss >> s.highlight1 >> s.highlight2;
    } else if (key == "channels") {
      int n = 0;
      ss >> n;
      s.channels.clear();
      s.setChannelCount(n);
    } else if (key == "channel") {
      int c = -1, fx = 1, pan = 128, forceWave = -1;
      ss >> c >> fx;
      if (version >= 2) ss >> pan >> forceWave;
      if (c >= 0 && c < s.channelCount()) {
        s.channels[c].effectCols = clampVal(fx, 1, MAX_EFFECTS);
        s.channels[c].pan = clampVal(pan, 0, 255);
        s.channels[c].forceWave = clampVal(forceWave, -1, WAVE_COUNT - 1);
        s.channels[c].name = restOfLine(ss);
      }
    } else if (key == "chanslide") {
      int c = -1, unit = 512;
      ss >> c >> unit;
      if (c >= 0 && c < s.channelCount()) s.channels[c].volSlideUnit = clampVal(unit, 1, 32767);
    } else if (key == "chanchip") {
      int c = -1, duty = -1, minNote = 0;
      float mix = 1.0f;
      ss >> c >> duty >> minNote;
      if (!(ss >> mix)) mix = 1.0f;
      if (c >= 0 && c < s.channelCount()) {
        s.channels[c].mix = std::clamp(mix, 0.0f, 4.0f);
        s.channels[c].fixedDuty = clampVal(duty, -1, 3);
        s.channels[c].minNote = clampVal(minNote, 0, NOTE_COUNT - 1);
      }
    } else if (key == "instrument") {
      int idx = 0;
      ss >> idx;
      if ((int)s.instruments.size() >= MAX_INSTRUMENTS) {
        curIns = nullptr;
        continue;
      }
      s.instruments.emplace_back();
      curIns = &s.instruments.back();
      curIns->name = restOfLine(ss);
    } else if (key.rfind("ins.", 0) == 0) {
      if (!curIns) continue;
      if (key == "ins.type") {
        ss >> curIns->type;
        curIns->type = clampVal(curIns->type, 0, INS_TYPE_COUNT - 1);
      } else if (key == "ins.wave") {
        ss >> curIns->wave;
        curIns->wave = clampVal(curIns->wave, 0, WAVE_COUNT - 1);
      } else if (key == "ins.duty") {
        ss >> curIns->duty;
        curIns->duty = clampVal(curIns->duty, 0, 3);
      } else if (key == "ins.volume") {
        ss >> curIns->volume;
        curIns->volume = clampVal(curIns->volume, 0, 15);
      } else if (key == "ins.wavetable") {
        for (int& v : curIns->wavetable) {
          ss >> v;
          v = clampVal(v, 0, 15);
        }
      } else if (key == "ins.macro") {
        int type = -1, loop = -1, rel = -1, n = 0;
        ss >> type >> loop >> rel >> n;
        if (type < 0 || type >= MACRO_COUNT) continue;
        Macro& m = curIns->macros[type];
        n = clampVal(n, 0, MAX_MACRO_LEN);
        m.values.resize(n);
        int lo, hi;
        curIns->macroRange(type, lo, hi);
        for (int& v : m.values) {
          ss >> v;
          v = clampVal(v, lo, hi);
        }
        m.loop = (loop >= 0 && loop < n) ? loop : -1;
        m.release = (rel >= 0 && rel < n) ? rel : -1;
      } else if (key == "ins.fm") {
        ss >> curIns->fm.alg >> curIns->fm.fb;
        curIns->fm.alg = clampVal(curIns->fm.alg, 0, 7);
        curIns->fm.fb = clampVal(curIns->fm.fb, 0, 7);
      } else if (key == "ins.fmop") {
        int o = -1, en = 1;
        ss >> o;
        if (o < 0 || o > 3) continue;
        FMOperator& op = curIns->fm.ops[o];
        ss >> en >> op.mult >> op.dt >> op.tl >> op.ar >> op.dr >> op.sl >> op.d2r >> op.rr >> op.ksr >> op.ssg;
        op.enabled = en != 0;
        op.mult = clampVal(op.mult, 0, 15);
        op.dt = clampVal(op.dt, 0, 7);
        op.tl = clampVal(op.tl, 0, 127);
        op.ar = clampVal(op.ar, 0, 31);
        op.dr = clampVal(op.dr, 0, 31);
        op.sl = clampVal(op.sl, 0, 15);
        op.d2r = clampVal(op.d2r, 0, 31);
        op.rr = clampVal(op.rr, 0, 15);
        op.ksr = clampVal(op.ksr, 0, 3);
        op.ssg = clampVal(op.ssg, 0, 15);
      } else if (key == "ins.sample") {
        ss >> curIns->sample;
        curIns->sample = clampVal(curIns->sample, -1, MAX_SAMPLES - 1);
      } else if (key == "ins.songwave") {
        ss >> curIns->songWave;
        curIns->songWave = clampVal(curIns->songWave, -1, MAX_WAVETABLES - 1);
      } else if (key == "ins.samplemap") {
        curIns->sampleMap.assign(NOTE_COUNT, -1);
        for (auto& v : curIns->sampleMap) {
          int x = -1;
          ss >> x;
          v = (int16_t)clampVal(x, -1, MAX_SAMPLES - 1);
        }
      } else if (key == "ins.fadeout") {
        ss >> curIns->fadeout;
        curIns->fadeout = std::clamp(curIns->fadeout, 0.0f, 1.0f);
      }
    } else if (key == "sample") {
      int idx = 0;
      ss >> idx;
      if ((int)s.samples.size() >= MAX_SAMPLES) {
        curSmp = nullptr;
        continue;
      }
      s.samples.emplace_back();
      curSmp = &s.samples.back();
      curSmp->name = restOfLine(ss);
    } else if (key == "smp.info") {
      if (!curSmp) continue;
      ss >> curSmp->rate >> curSmp->loopStart >> curSmp->loopEnd >> curSmp->loopMode >> curSmp->volume;
      curSmp->rate = clampVal(curSmp->rate, 1, 1000000);
      curSmp->loopMode = clampVal(curSmp->loopMode, 0, 3);
      curSmp->volume = clampVal(curSmp->volume, 0, 64);
    } else if (key == "smp.data") {
      if (!curSmp) continue;
      size_t n = 0;
      std::string b64;
      ss >> n >> b64;
      std::vector<uint8_t> bytes = base64Decode(b64);
      n = std::min(n, bytes.size() / 2);
      curSmp->data.resize(n);
      for (size_t k = 0; k < n; k++) curSmp->data[k] = (int16_t)(bytes[k * 2] | (bytes[k * 2 + 1] << 8)) / 32768.0f;
      int len = (int)n;
      curSmp->loopStart = clampVal(curSmp->loopStart, 0, len);
      curSmp->loopEnd = clampVal(curSmp->loopEnd, curSmp->loopStart, len);
    } else if (key == "wave") {
      int idx = 0, n = 0;
      ss >> idx >> n;
      if ((int)s.wavetables.size() >= MAX_WAVETABLES) continue;
      Wavetable w;
      w.data.resize(clampVal(n, 0, 4096));
      for (float& v : w.data) {
        float x = 0;
        ss >> x;
        v = std::clamp(x, -1.0f, 1.0f);
      }
      s.wavetables.push_back(std::move(w));
    } else if (key == "order") {
      if ((int)s.orders.size() >= MAX_ORDERS) continue;
      std::array<uint8_t, MAX_CHANNELS> o{};
      for (int c = 0; c < s.channelCount(); c++) {
        int v = 0;
        ss >> v;
        o[c] = (uint8_t)clampVal(v, 0, MAX_PATTERNS - 1);
      }
      s.orders.push_back(o);
    } else if (key == "pattern") {
      int c = -1, p = -1;
      ss >> c >> p;
      curPat = s.pattern(c, p, true);
    } else if (key == "row") {
      if (!curPat) continue;
      int r = -1;
      ss >> r;
      if (r < 0 || r >= MAX_ROWS) continue;
      Cell& cell = curPat->rows[r];
      int note = -1, ins = -1, vol = -1;
      ss >> note >> ins >> vol;
      cell.note = (int16_t)clampVal(note, NOTE_OFF, 119);
      cell.ins = (int16_t)clampVal(ins, -1, MAX_INSTRUMENTS - 1);
      cell.vol = (int16_t)clampVal(vol, -1, 0x7f);
      for (Effect& e : cell.fx) {
        int cmd = -1, val = -1;
        if (!(ss >> cmd >> val)) cmd = val = -1;
        e.cmd = (int16_t)clampVal(cmd, -1, 0xff);
        e.val = (int16_t)clampVal(val, -1, 0xff);
      }
    } else if (key == "end") {
      break;
    } else {
      err = "Unknown keyword '" + key + "' at line " + std::to_string(lineNo);
      return false;
    }
  }

  if (s.channels.empty()) s.setChannelCount(8);
  if (s.orders.empty()) s.orders.push_back({});
  if (s.instruments.empty()) s.instruments.emplace_back();
  *this = std::move(s);
  return true;
}

// ---------------------------------------------------------------------------
// WAV sample loading: PCM 8/16/24/32-bit and 32-bit float, mixed to mono.
// ---------------------------------------------------------------------------

bool loadWavSample(const std::string& path, Sample& out, std::string& err) {
  std::ifstream f(path, std::ios::binary);
  if (!f) {
    err = "Cannot open " + path;
    return false;
  }
  std::vector<uint8_t> d((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
  auto u16 = [&](size_t p) { return (uint32_t)(d[p] | (d[p + 1] << 8)); };
  auto u32 = [&](size_t p) { return (uint32_t)(d[p] | (d[p + 1] << 8) | (d[p + 2] << 16) | ((uint32_t)d[p + 3] << 24)); };
  if (d.size() < 12 || std::memcmp(d.data(), "RIFF", 4) != 0 || std::memcmp(d.data() + 8, "WAVE", 4) != 0) {
    err = "Not a WAV file";
    return false;
  }
  int format = 0, channels = 0, rate = 0, bits = 0;
  size_t dataPos = 0, dataLen = 0;
  int loopStart = -1, loopEnd = -1;
  for (size_t p = 12; p + 8 <= d.size();) {
    uint32_t len = u32(p + 4);
    size_t body = p + 8;
    if (body + len > d.size()) len = (uint32_t)(d.size() - body);
    if (!std::memcmp(&d[p], "fmt ", 4) && len >= 16) {
      format = u16(body);
      channels = u16(body + 2);
      rate = u32(body + 4);
      bits = u16(body + 14);
      if (format == 0xFFFE && len >= 26) format = u16(body + 24);  // WAVE_FORMAT_EXTENSIBLE
    } else if (!std::memcmp(&d[p], "data", 4)) {
      dataPos = body;
      dataLen = len;
    } else if (!std::memcmp(&d[p], "smpl", 4) && len >= 36 + 24 && u32(body + 28) > 0) {
      loopStart = (int)u32(body + 36 + 8);
      loopEnd = (int)u32(body + 36 + 12) + 1;
    }
    p = body + len + (len & 1);
  }
  if (!dataPos || channels < 1 || (format != 1 && format != 3) || (format == 3 && bits != 32) ||
      (format == 1 && bits != 8 && bits != 16 && bits != 24 && bits != 32)) {
    err = "Unsupported WAV format (use PCM or 32-bit float)";
    return false;
  }
  int bps = bits / 8;
  size_t frames = dataLen / (bps * channels);
  out.data.assign(frames, 0.0f);
  for (size_t i = 0; i < frames; i++) {
    float sum = 0;
    for (int c = 0; c < channels; c++) {
      size_t p = dataPos + (i * channels + c) * bps;
      float v;
      if (format == 3) {
        uint32_t raw = u32(p);
        std::memcpy(&v, &raw, 4);
      } else if (bits == 8) {
        v = (d[p] - 128) / 128.0f;
      } else if (bits == 16) {
        v = (int16_t)u16(p) / 32768.0f;
      } else if (bits == 24) {
        int32_t x = (int32_t)((d[p] << 8) | (d[p + 1] << 16) | ((uint32_t)d[p + 2] << 24)) >> 8;
        v = x / 8388608.0f;
      } else {
        v = (int32_t)u32(p) / 2147483648.0f;
      }
      sum += v;
    }
    out.data[i] = std::clamp(sum / channels, -1.0f, 1.0f);
  }
  out.rate = rate > 0 ? rate : 44100;
  out.volume = 64;
  if (loopStart >= 0 && loopEnd > loopStart && loopEnd <= (int)frames) {
    out.loopMode = 1;
    out.loopStart = loopStart;
    out.loopEnd = loopEnd;
  } else {
    out.loopMode = 0;
    out.loopStart = 0;
    out.loopEnd = (int)frames;
  }
  size_t slash = path.find_last_of("/\\");
  out.name = path.substr(slash == std::string::npos ? 0 : slash + 1);
  return true;
}
