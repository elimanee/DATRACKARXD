// Importers for the sample trackers: MOD, S3M, XM and IT.
#include <algorithm>
#include <cmath>
#include <cstring>

#include "import.h"

namespace {

int bcd(int p) { return (p >> 4) * 10 + (p & 15); }

// Volume envelope (ticks, values 0-64) to a volume macro.
Macro envelopeToMacro(const std::vector<std::pair<int, int>>& pts, bool loopOn, int loopStart, int loopEnd, bool susOn,
                      int susStart, int susEnd) {
  Macro m;
  if (pts.empty()) return m;
  int n = (int)pts.size();
  auto tickOf = [&](int i) { return pts[std::clamp(i, 0, n - 1)].first; };
  int last = pts.back().first;
  if (loopOn) last = std::min(last, tickOf(loopEnd));
  last = std::min(last, MAX_MACRO_LEN - 1);
  for (int t = 0; t <= last; t++) {
    int k = 0;
    while (k + 1 < n && pts[k + 1].first <= t) k++;
    int v = pts[k].second;
    if (k + 1 < n && pts[k + 1].first > pts[k].first) {
      double f = (t - pts[k].first) / (double)(pts[k + 1].first - pts[k].first);
      v = (int)std::lround(pts[k].second + (pts[k + 1].second - pts[k].second) * f);
    }
    m.values.push_back(std::clamp(v, 0, 64));
  }
  int len = (int)m.values.size();
  if (loopOn && tickOf(loopStart) < len) m.loop = tickOf(loopStart);
  if (susOn) {
    if (susStart != susEnd && !loopOn && tickOf(susStart) < len) m.loop = tickOf(susStart);
    if (tickOf(susEnd) < len) m.release = tickOf(susEnd);
  }
  if (m.loop >= 0 && m.release >= 0 && m.loop > m.release && !loopOn) m.loop = -1;
  // A flat envelope at full volume adds nothing.
  bool flat = true;
  for (int v : m.values) flat &= v == 64;
  if (flat && m.release < 0) m.values.clear();
  return m;
}

// IT 2.14 / 2.15 sample decompression.
class BitReader {
 public:
  BitReader(const uint8_t* d, size_t n) : d_(d), n_(n) {}
  uint32_t read(int bits) {
    uint32_t v = 0;
    for (int i = 0; i < bits; i++) {
      if (bitPos_ >= n_ * 8) return v;
      v |= ((d_[bitPos_ >> 3] >> (bitPos_ & 7)) & 1u) << i;
      bitPos_++;
    }
    return v;
  }

 private:
  const uint8_t* d_;
  size_t n_;
  size_t bitPos_ = 0;
};

void itDecompress(ByteReader& r, std::vector<float>& out, size_t len, bool is16, bool it215) {
  out.assign(len, 0.0f);
  size_t pos = 0;
  const int maxWidth = is16 ? 17 : 9;
  const size_t blockLen = is16 ? 0x4000 : 0x8000;
  while (pos < len && r.ok(2)) {
    uint16_t packed = r.u16();
    size_t avail = std::min<size_t>(packed, r.left());
    BitReader br(r.ptr(), avail);
    r.skip(packed);
    size_t todo = std::min(blockLen, len - pos);
    int width = maxWidth;
    int32_t d1 = 0, d2 = 0;
    while (todo > 0) {
      if (width < 1 || width > maxWidth) break;
      uint32_t v = br.read(width);
      if (width < 7) {
        if (v == 1u << (width - 1)) {
          int nw = (int)br.read(is16 ? 4 : 3) + 1;
          width = nw < width ? nw : nw + 1;
          continue;
        }
      } else if (width < maxWidth) {
        uint32_t border = ((is16 ? 0xFFFFu : 0xFFu) >> (maxWidth - width)) - (is16 ? 8 : 4);
        if (v > border && v <= border + (is16 ? 16 : 8)) {
          int nw = (int)(v - border);
          width = nw < width ? nw : nw + 1;
          continue;
        }
      } else {
        if (v & (is16 ? 0x10000u : 0x100u)) {
          width = (int)((v + 1) & 0xFF);
          continue;
        }
      }
      int32_t sv;
      int sw = is16 ? 16 : 8;
      if (width < sw) {
        int shift = 32 - width;
        sv = (int32_t)(v << shift) >> shift;
      } else {
        sv = is16 ? (int16_t)(v & 0xFFFF) : (int8_t)(v & 0xFF);
      }
      d1 += sv;
      d2 += d1;
      int32_t s = it215 ? d2 : d1;
      out[pos++] = is16 ? (int16_t)s / 32768.0f : (int8_t)s / 128.0f;
      todo--;
    }
  }
}

void setLoop(Sample& s, int mode, int start, int end) {
  int len = (int)s.data.size();
  start = std::clamp(start, 0, len);
  end = std::clamp(end, start, len);
  if (mode && end - start >= 2) {
    s.loopMode = mode;
    s.loopStart = start;
    s.loopEnd = end;
  } else {
    s.loopMode = 0;
    s.loopStart = 0;
    s.loopEnd = len;
  }
}

// One sample instrument per sample (MOD, S3M, IT sample mode).
void instrumentsFromSamples(Song& song) {
  song.instruments.clear();
  for (size_t i = 0; i < song.samples.size() && i < MAX_INSTRUMENTS; i++) {
    Instrument ins;
    ins.type = INS_SAMPLE;
    ins.sample = (int)i;
    ins.name = song.samples[i].name.empty() ? "Sample " + std::to_string(i + 1) : song.samples[i].name;
    song.instruments.push_back(ins);
  }
}

}  // namespace

