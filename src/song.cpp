#include "song.h"

#include "chips.h"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <cstring>
#include <sstream>

const char* const WAVE_NAMES[WAVE_COUNT] = {"Pulse", "Triangle", "Saw", "Noise", "Sine", "Wavetable"};
const char* const MACRO_NAMES[MACRO_COUNT] = {"Volume", "Arpeggio", "Duty", "Waveform", "Pitch", "Cutoff"};
const char* const INS_TYPE_NAMES[INS_TYPE_COUNT] = {"Chip",         "FM",       "Sample",          "AY / YM2149",
                                                    "SN76489",      "Game Boy", "OPL3 (AdLib)",    "YM2612 (Mega Drive)",
                                                    "NES (2A03)", "Konami SCC", "PC Engine"};
const char* const LOOP_NAMES[4] = {"No loop", "Forward", "Ping-pong", "Backward"};
const char* const LFO_TARGET_NAMES[LFO_TARGET_COUNT] = {"Pitch", "Volume", "Pulse width", "Cutoff", "Pan"};
const ChanFxInfo CHAN_FX_INFO[CFX_COUNT] = {
    {"Bitcrusher", 3,
     {{"Bits", 1, 16, 6, "%.0f bits", false, nullptr},
      {"Downsample", 1, 64, 4, "x%.0f", true, nullptr},
      {"Mix", 0, 1, 1, "%.2f", false, nullptr},
      {}}},
    {"Distortion", 4,
     {{"Drive", 0, 1, 0.5f, "%.2f", false, nullptr},
      {"Tone", 0, 1, 0.7f, "%.2f", false, nullptr},
      {"Mix", 0, 1, 1, "%.2f", false, nullptr},
      {"Type", 0, 2, 0, nullptr, false, "Soft\0Hard clip\0Fold\0"}}},
    {"Chorus", 4,
     {{"Rate", 0.05f, 5, 0.8f, "%.2f Hz", true, nullptr},
      {"Depth", 0, 10, 3, "%.1f ms", false, nullptr},
      {"Mix", 0, 1, 0.5f, "%.2f", false, nullptr},
      {"Feedback", 0, 0.9f, 0, "%.2f", false, nullptr}}},
    {"EQ", 4,
     {{"Low", -24, 12, 0, "%+.1f dB", false, nullptr},
      {"Mid", -24, 12, 0, "%+.1f dB", false, nullptr},
      {"Mid freq", 200, 8000, 1000, "%.0f Hz", true, nullptr},
      {"High", -24, 12, 0, "%+.1f dB", false, nullptr}}},
    {"Compressor", 4,
     {{"Threshold", -60, 0, -18, "%.1f dB", false, nullptr},
      {"Ratio", 1, 20, 4, "%.1f:1", true, nullptr},
      {"Attack", 0.1f, 100, 5, "%.1f ms", true, nullptr},
      {"Release", 10, 1000, 100, "%.0f ms", true, nullptr}}},
};

ChannelEffect ChannelEffect::make(int type) {
  ChannelEffect e;
  e.type = std::clamp(type, 0, CFX_COUNT - 1);
  for (int i = 0; i < CHAN_FX_INFO[e.type].count; i++) e.p[i] = CHAN_FX_INFO[e.type].params[i].def;
  return e;
}

const char* const AUTO_TARGET_NAMES[AUTO_TARGET_COUNT] = {"Volume", "Pan",           "Cutoff",       "Reverb send",
                                                          "Delay send", "Effect param", "Master volume", "Master cutoff"};

float AutomationLane::valueAt(float pos) const {
  if (points.empty()) return std::nanf("");
  if (pos <= points.front().pos) return points.front().value;
  if (pos >= points.back().pos) return points.back().value;
  auto it = std::upper_bound(points.begin(), points.end(), pos, [](float p, const AutoPoint& a) { return p < a.pos; });
  const AutoPoint& b = *it;
  const AutoPoint& a = *(it - 1);
  if (b.pos <= a.pos) return b.value;
  return a.value + (b.value - a.value) * (pos - a.pos) / (b.pos - a.pos);
}

void AutomationLane::sort() {
  std::stable_sort(points.begin(), points.end(), [](const AutoPoint& a, const AutoPoint& b) { return a.pos < b.pos; });
}

