// FamiTracker (.ftm, also Dn-FamiTracker .dnm and 0CC .0cc) importer: the
// 2A03 and the VRC6, VRC7, FDS, MMC5, N163 and Sunsoft 5B expansions. The
// module is read into Furnace terms (as Furnace does) and converted from
// there; DPCM samples become sample instruments at the right pitches.
#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <map>

#include "fur_internal.h"
#include "import.h"

using namespace fur;

namespace {

// FamiTracker effect number -> Furnace effect code (-1 = not supported).
const int FT_EFFECTS[] = {
    -1,   0x0f, 0x0b, 0x0d, 0xff, -1,   0x03, 0x03, 0x13, 0x14, 0x00, 0x04, 0x07, 0xe5, 0xed, 0x11,
    0x01, 0x02, 0x12, 0x90, 0xe1, 0xe2, 0x0a, 0xec, 0x0c, -1,   0x11, 0x12, 0x13, 0x20, 0x22, 0x24,
    0x23, 0x21, -1,   -1,   0xfc, 0x09, 0xe6, 0x1a, -1,   -1,   -1,   -1,   -1,
};
const int FT_EFFECT_COUNT = sizeof(FT_EFFECTS) / sizeof(int);
enum { FT_EF_PORTAMENTO = 6, FT_EF_PORTAOFF = 7, FT_EF_SAMPLE_OFFSET = 19, FT_EF_N163_WAVE_BUFFER = 39 };

// Effect numbers moved around between FamiTracker 0.4.x and 0.5 (0CC order).
const int FT_050_REMAP[][2] = {{33, 36}, {34, 37}, {35, 38}, {36, 39}, {37, 40}, {38, 41}, {39, 33}, {40, 34}, {41, 35}};

// DPCM playback rates (NTSC) for the 16 pitch settings.
const double DPCM_RATES[16] = {4181.71, 4709.93, 5264.04, 5593.04, 6257.95, 7046.35, 7919.35, 8363.42,
                               9419.86, 11186.1, 12604.0, 13982.6, 16884.6, 21306.8, 24858.0, 33143.9};

struct Sequence {
  std::vector<int> values;
  int loop = -1, release = -1, setting = 0;
};

// Sequence tables per chip: 0 = 2A03/MMC5, 1 = VRC6, 2 = N163, 3 = 5B.
using SeqTable = std::map<std::pair<int, int>, Sequence>;  // (index, type)

struct FtIns {
  int chip = 0;  // sequence table
  bool used = false;
  bool hasSeq[5] = {};
  int seqIndex[5] = {};
  FurIns fur;
  // DPCM: note -> (sample, pitch, loop)
  struct Dpcm {
    int sample = -1, pitch = 15;
    bool loop = false;
  };
  std::map<int, Dpcm> dpcm;
  int waveOff = -1, waveCount = 0;  // N163 / FDS waves in the module list
};

std::string lenStr(ByteReader& r) {
  uint32_t n = r.u32();
  return r.str(std::min<uint32_t>(n, 4096));
}

void readSequences(ByteReader& r, int blockVersion, bool alwaysSettings, SeqTable& table) {
  uint32_t count = std::min<uint32_t>(r.u32(), 128 * 5);
  std::vector<std::pair<int, int>> keys;
  for (uint32_t i = 0; i < count && !r.eof(); i++) {
    int index = (int)r.u32(), type = (int)r.u32();
    int size = r.u8();
    Sequence s;
    s.loop = (int)r.u32();
    if (alwaysSettings || blockVersion == 4) {
      s.release = (int)r.u32();
      s.setting = (int)r.u32();
    }
    s.values.resize(size);
    for (int& v : s.values) v = r.u8();
    keys.push_back({index, type});
    table[{index, type}] = s;
  }
  if (alwaysSettings) return;
  if (blockVersion == 5) {
    // Release points and settings for every (index, type), stored apart.
    for (int i = 0; i < 128; i++)
      for (int j = 0; j < 5; j++) {
        int release = (int)r.u32(), setting = (int)r.u32();
        auto it = table.find({i, j});
        if (it != table.end()) {
          it->second.release = release;
          it->second.setting = setting;
        }
      }
  } else if (blockVersion >= 6) {
    for (auto& k : keys) {
      int release = (int)r.u32(), setting = (int)r.u32();
      table[k].release = release;
      table[k].setting = setting;
    }
  }
}

// FamiTracker sequence -> Furnace macro.
void applySequence(FurIns& ins, int type, const Sequence& s, int insType, int waveOff) {
  FurMacro m;
  int n = (int)s.values.size();
  m.loop = s.loop >= 0 && s.loop < n ? s.loop : -1;
  m.release = s.release >= 0 && s.release < n ? s.release : -1;
  m.values = s.values;
  int target;
  switch (type) {
    case 0: target = 0; break;  // volume
    case 1: {                   // arpeggio
      target = 1;
      if (s.setting == 1) {     // fixed notes
        for (int& v : m.values) v |= 1 << 30;
        if (m.loop < 0) m.values.push_back(0);
      } else {
        for (int& v : m.values) v = (int8_t)v;
        if (s.setting == 2) {   // relative: adds up
          int acc = 0;
          for (int& v : m.values) v = acc += v;
        }
      }
      break;
    }
    case 2:  // pitch: positive is down in FamiTracker
      target = 4;
      for (int& v : m.values) v = insType == 17 ? (int8_t)v : -(int8_t)v;
      m.mode = s.setting == 0 ? 1 : 0;
      break;
    case 4:
      if (insType == 17) {  // N163 wave
        target = 3;
        for (int& v : m.values) v += std::max(waveOff, 0);
      } else if (insType == 6) {
        return;  // 5B noise/mode
      } else {
        target = 2;  // duty
      }
      break;
    default: return;  // hi-pitch
  }
  ins.macros[target] = m;
}

}  // namespace