// ---------------------------------------------------------------------------
// MOD (ProTracker and friends)
// ---------------------------------------------------------------------------

bool importMOD(const std::vector<uint8_t>& data, Song& song, std::string& err) {
  ByteReader r(data);
  if (data.size() < 600) {
    err = "File too small for a MOD";
    return false;
  }
  std::string sig(data.size() >= 1084 ? std::string((const char*)&data[1080], 4) : "");
  int channels = 0;
  if (sig == "M.K." || sig == "M!K!" || sig == "M&K!" || sig == "FLT4" || sig == "NSMS" || sig == "LARD" || sig == "PATT" ||
      sig == "FEST" || sig == "N.T.")
    channels = 4;
  else if (sig == "FLT8" || sig == "CD81" || sig == "OKTA" || sig == "OCTA")
    channels = 8;
  else if (sig.size() == 4 && isdigit((unsigned char)sig[0]) && sig.substr(1) == "CHN")
    channels = sig[0] - '0';
  else if (sig.size() == 4 && isdigit((unsigned char)sig[0]) && isdigit((unsigned char)sig[1]) && (sig.substr(2) == "CH" || sig.substr(2) == "CN"))
    channels = (sig[0] - '0') * 10 + (sig[1] - '0');
  else if (sig.size() == 4 && sig.substr(0, 3) == "TDZ" && isdigit((unsigned char)sig[3]))
    channels = sig[3] - '0';
  int numSamples = 31;
  if (channels == 0) {
    // Unknown tag: a 31-sample 4-channel layout if the sizes add up, else Soundtracker.
    channels = 4;
    numSamples = 15;
    if (data.size() >= 1084) {
      size_t smpBytes = 0;
      for (int i = 0; i < 31; i++) smpBytes += ((data[20 + i * 30 + 22] << 8) | data[20 + i * 30 + 23]) * 2;
      int maxPat = 0;
      for (int i = 0; i < 128; i++) maxPat = std::max<int>(maxPat, data[952 + i]);
      size_t expect = 1084 + (size_t)(maxPat + 1) * 1024 + smpBytes;
      bool printable = true;
      for (char ch : sig) printable &= ch >= 32 && ch < 127;
      if (printable && expect <= data.size() + 4096 && expect + 65536 >= data.size()) numSamples = 31;
    }
  }
  if (channels < 1 || channels > 32) {
    err = "Unsupported MOD channel count";
    return false;
  }

  song.reset(channels);
  song.instruments.clear();
  song.insResetsVolume = true;
  song.name = r.str(20);
  struct Hdr {
    int len, finetune, vol, loopStart, loopLen;
  };
  std::vector<Hdr> hdr(numSamples);
  for (int i = 0; i < numSamples; i++) {
    Sample s;
    s.name = r.str(22);
    hdr[i].len = r.u16be() * 2;
    hdr[i].finetune = r.u8() & 15;
    if (hdr[i].finetune > 7) hdr[i].finetune -= 16;
    hdr[i].vol = std::min<int>(r.u8(), 64);
    hdr[i].loopStart = r.u16be() * 2;
    hdr[i].loopLen = r.u16be() * 2;
    s.volume = hdr[i].vol;
    s.rate = (int)std::lround(8363.0 * std::pow(2.0, hdr[i].finetune / 96.0));
    song.samples.push_back(s);
  }
  int songLen = std::clamp<int>(r.u8(), 1, 128);
  r.u8();  // restart
  std::vector<int> orderList(128);
  int numPatterns = 0;
  for (int i = 0; i < 128; i++) {
    orderList[i] = r.u8();
    if (i < songLen) numPatterns = std::max(numPatterns, orderList[i] + 1);
    else if (orderList[i] < 128) numPatterns = std::max(numPatterns, orderList[i] + 1);
  }
  if (numSamples == 31) r.skip(4);

  TrackerConverter conv;
  conv.channels = channels;
  conv.slideScale = 1.3;
  conv.effectMemory = false;
  conv.orders.assign(orderList.begin(), orderList.begin() + songLen);
  conv.patterns.resize(numPatterns);
  for (int p = 0; p < numPatterns; p++) {
    auto& pat = conv.patterns[p];
    pat.rows = 64;
    pat.cells.resize(64 * channels);
    for (int row = 0; row < 64; row++)
      for (int c = 0; c < channels; c++) {
        uint8_t b0 = r.u8(), b1 = r.u8(), b2 = r.u8(), b3 = r.u8();
        TrackerCell& tc = pat.at(row, c, channels);
        int period = ((b0 & 15) << 8) | b1;
        int smp = (b0 & 0xF0) | (b2 >> 4);
        if (period > 0) tc.note = std::clamp((int)std::lround(60 + 12 * std::log2(428.0 / period)), 0, 119);
        if (smp > 0) tc.ins = smp - 1;
        int cmd = b2 & 15, prm = b3;
        int x = prm >> 4, y = prm & 15;
        auto fx = [&](TFx t, int p) { tc.fx.push_back({t, p}); };
        switch (cmd) {
          case 0x0: if (prm) fx(TFx::Arp, prm); break;
          case 0x1: fx(TFx::PortaUp, prm); break;
          case 0x2: fx(TFx::PortaDown, prm); break;
          case 0x3: fx(TFx::TonePorta, prm); break;
          case 0x4: fx(TFx::Vibrato, prm); break;
          case 0x5: fx(TFx::PortaVolSlide, prm); break;
          case 0x6: fx(TFx::VibVolSlide, prm); break;
          case 0x7: fx(TFx::Tremolo, prm); break;
          case 0x8: fx(TFx::Pan, prm); break;
          case 0x9: fx(TFx::Offset, prm); break;
          case 0xA: fx(TFx::VolSlide, prm); break;
          case 0xB: fx(TFx::Jump, prm); break;
          case 0xC: fx(TFx::SetVol, prm); break;
          case 0xD: fx(TFx::Break, bcd(prm)); break;
          case 0xE:
            switch (x) {
              case 0x1: fx(TFx::FinePortaUp, y); break;
              case 0x2: fx(TFx::FinePortaDown, y); break;
              case 0x6: fx(TFx::Loop, y); break;
              case 0x8: fx(TFx::Pan, y * 17); break;
              case 0x9: fx(TFx::Retrig, y); break;
              case 0xA: fx(TFx::FineVolUp, y); break;
              case 0xB: fx(TFx::FineVolDown, y); break;
              case 0xC: fx(TFx::Cut, y); break;
              case 0xD: fx(TFx::Delay, y); break;
              case 0xE: fx(TFx::PatternDelay, y); break;
              default: break;
            }
            break;
          case 0xF:
            if (prm > 0 && prm < 32) fx(TFx::Speed, prm);
            else if (prm >= 32) fx(TFx::Tempo, prm);
            break;
        }
      }
  }
  for (int i = 0; i < numSamples; i++) {
    Sample& s = song.samples[i];
    int len = std::min<int>(hdr[i].len, (int)r.left());
    s.data.resize(std::max(len, 0));
    for (int k = 0; k < len; k++) s.data[k] = r.s8() / 128.0f;
    int ls = hdr[i].loopStart, ll = hdr[i].loopLen;
    if (ls + ll > len && ls / 2 + ll <= len) ls /= 2;  // some trackers store the start in bytes
    setLoop(s, ll > 2 ? 1 : 0, ls, ls + ll);
  }
  instrumentsFromSamples(song);
  conv.build(song);
  song.speeds = {6};
  song.tickRate = 50;
  for (int c = 0; c < song.channelCount(); c++) {
    int k = c % 4;
    song.channels[c].pan = (k == 0 || k == 3) ? 64 : 192;
    song.channels[c].name = "Channel " + std::to_string(c + 1);
  }
  return true;
}