const char* const LFO_SHAPE_NAMES[LFO_SHAPE_COUNT] = {"Sine", "Triangle", "Saw", "Square", "Random"};

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
  // Default morph target: a saw.
  for (int i = 0; i < WAVETABLE_LEN; i++) morph.table[i] = i / 2;
  // Default OPL sound: a soft electric piano-like 2-op patch.
  opl.ops[0].tl = 28;
  opl.ops[0].mult = 1;
  opl.ops[0].dr = 3;
  opl.ops[0].sl = 3;
  opl.ops[1].mult = 1;
  opl.ops[1].dr = 2;
  opl.ops[1].sl = 2;
  opl.ops[2].tl = 30;
  opl.ops[3].tl = 0;
}

void Instrument::macroRange(int macro, int& lo, int& hi) const {
  switch (macro) {
    case MACRO_VOL:
      lo = 0;
      hi = type == INS_FM || type == INS_OPL || type == INS_OPN ? 127 : type == INS_SAMPLE ? 64 : 15;
      break;
    case MACRO_ARP: lo = -60; hi = ARP_FIXED + NOTE_COUNT - 1; break;
    case MACRO_DUTY:
      lo = 0;
      hi = type == INS_AY ? 31 : type == INS_SN ? 1 : pulseWidth >= 0 && type == INS_STANDARD ? 255 : 3;
      break;
    case MACRO_CUTOFF: lo = 0; hi = 255; break;
    case MACRO_WAVE: lo = 0; hi = type == INS_AY ? 7 : type == INS_OPL ? 7 : 255; break;
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
  automation.clear();
  subsongName.clear();
  subsongs.assign(1, SubSong());
  curSubsong = 0;
}

std::string Song::subsongNameAt(int i) const {
  return i == curSubsong ? subsongName : (i >= 0 && i < (int)subsongs.size() ? subsongs[i].name : std::string());
}

void Song::selectSubsong(int i) {
  if (subsongs.empty()) subsongs.resize(1);
  if (i < 0 || i >= (int)subsongs.size() || i == curSubsong) return;
  SubSong& cur = subsongs[curSubsong];
  cur.name = subsongName;
  cur.tickRate = tickRate;
  cur.speeds = speeds;
  cur.patternLength = patternLength;
  cur.orders = std::move(orders);
  cur.automation = std::move(automation);
  cur.patterns.resize(channels.size());
  for (size_t c = 0; c < channels.size(); c++) cur.patterns[c] = std::move(channels[c].patterns);

  SubSong& next = subsongs[i];
  subsongName = next.name;
  tickRate = next.tickRate;
  speeds = next.speeds.empty() ? std::vector<int>{6} : next.speeds;
  patternLength = std::clamp(next.patternLength, 1, MAX_ROWS);
  orders = std::move(next.orders);
  if (orders.empty()) orders.assign(1, {});
  automation = std::move(next.automation);
  for (size_t c = 0; c < channels.size(); c++) {
    channels[c].patterns = c < next.patterns.size() ? std::move(next.patterns[c]) : std::vector<Pattern>();
    channels[c].patterns.resize(MAX_PATTERNS);
  }
  next = SubSong();
  curSubsong = i;
}

int Song::addSubsong() {
  if (subsongs.empty()) subsongs.resize(1);
  SubSong sub;
  sub.tickRate = tickRate;
  sub.speeds = speeds;
  sub.patternLength = patternLength;
  sub.orders.assign(1, {});
  subsongs.push_back(std::move(sub));
  return (int)subsongs.size() - 1;
}

void Song::removeSubsong(int i) {
  if ((int)subsongs.size() <= 1 || i < 0 || i >= (int)subsongs.size()) return;
  if (i == curSubsong) selectSubsong(i == 0 ? 1 : i - 1);
  subsongs.erase(subsongs.begin() + i);
  if (curSubsong > i) curSubsong--;
}

Song Song::currentSubsongOnly() const {
  Song copy = *this;
  copy.subsongs.assign(1, SubSong());
  copy.curSubsong = 0;
  return copy;
}

void Song::insertOrderTime(int at) {
  for (AutomationLane& lane : automation)
    for (AutoPoint& p : lane.points)
      if (p.pos >= at) p.pos += 1;
}

void Song::removeOrderTime(int at) {
  for (AutomationLane& lane : automation) {
    auto& pts = lane.points;
    pts.erase(std::remove_if(pts.begin(), pts.end(), [&](const AutoPoint& p) { return p.pos >= at && p.pos < at + 1; }), pts.end());
    for (AutoPoint& p : pts)
      if (p.pos >= at + 1) p.pos -= 1;
  }
}

void Song::swapOrderTime(int a, int b) {
  for (AutomationLane& lane : automation) {
    for (AutoPoint& p : lane.points) {
      if (p.pos >= a && p.pos < a + 1) p.pos += (float)(b - a);
      else if (p.pos >= b && p.pos < b + 1) p.pos -= (float)(b - a);
    }
    lane.sort();
  }
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

bool putCellEffect(Cell& cell, int& effectCols, int cmd, int val) {
  for (int e = 0; e < MAX_EFFECTS; e++)
    if (cell.fx[e].cmd == cmd) {
      cell.fx[e].val = (int16_t)val;
      effectCols = std::max(effectCols, e + 1);
      return true;
    }
  for (int e = 0; e < MAX_EFFECTS; e++) {
    if (cell.fx[e].cmd >= 0) continue;
    cell.fx[e] = {(int16_t)cmd, (int16_t)val};
    effectCols = std::max(effectCols, e + 1);
    return true;
  }
  return false;
}

void removeCellEffect(Cell& cell, int cmd) {
  for (Effect& e : cell.fx)
    if (e.cmd == cmd) e = Effect();
}

int cellEffect(const Cell& cell, int cmd) {
  for (const Effect& e : cell.fx)
    if (e.cmd == cmd) return e.val < 0 ? -2 : e.val;
  return -1;
}

void bendEffect(double semitones, int& cmd, int& val) {
  if (semitones >= -8.0 && semitones <= 7.95) {
    cmd = 0xE8;
    val = std::clamp((int)std::lround(semitones * 16) + 0x80, 0, 0xff);
  } else {
    cmd = 0xE9;
    val = std::clamp((int)std::lround(semitones * 4) + 0x80, 0, 0xff);
  }
}

double cellBend(const Cell& cell) {
  int v = cellEffect(cell, 0xE8);
  if (v != -1) return ((v < 0 ? 0x80 : v) - 0x80) / 16.0;
  v = cellEffect(cell, 0xE9);
  if (v != -1) return ((v < 0 ? 0x80 : v) - 0x80) / 4.0;
  return std::nan("");
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

// Written next to the file first, then moved over it: a crash or a full
// disk in the middle never leaves a half-written song.
bool Song::save(const std::string& path, std::string& err) const {
  std::string tmp = path + ".tmp";
  {
    std::ofstream f(tmp);
    if (!f) {
      err = "Cannot open " + path + " for writing";
      return false;
    }
    write(f);
    f.flush();
    if (!f) {
      err = "Write error on " + path;
      f.close();
      std::error_code ec;
      std::filesystem::remove(tmp, ec);
      return false;
    }
  }
  std::error_code ec;
  std::filesystem::rename(tmp, path, ec);
  if (ec) {
    err = "Cannot replace " + path + ": " + ec.message();
    std::filesystem::remove(tmp, ec);
    return false;
  }
  return true;
}

std::string Song::toText() const {
  std::ostringstream ss;
  write(ss);
  return ss.str();
}

bool Song::fromText(const std::string& text, std::string& err) {
  std::istringstream ss(text);
  return read(ss, "song", err);
}

void Song::write(std::ostream& f) const {
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
  {
    bool live0 = curSubsong == 0 || subsongs.empty();
    f << "tickrate " << (live0 ? tickRate : subsongs[0].tickRate) << '\n';
    f << "speeds";
    for (int v : (live0 ? speeds : subsongs[0].speeds)) f << ' ' << v;
    f << '\n';
    f << "patlen " << (live0 ? patternLength : subsongs[0].patternLength) << '\n';
  }
  f << "highlight " << highlight1 << ' ' << highlight2 << '\n';
  if (insResetsVolume) f << "insresetvol 1\n";
  f << "channels " << channelCount() << '\n';
  for (int c = 0; c < channelCount(); c++) {
    const ChannelInfo& ch = channels[c];
    f << "channel " << c << ' ' << ch.effectCols << ' ' << ch.pan << ' ' << ch.forceWave << ' ' << ch.name << '\n';
    if (ch.volSlideUnit != 512) f << "chanslide " << c << ' ' << ch.volSlideUnit << '\n';
    if (ch.fixedDuty >= 0 || ch.minNote > 0 || ch.mix != 1.0f)
      f << "chanchip " << c << ' ' << ch.fixedDuty << ' ' << ch.minNote << ' ' << ch.mix << '\n';
    if (ch.reverbSend > 0 || ch.delaySend > 0) f << "chansend " << c << ' ' << ch.reverbSend << ' ' << ch.delaySend << '\n';
    for (const ChannelEffect& e : ch.effects)
      f << "chanfx " << c << ' ' << e.type << ' ' << e.on << ' ' << e.p[0] << ' ' << e.p[1] << ' ' << e.p[2] << ' ' << e.p[3] << '\n';
  }
  f << "fx.reverb " << fx.reverb << ' ' << fx.roomSize << ' ' << fx.damping << ' ' << fx.reverbLevel << '\n';
  f << "fx.delay " << fx.delay << ' ' << fx.delayRows << ' ' << fx.delayFeedback << ' ' << fx.delayLevel << ' ' << fx.pingPong << '\n';
  f << "fx.filter " << fx.filter << ' ' << fx.filterType << ' ' << fx.cutoff << ' ' << fx.resonance << '\n';
  f << "fx.volume " << fx.volume << '\n';

  for (size_t i = 0; i < instruments.size(); i++) {
    const Instrument& ins = instruments[i];
    f << "instrument " << i << ' ' << ins.name << '\n';
    f << "ins.type " << ins.type << '\n';
    f << "ins.wave " << ins.wave << '\n';
    f << "ins.duty " << ins.duty << '\n';
    f << "ins.volume " << ins.volume << '\n';
    if (!ins.sidDefault())
      f << "ins.sid " << ins.filter.on << ' ' << ins.filter.mode << ' ' << ins.filter.cutoff << ' ' << ins.filter.resonance << ' '
        << ins.ringMod << ' ' << ins.sync << ' ' << ins.pulseWidth << ' ' << ins.pwmSweep << '\n';
    f << "ins.wavetable";
    for (int v : ins.wavetable) f << ' ' << v;
    f << '\n';
    for (int m = 0; m < MACRO_COUNT; m++)
      if (!ins.macros[m].values.empty()) writeMacro(f, m, ins.macros[m]);
    if (ins.type == INS_FM || ins.type == INS_OPN) {
      f << "ins.fm " << ins.fm.alg << ' ' << ins.fm.fb << '\n';
      for (int o = 0; o < 4; o++) {
        const FMOperator& op = ins.fm.ops[o];
        f << "ins.fmop " << o << ' ' << op.enabled << ' ' << op.mult << ' ' << op.dt << ' ' << op.tl << ' ' << op.ar << ' '
          << op.dr << ' ' << op.sl << ' ' << op.d2r << ' ' << op.rr << ' ' << op.ksr << ' ' << op.ssg << '\n';
      }
    }
    if (isChipType(ins.type) && ins.type != INS_OPL) {
      const ChipSettings& c = ins.chip;
      f << "ins.chip " << c.clock << ' ' << c.ayModel << ' ' << c.ayTone << ' ' << c.ayNoise << ' ' << c.ayEnv << ' ' << c.ayNoiseFreq
        << ' ' << c.ayEnvShape << ' ' << c.ayEnvAuto << ' ' << c.ayEnvPeriod << ' ' << c.snNoise << ' ' << c.snWhite << ' '
        << c.gbChannel << ' ' << c.gbEnvDir << ' ' << c.gbEnvLen << ' ' << c.gbSweepTime << ' ' << c.gbSweepDir << ' '
        << c.gbSweepShift << ' ' << c.gbShortNoise << '\n';
    }
    if (ins.type >= INS_OPN) {
      const ChipSettings& c = ins.chip;
      f << "ins.chip2 " << c.opnModel << ' ' << c.opnLfo << ' ' << c.opnAms << ' ' << c.opnFms << ' ' << c.opnAmOps << ' '
        << c.nesChannel << ' ' << c.nesEnv << ' ' << c.nesEnvPeriod << ' ' << c.nesEnvLoop << ' ' << c.nesSweep << ' '
        << c.nesSweepPeriod << ' ' << c.nesSweepShift << ' ' << c.nesSweepDown << ' ' << c.nesShortNoise << ' ' << c.pceNoise
        << '\n';
    }
    if (ins.type == INS_OPL) {
      const OPLParams& o = ins.opl;
      f << "ins.opl " << o.fourOp << ' ' << o.alg << ' ' << o.fb << ' ' << o.deepAm << ' ' << o.deepVib << '\n';
      for (int k = 0; k < (o.fourOp ? 4 : 2); k++) {
        const OPLOperator& op = o.ops[k];
        f << "ins.oplop " << k << ' ' << op.am << ' ' << op.vib << ' ' << op.sus << ' ' << op.ksr << ' ' << op.mult << ' ' << op.ksl
          << ' ' << op.tl << ' ' << op.ar << ' ' << op.dr << ' ' << op.sl << ' ' << op.rr << ' ' << op.ws << '\n';
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
    const InsEnvelope& env = ins.env;
    if (env.on) f << "ins.adsr " << env.on << ' ' << env.attack << ' ' << env.decay << ' ' << env.sustain << ' ' << env.release << '\n';
    for (int l = 0; l < INS_LFOS; l++) {
      const InsLfo& lfo = ins.lfo[l];
      if (!lfo.isDefault())
        f << "ins.lfo " << l << ' ' << lfo.on << ' ' << lfo.target << ' ' << lfo.shape << ' ' << lfo.rate << ' ' << lfo.depth << ' '
          << lfo.delay << '\n';
    }
    if (ins.morph.on) {
      f << "ins.morph " << ins.morph.on << ' ' << ins.morph.time << ' ' << ins.morph.mode;
      for (int v : ins.morph.table) f << ' ' << v;
      f << '\n';
    }
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

  // Each subsong: its orders, patterns and automation (and its tempo after
  // the first, whose tempo is in the header).
  int nsub = subsongCount();
  for (int i = 0; i < nsub; i++) {
    bool live = i == curSubsong;
    const SubSong* sub = live ? nullptr : &subsongs[i];
    if (nsub > 1 || !subsongName.empty()) f << "subsong " << subsongNameAt(i) << '\n';
    if (i > 0) {
      f << "tickrate " << (live ? tickRate : sub->tickRate) << '\n';
      f << "speeds";
      for (int v : (live ? speeds : sub->speeds)) f << ' ' << v;
      f << '\n';
      f << "patlen " << (live ? patternLength : sub->patternLength) << '\n';
    }
    for (const auto& o : (live ? orders : sub->orders)) {
      f << "order";
      for (int c = 0; c < channelCount(); c++) f << ' ' << (int)o[c];
      f << '\n';
    }
    for (int c = 0; c < channelCount(); c++) {
      int cols = channels[c].effectCols;
      const std::vector<Pattern>* pats = live ? &channels[c].patterns : (c < (int)sub->patterns.size() ? &sub->patterns[c] : nullptr);
      if (!pats) continue;
      for (int p = 0; p < (int)pats->size(); p++) {
        const Pattern& pat = (*pats)[p];
        if (!pat.allocated() || pat.isEmpty()) continue;
        f << "pattern " << c << ' ' << p << '\n';
        for (int r = 0; r < MAX_ROWS; r++) {
          const Cell& cell = pat.rows[r];
          if (cell.empty()) continue;
          f << "row " << r << ' ' << cell.note << ' ' << cell.ins << ' ' << cell.vol;
          for (int e = 0; e < cols; e++) f << ' ' << cell.fx[e].cmd << ' ' << cell.fx[e].val;
          f << '\n';
        }
      }
    }
    for (const AutomationLane& lane : (live ? automation : sub->automation)) {
      f << "auto " << lane.target << ' ' << lane.channel << ' ' << lane.fxSlot << ' ' << lane.fxParam << ' ' << lane.on << ' '
        << lane.points.size();
      for (const AutoPoint& p : lane.points) f << ' ' << p.pos << ' ' << p.value;
      f << '\n';
    }
  }
  f << "end\n";
}

bool Song::load(const std::string& path, std::string& err) {
  std::ifstream f(path);
  if (!f) {
    err = "Cannot open " + path;
    return false;
  }
  return read(f, path, err);
}

bool Song::read(std::istream& f, const std::string& path, std::string& err) {
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
  // Subsongs read so far; the one being read lives in s's own fields.
  std::vector<SubSong> doneSubsongs;
  bool inSubsong = false;

  auto clampVal = [](int v, int lo, int hi) { return std::clamp(v, lo, hi); };
  auto clampF = [](float v, float lo, float hi) { return std::isfinite(v) ? std::clamp(v, lo, hi) : lo; };

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
    } else if (key == "chanfx") {
      int c = -1, type = 0, on = 1;
      ss >> c >> type >> on;
      if (c < 0 || c >= s.channelCount() || type < 0 || type >= CFX_COUNT) continue;
      if ((int)s.channels[c].effects.size() >= MAX_CHANNEL_FX) continue;
      ChannelEffect e = ChannelEffect::make(type);
      e.on = on != 0;
      const ChanFxInfo& info = CHAN_FX_INFO[type];
      for (int i = 0; i < 4; i++) {
        float v = e.p[i];
        ss >> v;
        e.p[i] = i < info.count ? clampF(v, info.params[i].min, info.params[i].max) : 0.0f;
      }
      s.channels[c].effects.push_back(e);
    } else if (key == "chansend") {
      int c = -1;
      float rv = 0, dl = 0;
      ss >> c >> rv >> dl;
      if (c >= 0 && c < s.channelCount()) {
        s.channels[c].reverbSend = std::clamp(rv, 0.0f, 1.0f);
        s.channels[c].delaySend = std::clamp(dl, 0.0f, 1.0f);
      }
    } else if (key == "fx.reverb") {
      int on = 0;
      ss >> on >> s.fx.roomSize >> s.fx.damping >> s.fx.reverbLevel;
      s.fx.reverb = on != 0;
      s.fx.roomSize = std::clamp(s.fx.roomSize, 0.0f, 1.0f);
      s.fx.damping = std::clamp(s.fx.damping, 0.0f, 1.0f);
      s.fx.reverbLevel = std::clamp(s.fx.reverbLevel, 0.0f, 2.0f);
    } else if (key == "fx.delay") {
      int on = 0, pp = 1;
      ss >> on >> s.fx.delayRows >> s.fx.delayFeedback >> s.fx.delayLevel >> pp;
      s.fx.delay = on != 0;
      s.fx.pingPong = pp != 0;
      s.fx.delayRows = clampVal(s.fx.delayRows, 1, 64);
      s.fx.delayFeedback = std::clamp(s.fx.delayFeedback, 0.0f, 0.95f);
      s.fx.delayLevel = std::clamp(s.fx.delayLevel, 0.0f, 2.0f);
    } else if (key == "fx.filter") {
      int on = 0;
      ss >> on >> s.fx.filterType >> s.fx.cutoff >> s.fx.resonance;
      s.fx.filter = on != 0;
      s.fx.filterType = clampVal(s.fx.filterType, 0, 2);
      s.fx.cutoff = std::clamp(s.fx.cutoff, 20.0f, 20000.0f);
      s.fx.resonance = std::clamp(s.fx.resonance, 0.0f, 1.0f);
    } else if (key == "fx.volume") {
      ss >> s.fx.volume;
      s.fx.volume = std::clamp(s.fx.volume, 0.0f, 4.0f);
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
      } else if (key == "ins.sid") {
        int on = 0, ring = 0, sync = 0;
        InsFilter& fl = curIns->filter;
        ss >> on >> fl.mode >> fl.cutoff >> fl.resonance >> ring >> sync >> curIns->pulseWidth >> curIns->pwmSweep;
        fl.on = on != 0;
        fl.mode = clampVal(fl.mode, 0, 2);
        fl.cutoff = clampVal(fl.cutoff, 0, 255);
        fl.resonance = clampVal(fl.resonance, 0, 15);
        curIns->ringMod = ring != 0;
        curIns->sync = sync != 0;
        curIns->pulseWidth = curIns->pulseWidth < 0 ? -1 : clampVal(curIns->pulseWidth, 1, 255);
        curIns->pwmSweep = clampVal(curIns->pwmSweep, -64, 64);
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
        curIns->fadeout = clampF(curIns->fadeout, 0.0f, 1.0f);
      } else if (key == "ins.chip") {
        ChipSettings& c = curIns->chip;
        int t = 1, n = 0, e = 0, sn = 0, wh = 1, sh = 0;
        ss >> c.clock >> c.ayModel >> t >> n >> e >> c.ayNoiseFreq >> c.ayEnvShape >> c.ayEnvAuto >> c.ayEnvPeriod >> sn >> wh
           >> c.gbChannel >> c.gbEnvDir >> c.gbEnvLen >> c.gbSweepTime >> c.gbSweepDir >> c.gbSweepShift >> sh;
        c.clock = clampVal(c.clock, 0, 3);
        c.ayModel = clampVal(c.ayModel, 0, 1);
        c.ayTone = t != 0;
        c.ayNoise = n != 0;
        c.ayEnv = e != 0;
        c.ayNoiseFreq = clampVal(c.ayNoiseFreq, 0, 31);
        c.ayEnvShape = clampVal(c.ayEnvShape, 0, 15);
        c.ayEnvAuto = clampVal(c.ayEnvAuto, 0, 5);
        c.ayEnvPeriod = clampVal(c.ayEnvPeriod, 1, 65535);
        c.snNoise = sn != 0;
        c.snWhite = wh != 0;
        c.gbChannel = clampVal(c.gbChannel, 0, 2);
        c.gbEnvDir = clampVal(c.gbEnvDir, 0, 1);
        c.gbEnvLen = clampVal(c.gbEnvLen, 0, 7);
        c.gbSweepTime = clampVal(c.gbSweepTime, 0, 7);
        c.gbSweepDir = clampVal(c.gbSweepDir, 0, 1);
        c.gbSweepShift = clampVal(c.gbSweepShift, 0, 7);
        c.gbShortNoise = sh != 0;
      } else if (key == "ins.chip2") {
        ChipSettings& c = curIns->chip;
        int env = 0, loop = 0, sw = 0, down = 0, sh = 0, pn = 0;
        ss >> c.opnModel >> c.opnLfo >> c.opnAms >> c.opnFms >> c.opnAmOps >> c.nesChannel >> env >> c.nesEnvPeriod >> loop >> sw >>
            c.nesSweepPeriod >> c.nesSweepShift >> down >> sh >> pn;
        c.opnModel = clampVal(c.opnModel, 0, 1);
        c.opnLfo = clampVal(c.opnLfo, 0, 8);
        c.opnAms = clampVal(c.opnAms, 0, 3);
        c.opnFms = clampVal(c.opnFms, 0, 7);
        c.opnAmOps = clampVal(c.opnAmOps, 0, 15);
        c.nesChannel = clampVal(c.nesChannel, 0, 3);
        c.nesEnv = env != 0;
        c.nesEnvPeriod = clampVal(c.nesEnvPeriod, 0, 15);
        c.nesEnvLoop = loop != 0;
        c.nesSweep = sw != 0;
        c.nesSweepPeriod = clampVal(c.nesSweepPeriod, 0, 7);
        c.nesSweepShift = clampVal(c.nesSweepShift, 0, 7);
        c.nesSweepDown = down != 0;
        c.nesShortNoise = sh != 0;
        c.pceNoise = pn != 0;
      } else if (key == "ins.opl") {
        OPLParams& o = curIns->opl;
        int four = 0, am = 0, vib = 0;
        ss >> four >> o.alg >> o.fb >> am >> vib;
        o.fourOp = four != 0;
        o.alg = clampVal(o.alg, 0, o.fourOp ? 3 : 1);
        o.fb = clampVal(o.fb, 0, 7);
        o.deepAm = am != 0;
        o.deepVib = vib != 0;
      } else if (key == "ins.oplop") {
        int k = -1;
        ss >> k;
        if (k < 0 || k > 3) continue;
        OPLOperator& op = curIns->opl.ops[k];
        ss >> op.am >> op.vib >> op.sus >> op.ksr >> op.mult >> op.ksl >> op.tl >> op.ar >> op.dr >> op.sl >> op.rr >> op.ws;
        op.am = clampVal(op.am, 0, 1);
        op.vib = clampVal(op.vib, 0, 1);
        op.sus = clampVal(op.sus, 0, 1);
        op.ksr = clampVal(op.ksr, 0, 1);
        op.mult = clampVal(op.mult, 0, 15);
        op.ksl = clampVal(op.ksl, 0, 3);
        op.tl = clampVal(op.tl, 0, 63);
        op.ar = clampVal(op.ar, 0, 15);
        op.dr = clampVal(op.dr, 0, 15);
        op.sl = clampVal(op.sl, 0, 15);
        op.rr = clampVal(op.rr, 0, 15);
        op.ws = clampVal(op.ws, 0, 7);
      } else if (key == "ins.adsr") {
        int on = 0;
        InsEnvelope& env = curIns->env;
        ss >> on >> env.attack >> env.decay >> env.sustain >> env.release;
        env.on = on != 0;
        env.attack = clampF(env.attack, 0.0f, 20.0f);
        env.decay = clampF(env.decay, 0.0f, 20.0f);
        env.sustain = clampF(env.sustain, 0.0f, 1.0f);
        env.release = clampF(env.release, 0.0f, 20.0f);
      } else if (key == "ins.lfo") {
        int l = -1, on = 0;
        ss >> l;
        if (l < 0 || l >= INS_LFOS) continue;
        InsLfo& lfo = curIns->lfo[l];
        ss >> on >> lfo.target >> lfo.shape >> lfo.rate >> lfo.depth >> lfo.delay;
        lfo.on = on != 0;
        lfo.target = clampVal(lfo.target, 0, LFO_TARGET_COUNT - 1);
        lfo.shape = clampVal(lfo.shape, 0, LFO_SHAPE_COUNT - 1);
        lfo.rate = clampF(lfo.rate, 0.01f, 100.0f);
        lfo.depth = clampF(lfo.depth, 0.0f, 255.0f);
        lfo.delay = clampF(lfo.delay, 0.0f, 20.0f);
      } else if (key == "ins.morph") {
        int on = 0;
        InsMorph& m = curIns->morph;
        ss >> on >> m.time >> m.mode;
        m.on = on != 0;
        m.time = clampF(m.time, 0.01f, 60.0f);
        m.mode = clampVal(m.mode, 0, 2);
        for (int& v : m.table) {
          ss >> v;
          v = clampVal(v, 0, 15);
        }
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
    } else if (key == "subsong") {
      std::string subName = restOfLine(ss);
      if (inSubsong) {
        // The previous subsong is complete: put it aside.
        if ((int)doneSubsongs.size() >= 255) break;
        SubSong done;
        done.name = s.subsongName;
        done.tickRate = s.tickRate;
        done.speeds = s.speeds;
        done.patternLength = s.patternLength;
        done.orders = std::move(s.orders);
        if (done.orders.empty()) done.orders.assign(1, {});
        done.automation = std::move(s.automation);
        done.patterns.resize(s.channels.size());
        for (size_t c = 0; c < s.channels.size(); c++) {
          done.patterns[c] = std::move(s.channels[c].patterns);
          s.channels[c].patterns.assign(MAX_PATTERNS, Pattern());
        }
        doneSubsongs.push_back(std::move(done));
        s.orders.clear();
        s.automation.clear();
        curPat = nullptr;
      }
      inSubsong = true;
      s.subsongName = subName;
    } else if (key == "auto") {
      AutomationLane lane;
      int on = 1, n = 0;
      ss >> lane.target >> lane.channel >> lane.fxSlot >> lane.fxParam >> on >> n;
      if (lane.target < 0 || lane.target >= AUTO_TARGET_COUNT) continue;
      lane.channel = clampVal(lane.channel, 0, MAX_CHANNELS - 1);
      lane.fxSlot = clampVal(lane.fxSlot, 0, MAX_CHANNEL_FX - 1);
      lane.fxParam = clampVal(lane.fxParam, 0, 3);
      lane.on = on != 0;
      n = clampVal(n, 0, 100000);
      for (int k = 0; k < n; k++) {
        AutoPoint p{0, 0};
        if (!(ss >> p.pos >> p.value)) break;
        p.pos = clampF(p.pos, 0.0f, (float)MAX_ORDERS);
        p.value = clampF(p.value, 0.0f, 1.0f);
        lane.points.push_back(p);
      }
      lane.sort();
      if (s.automation.size() < 256) s.automation.push_back(std::move(lane));
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
  // The last subsong read is live; the first one becomes the current one.
  s.subsongs = std::move(doneSubsongs);
  s.subsongs.emplace_back();
  s.curSubsong = (int)s.subsongs.size() - 1;
  s.selectSubsong(0);
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
