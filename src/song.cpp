#include "song.h"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <sstream>

const char* const WAVE_NAMES[WAVE_COUNT] = {"Pulse", "Triangle", "Saw", "Noise", "Sine", "Wavetable"};
const char* const MACRO_NAMES[MACRO_COUNT] = {"Volume", "Arpeggio", "Duty", "Waveform", "Pitch"};
const int MACRO_MIN[MACRO_COUNT] = {0, -48, 0, 0, -64};
const int MACRO_MAX[MACRO_COUNT] = {15, 48, 3, WAVE_COUNT - 1, 64};

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

Song::Song() { reset(); }

void Song::reset(int channelCount) {
  name = "Untitled";
  author.clear();
  tickRate = 60.0f;
  speed = 6;
  patternLength = 64;
  highlight1 = 4;
  highlight2 = 16;
  channels.clear();
  setChannelCount(channelCount);
  orders.assign(1, {});
  instruments.assign(1, Instrument());
  instruments[0].name = "Pulse";
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
static const int FILE_VERSION = 1;

static std::string restOfLine(std::istringstream& ss) {
  std::string s;
  std::getline(ss, s);
  size_t start = s.find_first_not_of(" \t");
  if (start == std::string::npos) return "";
  s = s.substr(start);
  while (!s.empty() && (s.back() == '\r' || s.back() == '\n')) s.pop_back();
  return s;
}

bool Song::save(const std::string& path, std::string& err) const {
  std::ofstream f(path);
  if (!f) {
    err = "Cannot open " + path + " for writing";
    return false;
  }
  f << FILE_MAGIC << ' ' << FILE_VERSION << '\n';
  f << "name " << name << '\n';
  f << "author " << author << '\n';
  f << "tickrate " << tickRate << '\n';
  f << "speed " << speed << '\n';
  f << "patlen " << patternLength << '\n';
  f << "highlight " << highlight1 << ' ' << highlight2 << '\n';
  f << "channels " << channelCount() << '\n';
  for (int c = 0; c < channelCount(); c++)
    f << "channel " << c << ' ' << channels[c].effectCols << ' ' << channels[c].name << '\n';

  for (size_t i = 0; i < instruments.size(); i++) {
    const Instrument& ins = instruments[i];
    f << "instrument " << i << ' ' << ins.name << '\n';
    f << "ins.wave " << ins.wave << '\n';
    f << "ins.duty " << ins.duty << '\n';
    f << "ins.volume " << ins.volume << '\n';
    f << "ins.wavetable";
    for (int v : ins.wavetable) f << ' ' << v;
    f << '\n';
    for (int m = 0; m < MACRO_COUNT; m++) {
      const Macro& mac = ins.macros[m];
      if (mac.values.empty()) continue;
      f << "ins.macro " << m << ' ' << mac.loop << ' ' << mac.release << ' ' << mac.values.size();
      for (int v : mac.values) f << ' ' << v;
      f << '\n';
    }
  }

  for (const auto& o : orders) {
    f << "order";
    for (int c = 0; c < channelCount(); c++) f << ' ' << (int)o[c];
    f << '\n';
  }

  for (int c = 0; c < channelCount(); c++) {
    for (int p = 0; p < MAX_PATTERNS; p++) {
      const Pattern* pat = pattern(c, p);
      if (!pat || pat->isEmpty()) continue;
      f << "pattern " << c << ' ' << p << '\n';
      for (int r = 0; r < MAX_ROWS; r++) {
        const Cell& cell = pat->rows[r];
        if (cell.empty()) continue;
        f << "row " << r << ' ' << cell.note << ' ' << cell.ins << ' ' << cell.vol;
        for (const Effect& e : cell.fx) f << ' ' << e.cmd << ' ' << e.val;
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

  Song s;
  s.instruments.clear();
  s.orders.clear();
  Instrument* curIns = nullptr;
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
    } else if (key == "speed") {
      ss >> s.speed;
      s.speed = clampVal(s.speed, 1, 255);
    } else if (key == "patlen") {
      ss >> s.patternLength;
      s.patternLength = clampVal(s.patternLength, 1, MAX_ROWS);
    } else if (key == "highlight") {
      ss >> s.highlight1 >> s.highlight2;
    } else if (key == "channels") {
      int n = 0;
      ss >> n;
      s.channels.clear();
      s.setChannelCount(n);
    } else if (key == "channel") {
      int c = -1, fx = 1;
      ss >> c >> fx;
      if (c >= 0 && c < s.channelCount()) {
        s.channels[c].effectCols = clampVal(fx, 1, MAX_EFFECTS);
        s.channels[c].name = restOfLine(ss);
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
      if (key == "ins.wave") {
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
        for (int& v : m.values) {
          ss >> v;
          v = clampVal(v, MACRO_MIN[type], MACRO_MAX[type]);
        }
        m.loop = (loop >= 0 && loop < n) ? loop : -1;
        m.release = (rel >= 0 && rel < n) ? rel : -1;
      }
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
        ss >> cmd >> val;
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