// ---------------------------------------------------------------------------
// S3M / IT effect letters (shared)
// ---------------------------------------------------------------------------

namespace {

struct LetterMemory {
  int d = 0, e = 0, f = 0, k = 0, l = 0, q = 0, s = 0;
};

// Decodes a D/K/L volume slide parameter into effects.
void volSlide(TrackerCell& tc, TFx kind, int p) {
  int x = p >> 4, y = p & 15;
  if (kind == TFx::VolSlide) {
    if (y == 0xF && x) {
      tc.fx.push_back({TFx::FineVolUp, x});
      return;
    }
    if (x == 0xF && y) {
      tc.fx.push_back({TFx::FineVolDown, y});
      return;
    }
  }
  int prm = y ? y : (x << 4);
  if (kind != TFx::VolSlide || prm) tc.fx.push_back({kind, prm});
}

void letterEffect(TrackerCell& tc, int cmd, int p, LetterMemory& m, bool isIT) {
  int x = p >> 4, y = p & 15;
  auto fx = [&](TFx t, int v) { tc.fx.push_back({t, v}); };
  auto recall = [&](int& mem) {
    if (p) mem = p;
    return mem;
  };
  switch (cmd) {
    case 1: if (p) fx(TFx::Speed, p); break;                    // A
    case 2: fx(TFx::Jump, p); break;                            // B
    case 3: fx(TFx::Break, isIT ? p : bcd(p)); break;           // C
    case 4: volSlide(tc, TFx::VolSlide, recall(m.d)); break;    // D
    case 5:                                                     // E
    case 6: {                                                   // F
      int v = recall(cmd == 5 ? m.e : m.f);
      bool up = cmd == 6;
      if (v >= 0xF0) fx(up ? TFx::FinePortaUp : TFx::FinePortaDown, v & 15);
      else if (v >= 0xE0) fx(up ? TFx::ExtraFinePortaUp : TFx::ExtraFinePortaDown, v & 15);
      else fx(up ? TFx::PortaUp : TFx::PortaDown, v);
      break;
    }
    case 7: fx(TFx::TonePorta, p); break;                       // G
    case 8: fx(TFx::Vibrato, p); break;                         // H
    case 10: fx(TFx::Arp, p); break;                            // J
    case 11: volSlide(tc, TFx::VibVolSlide, recall(m.k)); break;   // K
    case 12: volSlide(tc, TFx::PortaVolSlide, recall(m.l)); break; // L
    case 15: fx(TFx::Offset, p); break;                         // O
    case 17: fx(TFx::Retrig, recall(m.q) & 15); break;          // Q
    case 18: fx(TFx::Tremolo, p); break;                        // R
    case 19: {                                                  // S
      int v = recall(m.s);
      x = v >> 4;
      y = v & 15;
      switch (x) {
        case 0x8: fx(TFx::Pan, y * 17); break;
        case 0xB: fx(TFx::Loop, y); break;
        case 0xC: fx(TFx::Cut, y); break;
        case 0xD: fx(TFx::Delay, y); break;
        case 0xE: fx(TFx::PatternDelay, y); break;
        default: break;
      }
      break;
    }
    case 20: if (p >= 0x20) fx(TFx::Tempo, p); break;           // T
    case 21: fx(TFx::Vibrato, (x << 4) | ((y + 3) / 4)); break; // U (fine vibrato)
    case 24: fx(TFx::Pan, isIT ? p : std::min(255, p * 2)); break;  // X
    default: break;
  }
}

}  // namespace