bool importFTM(const std::vector<uint8_t>& d, Song& song, std::string& err) {
  ByteReader r(d);
  size_t hdr;
  bool dn = false;
  if (d.size() > 22 && std::memcmp(d.data(), "Dn-FamiTracker Module", 21) == 0) {
    hdr = 21;
    dn = true;
  } else if (d.size() > 22 && std::memcmp(d.data(), "0CC-FamiTracker Module", 22) == 0) {
    hdr = 22;
  } else if (d.size() > 22 && std::memcmp(d.data(), "FamiTracker Module", 18) == 0) {
    hdr = 18;
  } else {
    err = "Not a FamiTracker module";
    return false;
  }
  r.seek(hdr);
  int version = (int)r.u32();
  if (version < 0x0200 || version > 0x0450) {
    err = "Unsupported FamiTracker version";
    return false;
  }

  FurModule m;
  int expansions = 0, fileChans = 5, n163Chans = 0, pal = 0;
  double hz = 0;
  int hlA = 4, hlB = 16;
  std::vector<int> mapCh;  // file channel -> module channel (-1 = dropped)
  int fdsCh = -1, sawCh = -1;
  std::vector<int> n163Ch;
  std::array<FtIns, 128> ins;
  std::array<SeqTable, 4> seqs;
  std::vector<std::vector<uint8_t>> dpcmData(256);
  std::vector<std::string> dpcmNames(256);
  int speed = 6, tempo = 150, patLen = 64, frames = 1;
  std::vector<std::vector<int>> frameTable;  // [module channel][frame]
  std::vector<int> grooveVals;
  bool grooveUsed = false;
  int speedSplit = 0x20;

  struct RawCell {
    int note = -1, octave = 0, ins = 0x40, vol = 0x10;
    int fx[4][2] = {{0, 0}, {0, 0}, {0, 0}, {0, 0}};
  };
  struct RawPattern {
    int ch, idx;
    std::map<int, RawCell> rows;
  };
  std::vector<RawPattern> patterns;
  std::vector<int> fileFxCols;

  while (r.left() >= 3) {
    size_t p = r.pos();
    if (r.str(3) == "END") break;
    r.seek(p);
    std::string name = r.str(16);
    int bv = (int)r.u32();
    uint32_t size = r.u32();
    size_t start = r.pos(), end = std::min(start + size, d.size());
    if (size > d.size()) break;

    if (name == "PARAMS") {
      if (bv <= 1) r.u32();  // old speed/tempo
      if (bv >= 2) expansions = r.u8();
      fileChans = (int)r.u32();
      pal = (int)r.u32();
      if (bv >= 7) {
        int control = (int)r.u32(), value = (int)r.u32();
        hz = value <= 0 ? 0 : control == 1 ? 1000000.0 / value : value;
      } else {
        hz = (int)r.u32();
      }
      if (bv >= 3) r.u32();  // new vibrato
      if (bv >= 9) r.u32();  // sweep reset
      if (bv >= 4 && bv < 7) {
        hlA = (int)r.u32();
        hlB = (int)r.u32();
      }
      if ((expansions & 16) && bv >= 5) n163Chans = std::clamp((int)r.u32(), 1, 8);
      if (bv >= 6) speedSplit = (int)r.u32();
      if (fileChans == 5) expansions = 0;

      // Channel layout, in FamiTracker's order.
      auto chip = [&](int id, int moduleCount, int fileCount, std::vector<int>* list) {
        int base = m.totalCh;
        m.chips.push_back({id, moduleCount});
        m.chipVol.push_back(1.0f);
        for (int i = 0; i < fileCount; i++) {
          mapCh.push_back(base + i);
          if (list) list->push_back(base + i);
        }
        m.totalCh += moduleCount;
        return base;
      };
      chip(0x06, 5, 5, nullptr);
      if (expansions & 1) sawCh = chip(0x88, 3, 3, nullptr) + 2;
      if (expansions & 8) chip(0x8b, 3, 2, nullptr);  // no MMC5 PCM in FamiTracker
      if (expansions & 16) chip(0x8c, n163Chans, n163Chans, &n163Ch);
      if (expansions & 4) fdsCh = chip(0x8a, 1, 1, nullptr);
      if (expansions & 2) chip(0x9d, 6, 6, nullptr);
      if (expansions & 32) chip(0x80, 3, 3, nullptr);
    } else if (name == "INFO") {
      m.name = r.str(32);
      m.author = r.str(32);
      m.comment = r.str(32);
    } else if (name == "HEADER") {
      int songs = bv >= 2 ? r.u8() + 1 : 1;
      for (int i = 0; i < songs; i++)
        if (bv >= 3) r.cstr();
      fileFxCols.assign(fileChans, 1);
      for (int c = 0; c < fileChans; c++) {
        r.u8();  // channel id
        for (int s = 0; s < songs; s++) {
          int cols = r.u8() + 1;
          if (s == 0) fileFxCols[c] = std::clamp(cols, 1, 4);
        }
      }
      if (bv >= 4) {
        hlA = r.u8();
        hlB = r.u8();
      }
    } else if (name == "INSTRUMENTS") {
      int count = (int)r.u32();
      for (int i = 0; i < count && r.pos() < end; i++) {
        int idx = (int)r.u32();
        if (idx < 0 || idx >= 128) {
          err = "Corrupt FamiTracker instrument";
          return false;
        }
        FtIns& fi = ins[idx];
        fi.used = true;
        int type = r.u8();
        auto readSeqs = [&](int chipTable) {
          fi.chip = chipTable;
          int total = std::min<int>(r.u32(), 5);
          for (int j = 0; j < total; j++) {
            fi.hasSeq[j] = r.u8();
            fi.seqIndex[j] = r.u8();
          }
        };
        switch (type) {
          case 1: {  // 2A03
            fi.fur.type = 34;
            readSeqs(0);
            int notes = bv >= 2 ? 96 : 72;
            if (bv >= 7) notes = (int)r.u32();
            for (int j = 0; j < notes && j < 256; j++) {
              int note = bv >= 7 ? r.u8() : j;
              int smp = r.u8() - 1;
              int freq = r.u8();
              if (bv >= 6) r.u8();  // delta counter
              if (smp >= 0 && note < NOTE_COUNT) fi.dpcm[note] = {smp, freq & 15, (freq & 0x80) != 0};
            }
            break;
          }
          case 2:  // VRC6
            fi.fur.type = 12;
            readSeqs(1);
            break;
          case 3: {  // VRC7: built-in patch or custom
            fi.fur.type = 13;
            fi.fur.hasFM = true;
            fi.fur.opCount = 2;
            fi.fur.llPatch = r.u32() & 15;
            uint8_t cp[8];
            for (auto& b : cp) b = r.u8();
            for (int o = 0; o < 2; o++) {
              FurIns::Op& op = fi.fur.ops[o];
              op.ksr = (cp[o] >> 4) & 1;
              op.mult = cp[o] & 15;
              op.ar = cp[o + 4] >> 4;
              op.dr = cp[o + 4] & 15;
              op.sl = cp[o + 6] >> 4;
              op.rr = cp[o + 6] & 15;
              op.egt = (cp[o] >> 5) & 1;
            }
            fi.fur.fb = cp[3] & 7;
            fi.fur.ops[0].tl = cp[2] & 63;
            fi.fur.ops[1].tl = 0;
            break;
          }
          case 4: {  // FDS: a 64-step wave and its own envelopes
            fi.fur.type = 15;
            Wavetable w;
            w.data.resize(64);
            for (float& v : w.data) v = std::clamp((r.u8() & 63) / 63.0f * 2.0f - 1.0f, -1.0f, 1.0f);
            fi.waveOff = (int)m.waves.size();
            fi.fur.firstWave = fi.waveOff;
            m.waves.push_back(std::move(w));
            r.skip(32);  // modulation table
            r.u32();     // modulation speed
            r.u32();     // modulation depth
            r.u32();     // delay
            size_t here = r.pos();
            uint32_t a = r.u32(), b = r.u32();
            r.seek(here);
            if (!(a < 256 && (b & 0xff) != 0)) {
              auto inlineSeq = [&](FurMacro& mac, bool arp, bool pitch) {
                int len = r.u8();
                int loop = (int)r.u32(), rel = (int)r.u32();
                int mode = (int)r.u32();
                mac.values.resize(len);
                for (int& v : mac.values) {
                  v = r.u8();
                  if (pitch) v = -(int8_t)v;
                  else if (arp && mode == 1) v |= 1 << 30;
                  else if (arp) v = (int8_t)v;
                }
                mac.loop = loop >= 0 && loop < len ? loop : -1;
                mac.release = rel >= 0 && rel < len ? rel : -1;
                if (pitch) mac.mode = mode == 0 ? 1 : 0;
              };
              inlineSeq(fi.fur.macros[0], false, false);
              if (bv <= 3)
                for (int& v : fi.fur.macros[0].values) v *= 2;
              inlineSeq(fi.fur.macros[1], true, false);
              if (bv >= 3) inlineSeq(fi.fur.macros[4], false, true);
            }
            break;
          }
          case 5: {  // N163: sequences and its own waves
            fi.fur.type = 17;
            readSeqs(2);
            int waveSize = std::min<int>(r.u32(), 256);
            r.u32();  // wave position
            if (bv >= 8) r.u32();
            int waveCount = std::min<int>(r.u32(), 64);
            fi.waveOff = (int)m.waves.size();
            fi.waveCount = waveCount;
            fi.fur.firstWave = fi.waveOff;
            for (int k = 0; k < waveCount; k++) {
              Wavetable w;
              w.data.resize(waveSize);
              for (float& v : w.data) v = std::clamp((r.u8() & 15) / 15.0f * 2.0f - 1.0f, -1.0f, 1.0f);
              if ((int)m.waves.size() < MAX_WAVETABLES) m.waves.push_back(std::move(w));
            }
            break;
          }
          case 6:  // Sunsoft 5B
            fi.fur.type = 6;
            readSeqs(3);
            break;
          default:
            err = "Unsupported FamiTracker instrument type " + std::to_string(type);
            return false;
        }
        fi.fur.name = lenStr(r);
      }
    } else if (name == "SEQUENCES") {
      readSequences(r, bv, false, seqs[0]);
    } else if (name == "SEQUENCES_VRC6") {
      readSequences(r, bv, false, seqs[1]);
    } else if (name == "SEQUENCES_N163" || name == "SEQUENCES_N106") {
      readSequences(r, bv, true, seqs[2]);
    } else if (name == "SEQUENCES_S5B") {
      readSequences(r, bv, true, seqs[3]);
    } else if (name == "GROOVES") {
      int count = r.u8();
      std::map<int, std::vector<int>> grooves;
      for (int g = 0; g < count; g++) {
        int index = r.u8(), len = r.u8();
        std::vector<int>& vals = grooves[index];
        for (int k = 0; k < len; k++) vals.push_back(std::max(1, (int)r.u8()));
      }
      int songs = r.u8();
      for (int s = 0; s < songs; s++) {
        bool used = r.u8();
        if (s == 0 && used && grooves.count(0)) {
          grooveUsed = true;
          grooveVals = grooves[0];
        }
      }
    } else if (name == "FRAMES") {
      frames = std::clamp((int)r.u32(), 1, 256);
      if (bv >= 3) speed = (int)r.u32();
      if (bv >= 2) {
        tempo = (int)r.u32();
        patLen = std::clamp((int)r.u32(), 1, 256);
      }
      int chans = fileChans;
      if (bv == 1) chans = (int)r.u32();
      frameTable.assign(m.totalCh, std::vector<int>(frames, 0));
      for (int f = 0; f < frames; f++)
        for (int c = 0; c < chans; c++) {
          int o = r.u8();
          if (c < (int)mapCh.size() && mapCh[c] >= 0) frameTable[mapCh[c]][f] = o;
        }
      // Only the first song is imported; its frames come first.
    } else if (name == "PATTERNS") {
      if (bv == 1) patLen = std::clamp((int)r.u32(), 1, 256);
      while (r.pos() + 16 <= end) {
        int sub = bv >= 2 ? (int)r.u32() : 0;
        int ch = (int)r.u32(), idx = (int)r.u32(), items = (int)r.u32();
        if (ch < 0 || ch >= 64 || idx < 0 || idx >= 256 || items < 0 || items > 256) {
          err = "Corrupt FamiTracker pattern";
          return false;
        }
        RawPattern pat{ch, idx, {}};
        int cols = bv >= 6 ? 4 : ch < (int)fileFxCols.size() ? fileFxCols[ch] : 1;
        if (version == 0x200) cols = 1;
        for (int i = 0; i < items; i++) {
          int row = (version == 0x200 || bv >= 6) ? r.u8() : (int)r.u32();
          RawCell cell;
          cell.note = r.u8();
          cell.octave = r.u8();
          cell.ins = r.u8();
          cell.vol = r.u8();
          for (int j = 0; j < cols; j++) {
            int fx = r.u8(), val = 0;
            if (fx > 0 && fx < FT_EFFECT_COUNT) val = r.u8();
            else if (bv < 6) val = r.u8();
            if (bv < 3) {
              if (fx == FT_EF_PORTAOFF) {
                fx = FT_EF_PORTAMENTO;
                val = 0;
              } else if (fx == FT_EF_PORTAMENTO && val < 0xff) {
                val++;
              }
            }
            if (j < 4) {
              cell.fx[j][0] = fx;
              cell.fx[j][1] = val;
            }
          }
          if (sub == 0 && row < 256) pat.rows[row] = cell;
        }
        if (sub == 0) patterns.push_back(std::move(pat));
      }
    } else if (name == "DPCM SAMPLES") {
      int count = r.u8();
      for (int i = 0; i < count && r.pos() < end; i++) {
        int index = r.u8();
        dpcmNames[index] = lenStr(r);
        uint32_t len = std::min<uint32_t>(r.u32(), (uint32_t)r.left());
        dpcmData[index].resize(len);
        for (auto& b : dpcmData[index]) b = r.u8();
      }
    }
    r.seek(end);
  }
  if (mapCh.empty()) {
    err = "FamiTracker module without PARAMS";
    return false;
  }

  // Song settings: FamiTracker's tempo works like Furnace's virtual tempo.
  // Tempo T gives T / 2.5 ticks per second (FamiTracker's 150 = 60 Hz), like F0xx.
  m.hz = tempo > 0 ? tempo / 2.5f : hz > 0 ? (float)std::min(hz, 1000.0) : pal ? 50.0f : 60.0f;
  m.vtNum = m.vtDen = 1;
  m.speeds = grooveUsed && !grooveVals.empty() ? grooveVals : std::vector<int>{std::max(1, speed)};
  m.patLen = patLen;
  m.hlA = hlA;
  m.hlB = hlB;
  m.orders = frameTable;
  if (m.orders.empty()) m.orders.assign(m.totalCh, std::vector<int>(1, 0));
  m.fxCols.assign(m.totalCh, 1);
  for (int c = 0; c < (int)mapCh.size() && c < (int)fileFxCols.size(); c++) m.fxCols[mapCh[c]] = fileFxCols[c];

  // Instruments: sequences applied, all 64 slots kept so numbers match.
  int lastUsed = -1;
  for (int i = 0; i < 128; i++)
    if (ins[i].used) lastUsed = i;
  for (int i = 0; i <= lastUsed; i++) {
    FtIns& fi = ins[i];
    if (!fi.used) {
      FurIns empty;
      empty.type = 34;
      empty.name = "";
      m.instruments.push_back(empty);
      continue;
    }
    for (int t = 0; t < 5; t++) {
      if (!fi.hasSeq[t] || fi.fur.type == 15 || fi.fur.type == 13) continue;
      auto it = seqs[fi.chip].find({fi.seqIndex[t], t});
      if (it == seqs[fi.chip].end()) continue;
      if (fi.fur.type == 12 && t == 0 && it->second.setting == 1) fi.fur.type = 26;  // 64-step saw volume
      applySequence(fi.fur, t, it->second, fi.fur.type, fi.waveOff);
    }
    m.instruments.push_back(fi.fur);
  }

  // DPCM: one sample instrument per instrument with a note map. The pitch is
  // set by the map, not the note, so each note gets a copy of its sample at
  // the rate that plays it right.
  std::map<std::tuple<int, int, int>, int> made;  // (sample, pitch, note) -> our sample
  std::map<int, std::vector<float>> decoded;
  auto decode = [&](int s) -> const std::vector<float>& {
    auto it = decoded.find(s);
    if (it != decoded.end()) return it->second;
    std::vector<float>& out = decoded[s];
    int level = 64;
    for (uint8_t byte : dpcmData[s])
      for (int b = 0; b < 8; b++) {
        if (byte & (1 << b)) level = std::min(level + 2, 127);
        else level = std::max(level - 2, 0);
        out.push_back((level - 64) / 64.0f);
      }
    return out;
  };
  for (int i = 0; i <= lastUsed; i++) {
    FtIns& fi = ins[i];
    if (!fi.used || fi.dpcm.empty()) continue;
    Instrument smpIns;
    smpIns.type = INS_SAMPLE;
    smpIns.name = (fi.fur.name.empty() ? "Instrument " + std::to_string(i) : fi.fur.name) + " (DPCM)";
    smpIns.sampleMap.assign(NOTE_COUNT, -1);
    for (auto& [note, dp] : fi.dpcm) {
      if (dp.sample < 0 || dp.sample >= 256 || dpcmData[dp.sample].empty()) continue;
      auto key = std::make_tuple(dp.sample, dp.pitch, note);
      auto it = made.find(key);
      int idx;
      if (it != made.end()) {
        idx = it->second;
      } else {
        if ((int)m.samples.size() >= MAX_SAMPLES) break;
        Sample s;
        s.name = dpcmNames[dp.sample].empty() ? "DPCM " + std::to_string(dp.sample) : dpcmNames[dp.sample];
        s.data = decode(dp.sample);
        // Engine rate at C-5; scaled so this note plays at the DPCM rate.
        s.rate = (int)std::lround(DPCM_RATES[dp.pitch] * std::pow(2.0, (60 - note) / 12.0));
        if (dp.loop) {
          s.loopMode = 1;
          s.loopStart = 0;
          s.loopEnd = (int)s.data.size();
        }
        idx = (int)m.samples.size();
        m.samples.push_back(std::move(s));
        made[key] = idx;
      }
      smpIns.sampleMap[note] = (int16_t)idx;
      if (smpIns.sample < 0) smpIns.sample = idx;
    }
    if (smpIns.sample < 0) continue;
    m.sampleInsRemap[i] = (int)m.rawInstruments.size();
    m.rawInstruments.push_back(std::move(smpIns));
  }

  // Patterns.
  auto isIn = [](const std::vector<int>& v, int x) { return std::find(v.begin(), v.end(), x) != v.end(); };
  for (RawPattern& pat : patterns) {
    if (pat.ch >= (int)mapCh.size()) continue;
    int mc = mapCh[pat.ch];
    if (mc < 0 || mc >= m.totalCh) continue;
    std::vector<FurCell>& rows = m.patterns[{mc, pat.idx}];
    rows.assign(std::min(patLen, MAX_ROWS), FurCell());
    for (auto& [row, rc] : pat.rows) {
      if (row >= (int)rows.size()) continue;
      FurCell cell;
      int octave = rc.octave;
      if (rc.note == 0x0d || rc.note == 0x0e) {
        cell.note = NOTE_OFF;
      } else if (rc.note >= 1 && rc.note <= 12) {
        int n = octave * 12 + rc.note - 1;
        if (n >= 0 && n < NOTE_COUNT) cell.note = n;
      }
      if (rc.ins < 0x40 && rc.note != 0x0d && rc.note != 0x0e) cell.ins = rc.ins;
      if (rc.vol < 0x10) {
        int v = rc.vol;
        if (mc == sawCh) v = v * 42 / 15;
        if (mc == fdsCh) v = v * 31 / 15;
        cell.vol = v;
      }
      if (version == 0x200) {
        if (cell.vol == 0) cell.vol = 15;
        else if (cell.vol > 0) cell.vol = (cell.vol - 1) & 15;
      }
      for (int j = 0; j < 4; j++) {
        int fx = rc.fx[j][0], val = rc.fx[j][1];
        if (version < 0x0450 || dn)
          for (auto& e : FT_050_REMAP)
            if (fx == e[0]) {
              fx = e[1];
              break;
            }
        if (fx == FT_EF_SAMPLE_OFFSET && isIn(n163Ch, mc)) fx = FT_EF_N163_WAVE_BUFFER;
        if (fx <= 0 || fx >= FT_EFFECT_COUNT || FT_EFFECTS[fx] < 0) continue;
        int code = FT_EFFECTS[fx];
        if (code == 0x0f && val >= speedSplit) code = 0xf0;  // Fxx above the split sets the tempo
        if ((code == 0xe1 || code == 0xe2) && (val & 0xf0) == 0) val |= 0x10;
        if (code == 0x12 && isIn(n163Ch, mc)) code = 0x10;  // N163 wave select
        if (code == 0x09 || code == 0xff || code == 0xfc) continue;  // groove, halt, delayed release
        cell.fx[j][0] = code;
        cell.fx[j][1] = val;
      }
      rows[row] = cell;
    }
  }
  return buildFurSong(m, song, err);
}