// ---------------------------------------------------------------------------
// S3M (Scream Tracker 3)
// ---------------------------------------------------------------------------

bool importS3M(const std::vector<uint8_t>& data, Song& song, std::string& err) {
  ByteReader r(data);
  std::string title = r.str(28);
  r.seek(0x20);
  int ordNum = r.u16(), insNum = r.u16(), patNum = r.u16();
  r.u16();  // flags
  r.u16();  // tracker version
  int ffi = r.u16();
  r.seek(0x31);
  int initSpeed = r.u8(), initTempo = r.u8();
  r.u8();  // master volume
  r.u8();
  int defaultPan = r.u8();
  r.seek(0x40);
  int chanSet[32];
  for (int& c : chanSet) c = r.u8();
  std::vector<int> orders;
  for (int i = 0; i < ordNum; i++) {
    int o = r.u8();
    if (o == 255) break;
    if (o < 254) orders.push_back(o);
  }
  r.seek(0x60 + ordNum);
  std::vector<int> insPtr(insNum), patPtr(patNum);
  for (int& p : insPtr) p = r.u16() * 16;
  for (int& p : patPtr) p = r.u16() * 16;
  int panTable[32];
  for (int& p : panTable) p = defaultPan == 252 ? r.u8() : 0;

  // Enabled channels, in order.
  std::vector<int> chanMap(32, -1);
  int nch = 0;
  for (int c = 0; c < 32; c++)
    if (chanSet[c] < 16 && nch < MAX_CHANNELS) chanMap[c] = nch++;
  if (nch == 0) {
    err = "S3M has no enabled channels";
    return false;
  }

  song.reset(nch);
  song.instruments.clear();
  song.name = title;
  song.insResetsVolume = true;
  for (int c = 0; c < 32; c++) {
    int o = chanMap[c];
    if (o < 0) continue;
    int pan = chanSet[c] < 8 ? 64 : 192;
    if (panTable[c] & 0x20) pan = (panTable[c] & 15) * 17;
    song.channels[o].pan = pan;
    song.channels[o].name = "Channel " + std::to_string(o + 1);
  }

  for (int i = 0; i < insNum && i < MAX_SAMPLES; i++) {
    Sample s;
    r.seek(insPtr[i]);
    int type = r.u8();
    r.skip(12);
    int memHi = r.u8();
    int memLo = r.u16();
    int len = r.u32(), loopBeg = r.u32(), loopEnd = r.u32();
    s.volume = std::min<int>(r.u8(), 64);
    r.u8();
    int pack = r.u8(), flags = r.u8();
    s.rate = std::max<int>(r.u32(), 1);
    r.skip(12);
    s.name = r.str(28);
    if (type == 1 && pack == 0 && len > 0) {
      bool is16 = flags & 4, stereo = flags & 2;
      size_t off = (size_t)((memHi << 16) | memLo) * 16;
      int bps = is16 ? 2 : 1;
      ByteReader sr(data);
      sr.seek(off);
      len = std::min<int>(len, (int)(sr.left() / bps));
      s.data.resize(len);
      for (int k = 0; k < len; k++) {
        float v;
        if (is16) {
          uint16_t raw = sr.u16();
          v = ffi == 2 ? (raw - 32768) / 32768.0f : (int16_t)raw / 32768.0f;
        } else {
          uint8_t raw = sr.u8();
          v = ffi == 2 ? (raw - 128) / 128.0f : (int8_t)raw / 128.0f;
        }
        s.data[k] = v;
      }
      (void)stereo;  // the left channel is enough
      setLoop(s, (flags & 1) ? 1 : 0, loopBeg, loopEnd);
    }
    song.samples.push_back(std::move(s));
  }

  TrackerConverter conv;
  conv.channels = nch;
  conv.slideScale = 1.3;
  conv.effectMemory = true;
  conv.orders = orders;
  conv.patterns.resize(patNum);
  std::vector<LetterMemory> mem(32);
  for (int p = 0; p < patNum; p++) {
    auto& pat = conv.patterns[p];
    pat.rows = 64;
    pat.cells.resize(64 * nch);
    if (!patPtr[p]) continue;
    r.seek(patPtr[p]);
    r.u16();  // packed length
    for (int row = 0; row < 64 && !r.eof();) {
      int what = r.u8();
      if (what == 0) {
        row++;
        continue;
      }
      int c = what & 31;
      TrackerCell dummy;
      TrackerCell& tc = chanMap[c] >= 0 ? pat.at(row, chanMap[c], nch) : dummy;
      if (what & 32) {
        int note = r.u8(), ins = r.u8();
        if (note == 254) tc.note = NOTE_OFF;
        else if (note < 254) tc.note = std::clamp((note >> 4) * 12 + (note & 15) + 12, 0, 119);
        if (ins > 0) tc.ins = ins - 1;
      }
      if (what & 64) tc.vol = std::min<int>(r.u8(), 64);
      if (what & 128) {
        int cmd = r.u8(), info = r.u8();
        letterEffect(tc, cmd, info, mem[c], false);
      }
    }
  }
  instrumentsFromSamples(song);
  conv.build(song);
  song.speeds = {std::max(initSpeed, 1)};
  song.tickRate = std::max(initTempo, 32) * 2.0f / 5.0f;
  return true;
}

// ---------------------------------------------------------------------------
// XM (FastTracker 2)
// ---------------------------------------------------------------------------

bool importXM(const std::vector<uint8_t>& data, Song& song, std::string& err) {
  ByteReader r(data);
  r.seek(17);
  std::string title = r.str(20);
  r.seek(58);
  int version = r.u16();
  uint32_t hdrSize = r.u32();
  int songLen = r.u16();
  r.u16();  // restart position
  int channels = r.u16(), numPat = r.u16(), numIns = r.u16(), flags = r.u16();
  int speed = r.u16(), bpm = r.u16();
  std::vector<int> orders;
  for (int i = 0; i < 256; i++) {
    int o = r.u8();
    if (i < songLen) orders.push_back(o);
  }
  if (version < 0x0104) {
    err = "Old XM versions (before 1.04) are not supported";
    return false;
  }
  if (channels < 1 || channels > 64) {
    err = "Bad XM channel count";
    return false;
  }
  int nch = std::min(channels, MAX_CHANNELS);
  song.reset(nch);
  song.instruments.clear();
  song.name = title;
  song.insResetsVolume = true;

  TrackerConverter conv;
  conv.channels = nch;
  conv.slideScale = (flags & 1) ? 2.0 : 1.3;
  conv.effectMemory = true;
  conv.orders = orders;
  conv.patterns.resize(numPat);
  r.seek(60 + hdrSize);
  for (int p = 0; p < numPat; p++) {
    size_t start = r.pos();
    uint32_t len = r.u32();
    r.u8();  // packing
    int rows = r.u16();
    int packed = r.u16();
    r.seek(start + len);
    size_t end = r.pos() + packed;
    auto& pat = conv.patterns[p];
    pat.rows = std::clamp(rows, 1, MAX_ROWS);
    pat.cells.resize(pat.rows * nch);
    if (packed == 0) continue;
    for (int row = 0; row < rows; row++)
      for (int c = 0; c < channels; c++) {
        int note = 0, ins = 0, vol = 0, cmd = 0, prm = 0;
        int b = r.u8();
        if (b & 0x80) {
          if (b & 1) note = r.u8();
          if (b & 2) ins = r.u8();
          if (b & 4) vol = r.u8();
          if (b & 8) cmd = r.u8();
          if (b & 16) prm = r.u8();
        } else {
          note = b;
          ins = r.u8();
          vol = r.u8();
          cmd = r.u8();
          prm = r.u8();
        }
        if (c >= nch || row >= pat.rows) continue;
        TrackerCell& tc = pat.at(row, c, nch);
        if (note == 97) tc.note = NOTE_OFF;
        else if (note >= 1 && note <= 96) tc.note = note - 1 + 12;
        if (ins > 0) tc.ins = ins - 1;
        auto fx = [&](TFx t, int v) { tc.fx.push_back({t, v}); };
        int vx = vol >> 4, vy = vol & 15;
        if (vol >= 0x10 && vol <= 0x50) tc.vol = vol - 0x10;
        else if (vx == 0x6) fx(TFx::VolSlide, vy);
        else if (vx == 0x7) fx(TFx::VolSlide, vy << 4);
        else if (vx == 0x8) fx(TFx::FineVolDown, vy);
        else if (vx == 0x9) fx(TFx::FineVolUp, vy);
        else if (vx == 0xA) fx(TFx::Vibrato, vy << 4);
        else if (vx == 0xB) fx(TFx::Vibrato, vy);
        else if (vx == 0xC) fx(TFx::Pan, vy * 17);
        else if (vx == 0xF) fx(TFx::TonePorta, vy * 16);
        int x = prm >> 4, y = prm & 15;
        switch (cmd) {
          case 0x0: if (prm) fx(TFx::Arp, prm); break;
          case 0x1: fx(TFx::PortaUp, prm); break;
          case 0x2: fx(TFx::PortaDown, prm); break;
          case 0x3: fx(TFx::TonePorta, prm); break;
          case 0x4: fx(TFx::Vibrato, prm); break;
          case 0x5: fx(TFx::PortaVolSlide, prm); break;
          case 0x6: fx(TFx::VibVolSlide, prm); break;
          case 0x7: fx(TFx::Tremolo, prm); break;
          case 0x8: fx(TFx::Pan, prm); break;
          case 0x9: fx(TFx::Offset, prm); break;
          case 0xA: fx(TFx::VolSlide, prm); break;
          case 0xB: fx(TFx::Jump, prm); break;
          case 0xC: fx(TFx::SetVol, prm); break;
          case 0xD: fx(TFx::Break, bcd(prm)); break;
          case 0xE:
            switch (x) {
              case 0x1: fx(TFx::FinePortaUp, y); break;
              case 0x2: fx(TFx::FinePortaDown, y); break;
              case 0x6: fx(TFx::Loop, y); break;
              case 0x8: fx(TFx::Pan, y * 17); break;
              case 0x9: fx(TFx::Retrig, y); break;
              case 0xA: fx(TFx::FineVolUp, y); break;
              case 0xB: fx(TFx::FineVolDown, y); break;
              case 0xC: fx(TFx::Cut, y); break;
              case 0xD: fx(TFx::Delay, y); break;
              case 0xE: fx(TFx::PatternDelay, y); break;
              default: break;
            }
            break;
          case 0xF:
            if (prm > 0 && prm < 32) fx(TFx::Speed, prm);
            else if (prm >= 32) fx(TFx::Tempo, prm);
            break;
          case 20: fx(TFx::KeyOff, prm); break;          // K
          case 27: if (y) fx(TFx::Retrig, y); break;     // R
          case 33:                                       // X
            if (x == 1) fx(TFx::ExtraFinePortaUp, y);
            if (x == 2) fx(TFx::ExtraFinePortaDown, y);
            break;
          default: break;
        }
      }
    r.seek(end);
  }

  // Instruments: each owns some samples and maps notes to them.
  for (int i = 0; i < numIns; i++) {
    size_t start = r.pos();
    uint32_t insHdr = r.u32();
    Instrument ins;
    ins.type = INS_SAMPLE;
    ins.name = r.str(22);
    r.u8();  // type
    int numSmp = r.u16();
    int baseSample = (int)song.samples.size();
    if (numSmp > 0) {
      r.u32();  // sample header size
      uint8_t keymap[96];
      for (auto& k : keymap) k = r.u8();
      std::vector<std::pair<int, int>> volPts(12);
      for (auto& pt : volPts) {
        pt.first = r.u16();
        pt.second = r.u16();
      }
      r.skip(48);  // panning envelope
      int numVol = r.u8();
      r.u8();
      int volSus = r.u8(), volLoopS = r.u8(), volLoopE = r.u8();
      r.skip(3);
      int volType = r.u8();
      r.u8();
      r.skip(4);  // auto-vibrato
      int fadeout = r.u16();
      r.seek(start + insHdr);

      if ((volType & 1) && numVol > 0) {
        volPts.resize(std::min(numVol, 12));
        ins.macros[MACRO_VOL] = envelopeToMacro(volPts, volType & 4, volLoopS, volLoopE, volType & 2, volSus, volSus);
        ins.fadeout = fadeout / 32768.0f;
        if (ins.fadeout <= 0 && ins.macros[MACRO_VOL].release < 0) ins.fadeout = 0.0001f;  // keep ringing after note off
      }
      struct SH {
        int len, loopStart, loopLen, type;
        bool is16;
      };
      std::vector<SH> sh(numSmp);
      for (int k = 0; k < numSmp; k++) {
        Sample s;
        sh[k].len = r.u32();
        sh[k].loopStart = r.u32();
        sh[k].loopLen = r.u32();
        s.volume = std::min<int>(r.u8(), 64);
        int finetune = r.s8();
        int type = r.u8();
        r.u8();  // panning
        int rel = r.s8();
        r.u8();
        s.name = r.str(22);
        sh[k].type = type & 3;
        sh[k].is16 = type & 16;
        s.rate = (int)std::lround(8363.0 * std::pow(2.0, (rel + finetune / 128.0) / 12.0));
        if ((int)song.samples.size() < MAX_SAMPLES) song.samples.push_back(s);
      }
      for (int k = 0; k < numSmp; k++) {
        int bytes = std::min<int>(sh[k].len, (int)r.left());
        int idx = baseSample + k;
        if (idx >= (int)song.samples.size()) {
          r.skip(bytes);
          continue;
        }
        Sample& s = song.samples[idx];
        int div = sh[k].is16 ? 2 : 1;
        int n = bytes / div;
        s.data.resize(n);
        int old = 0;
        for (int j = 0; j < n; j++) {
          if (sh[k].is16) {
            old = (int16_t)(old + r.s16());
            s.data[j] = old / 32768.0f;
          } else {
            old = (int8_t)(old + r.s8());
            s.data[j] = old / 128.0f;
          }
        }
        if (bytes % div) r.skip(1);
        int mode = sh[k].type == 1 ? 1 : sh[k].type == 2 ? 2 : 0;
        setLoop(s, mode, sh[k].loopStart / div, (sh[k].loopStart + sh[k].loopLen) / div);
      }
      ins.sample = baseSample < (int)song.samples.size() ? baseSample : -1;
      bool multi = false;
      for (uint8_t k : keymap) multi |= k != 0;
      if (multi) {
        ins.sampleMap.assign(NOTE_COUNT, -1);
        for (int n = 0; n < 96; n++) {
          int idx = baseSample + keymap[n];
          if (idx < (int)song.samples.size() && keymap[n] < numSmp) ins.sampleMap[n + 12] = (int16_t)idx;
        }
      }
    } else {
      r.seek(start + insHdr);
    }
    if ((int)song.instruments.size() < MAX_INSTRUMENTS) song.instruments.push_back(std::move(ins));
  }

  conv.build(song);
  song.speeds = {std::max(speed, 1)};
  song.tickRate = std::max(bpm, 32) * 2.0f / 5.0f;
  for (int c = 0; c < song.channelCount(); c++) song.channels[c].name = "Channel " + std::to_string(c + 1);
  return true;
}

// ---------------------------------------------------------------------------
// IT (Impulse Tracker)
// ---------------------------------------------------------------------------

bool importIT(const std::vector<uint8_t>& data, Song& song, std::string& err) {
  if (data.size() < 0xC0) {
    err = "File too small for an IT module";
    return false;
  }
  ByteReader r(data);
  r.seek(4);
  std::string title = r.str(26);
  r.seek(0x20);
  int ordNum = r.u16(), insNum = r.u16(), smpNum = r.u16(), patNum = r.u16();
  r.u16();  // created with
  int cmwt = r.u16();
  int flags = r.u16();
  r.u16();  // special
  r.u8();   // global volume
  r.u8();   // mix volume
  int initSpeed = r.u8(), initTempo = r.u8();
  r.seek(0x40);
  int chanPan[64];
  for (int& p : chanPan) p = r.u8();
  r.seek(0xC0);
  std::vector<int> orders;
  for (int i = 0; i < ordNum; i++) {
    int o = r.u8();
    if (o == 255) break;
    if (o < 254) orders.push_back(o);
  }
  r.seek(0xC0 + ordNum);
  std::vector<uint32_t> insOff(insNum), smpOff(smpNum), patOff(patNum);
  for (auto& o : insOff) o = r.u32();
  for (auto& o : smpOff) o = r.u32();
  for (auto& o : patOff) o = r.u32();
  bool useInstruments = flags & 4;

  // First pass over patterns: find used channels.
  int maxCh = 0;
  struct RawCell {
    int note = -1, ins = 0, vol = -1, cmd = 0, prm = 0;
  };
  std::vector<std::vector<RawCell>> raw(patNum);
  std::vector<int> patRows(patNum, 64);
  for (int p = 0; p < patNum; p++) {
    if (!patOff[p]) continue;
    r.seek(patOff[p]);
    int len = r.u16();
    int rows = r.u16();
    r.skip(4);
    patRows[p] = std::clamp(rows, 1, MAX_ROWS);
    raw[p].assign(patRows[p] * 64, RawCell());
    size_t end = r.pos() + len;
    int lastMask[64] = {}, lastNote[64], lastIns[64] = {}, lastVol[64], lastCmd[64] = {}, lastPrm[64] = {};
    std::fill(std::begin(lastNote), std::end(lastNote), -1);
    std::fill(std::begin(lastVol), std::end(lastVol), -1);
    for (int row = 0; row < rows && r.pos() < end;) {
      int cv = r.u8();
      if (cv == 0) {
        row++;
        continue;
      }
      int c = (cv - 1) & 63;
      int mask = (cv & 128) ? (lastMask[c] = r.u8()) : lastMask[c];
      RawCell cell;
      if (mask & 1) lastNote[c] = cell.note = r.u8();
      if (mask & 2) lastIns[c] = cell.ins = r.u8();
      if (mask & 4) lastVol[c] = cell.vol = r.u8();
      if (mask & 8) {
        lastCmd[c] = cell.cmd = r.u8();
        lastPrm[c] = cell.prm = r.u8();
      }
      if (mask & 16) cell.note = lastNote[c];
      if (mask & 32) cell.ins = lastIns[c];
      if (mask & 64) cell.vol = lastVol[c];
      if (mask & 128) {
        cell.cmd = lastCmd[c];
        cell.prm = lastPrm[c];
      }
      if (row < patRows[p]) raw[p][row * 64 + c] = cell;
      maxCh = std::max(maxCh, c + 1);
    }
  }
  int nch = std::clamp(maxCh, 1, MAX_CHANNELS);
  song.reset(nch);
  song.instruments.clear();
  song.name = title;
  song.insResetsVolume = true;
  for (int c = 0; c < nch; c++) {
    int pan = chanPan[c] & 127;
    song.channels[c].pan = pan <= 64 ? std::min(255, pan * 4) : 128;
    song.channels[c].muted = chanPan[c] & 128;
    song.channels[c].name = "Channel " + std::to_string(c + 1);
  }

  // Samples.
  for (int i = 0; i < smpNum && i < MAX_SAMPLES; i++) {
    Sample s;
    r.seek(smpOff[i]);
    r.skip(4 + 12 + 1);
    int gvl = r.u8(), flg = r.u8(), vol = r.u8();
    s.name = r.str(26);
    int cvt = r.u8();
    r.u8();  // default pan
    int len = r.u32(), loopBeg = r.u32(), loopEnd = r.u32();
    s.rate = std::max<int>(r.u32(), 1);
    int susBeg = r.u32(), susEnd = r.u32();
    uint32_t ptr = r.u32();
    s.volume = std::min(64, std::min(vol, 64) * std::min(gvl, 64) / 64);
    if ((flg & 1) && len > 0) {
      bool is16 = flg & 2, compressed = flg & 8, isSigned = cvt & 1;
      ByteReader sr(data);
      sr.seek(ptr);
      if (compressed) {
        itDecompress(sr, s.data, std::min<size_t>(len, 1 << 24), is16, cvt & 4);
      } else {
        int bps = is16 ? 2 : 1;
        len = std::min<int>(len, (int)(sr.left() / bps));
        s.data.resize(len);
        for (int k = 0; k < len; k++) {
          if (is16) {
            uint16_t v = sr.u16();
            s.data[k] = isSigned ? (int16_t)v / 32768.0f : (v - 32768) / 32768.0f;
          } else {
            uint8_t v = sr.u8();
            s.data[k] = isSigned ? (int8_t)v / 128.0f : (v - 128) / 128.0f;
          }
        }
      }
      if (flg & 16) setLoop(s, (flg & 64) ? 2 : 1, loopBeg, loopEnd);
      else if (flg & 32) setLoop(s, (flg & 128) ? 2 : 1, susBeg, susEnd);
      else setLoop(s, 0, 0, 0);
    }
    song.samples.push_back(std::move(s));
  }

  // Instruments.
  if (useInstruments) {
    for (int i = 0; i < insNum && i < MAX_INSTRUMENTS; i++) {
      Instrument ins;
      ins.type = INS_SAMPLE;
      r.seek(insOff[i]);
      r.skip(4 + 12 + 1);
      bool oldFormat = cmwt < 0x200;
      int fadeout;
      if (oldFormat) {
        r.skip(7);
        fadeout = r.u16() * 2;  // old instruments use half the range
      } else {
        r.skip(3);
        fadeout = r.u16();
      }
      r.seek(insOff[i] + 0x20);
      ins.name = r.str(26);
      r.seek(insOff[i] + 0x40);
      ins.sampleMap.assign(NOTE_COUNT, -1);
      int firstSample = -1;
      for (int n = 0; n < 120; n++) {
        r.u8();  // note transpose (ignored)
        int smp = r.u8();
        if (smp > 0 && smp <= (int)song.samples.size()) {
          ins.sampleMap[n] = (int16_t)(smp - 1);
          if (firstSample < 0) firstSample = smp - 1;
        }
      }
      ins.sample = firstSample;
      if (!oldFormat) {
        r.seek(insOff[i] + 0x130);
        int envFlg = r.u8(), num = r.u8(), lpb = r.u8(), lpe = r.u8(), slb = r.u8(), sle = r.u8();
        std::vector<std::pair<int, int>> pts;
        for (int k = 0; k < 25; k++) {
          int y = r.u8();
          int tick = r.u16();
          if (k < num) pts.push_back({tick, y});
        }
        if ((envFlg & 1) && !pts.empty())
          ins.macros[MACRO_VOL] = envelopeToMacro(pts, envFlg & 2, lpb, lpe, envFlg & 4, slb, sle);
      }
      ins.fadeout = std::min(fadeout, 1024) / 1024.0f;
      song.instruments.push_back(std::move(ins));
    }
  } else {
    instrumentsFromSamples(song);
  }

  TrackerConverter conv;
  conv.channels = nch;
  conv.slideScale = (flags & 8) ? 2.0 : 1.3;
  conv.effectMemory = true;
  conv.orders = orders;
  conv.patterns.resize(patNum);
  std::vector<LetterMemory> mem(64);
  static const int portaTable[10] = {0, 1, 4, 8, 16, 32, 64, 96, 128, 255};
  for (int p = 0; p < patNum; p++) {
    auto& pat = conv.patterns[p];
    pat.rows = patRows[p];
    pat.cells.resize(pat.rows * nch);
    if (raw[p].empty()) continue;
    for (int row = 0; row < pat.rows; row++)
      for (int c = 0; c < nch; c++) {
        const RawCell& rc = raw[p][row * 64 + c];
        TrackerCell& tc = pat.at(row, c, nch);
        if (rc.note >= 0 && rc.note <= 119) tc.note = rc.note;
        else if (rc.note >= 120) tc.note = NOTE_OFF;
        if (rc.ins > 0) tc.ins = rc.ins - 1;
        int v = rc.vol;
        auto fx = [&](TFx t, int x) { tc.fx.push_back({t, x}); };
        if (v >= 0 && v <= 64) tc.vol = v;
        else if (v >= 65 && v <= 74) fx(TFx::FineVolUp, v - 65);
        else if (v >= 75 && v <= 84) fx(TFx::FineVolDown, v - 75);
        else if (v >= 85 && v <= 94) fx(TFx::VolSlide, (v - 85) << 4);
        else if (v >= 95 && v <= 104) fx(TFx::VolSlide, v - 95);
        else if (v >= 105 && v <= 114) fx(TFx::PortaDown, (v - 105) * 4);
        else if (v >= 115 && v <= 124) fx(TFx::PortaUp, (v - 115) * 4);
        else if (v >= 128 && v <= 192) fx(TFx::Pan, std::min(255, (v - 128) * 4));
        else if (v >= 193 && v <= 202) fx(TFx::TonePorta, portaTable[v - 193]);
        else if (v >= 203 && v <= 212) fx(TFx::Vibrato, v - 203);
        if (rc.cmd) letterEffect(tc, rc.cmd, rc.prm, mem[c], true);
      }
  }
  conv.build(song);
  song.speeds = {std::max(initSpeed, 1)};
  song.tickRate = std::max(initTempo, 32) * 2.0f / 5.0f;
  return true;
}
