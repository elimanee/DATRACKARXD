// DefleMask (.dmf) importer. DefleMask's effects are the ones Furnace grew
// from, so the module is read into Furnace terms and converted the same way.
#include <algorithm>
#include <cmath>
#include <cstring>

#include "fur_internal.h"
#include "import.h"

using namespace fur;

namespace {

// DefleMask system byte -> Furnace chips (most ids are Furnace's legacy ones).
void dmfChips(int sys, std::vector<std::pair<int, int>>& chips, std::vector<float>& vols) {
  auto add = [&](int id, int n, float v = 1.0f) {
    chips.push_back({id, n});
    vols.push_back(v);
  };
  switch (sys) {
    case 0x02: add(0x83, 6); add(0x03, 4, 0.5f); break;            // Genesis
    case 0x42: add(0xa0, 9); add(0x03, 4, 0.5f); break;            // Genesis, extended channel 3
    case 0x08: add(0x82, 8); add(0x9b, 5); break;                  // Arcade: YM2151 + SegaPCM
    case 0x09: add(0xa5, 13); break;                               // Neo Geo
    case 0x49: add(0xa6, 16); break;                               // Neo Geo, extended channel 2
    case 0x43: add(0x03, 4); add(0x89, 9); break;                  // SMS + OPLL
    case 0x46: add(0x06, 5); add(0x9d, 6); break;                  // NES + VRC7
    case 0x86: add(0x06, 5); add(0x8a, 1); break;                  // NES + FDS
    case 0x0a: add(0x80, 3); add(0xa1, 5); break;                  // MSX: AY + SCC
    case 0x03: add(0x03, 4); break;
    case 0x04: add(0x04, 4); break;
    case 0x05: add(0x05, 6); break;
    case 0x06: add(0x06, 5); break;
    case 0x07: add(0x07, 3); break;
    case 0x47: add(0x47, 3); break;
    default: break;
  }
}

std::string pstr(ByteReader& r) { return r.str(r.u8()); }

// Reads a DefleMask macro (length, values, loop when non-empty).
void readMacro(ByteReader& r, FurMacro& m, bool wide) {
  int len = r.u8();
  m.values.resize(len);
  for (int& v : m.values) v = wide ? r.s32() : r.u8();
  if (len > 0) {
    int loop = r.u8();
    m.loop = loop < len ? loop : -1;
  }
}

}  // namespace

bool importDMF(const std::vector<uint8_t>& raw, Song& song, std::string& err) {
  std::vector<uint8_t> inflated;
  const std::vector<uint8_t>* d = &raw;
  if (raw.size() < 16 || std::memcmp(raw.data(), ".DelekDefleMask.", 16) != 0) {
    if (!zlibInflate(raw.data(), raw.size(), inflated)) {
      err = "Corrupt DefleMask file";
      return false;
    }
    d = &inflated;
  }
  if (d->size() < 18 || std::memcmp(d->data(), ".DelekDefleMask.", 16) != 0) {
    err = "Not a DefleMask module";
    return false;
  }
  ByteReader r(*d);
  r.seek(16);
  int ver = r.u8();
  if (ver < 0x0b || ver > 0x1b) {
    err = "Unsupported DefleMask version (" + std::to_string(ver) + ")";
    return false;
  }
  int sys = r.u8();
  FurModule m;
  dmfChips(sys, m.chips, m.chipVol);
  if (m.chips.empty()) {
    err = "Unsupported DefleMask system";
    return false;
  }
  for (auto& c : m.chips) m.totalCh += c.second;
  int nch = m.totalCh;
  bool isGB = sys == 0x04, isC64 = sys == 0x07 || sys == 0x47;
  bool isNES = sys == 0x06 || sys == 0x46 || sys == 0x86;
  bool isOPLL = sys == 0x43 || sys == 0x46;
  bool isNeo = sys == 0x09 || sys == 0x49;

  m.name = pstr(r);
  m.author = pstr(r);
  if (ver > 0x0c) {
    m.hlA = r.u8();
    m.hlB = r.u8();
  }
  int timeBase = r.u8();
  int s1 = r.u8(), s2 = r.u8();
  bool pal = r.u8();
  bool customHz = r.u8();
  m.hz = pal ? 60.0f : 50.0f;  // the flag is inverted in the files
  std::string hzStr = r.str(3);
  if (customHz) {
    int hz = std::atoi(hzStr.c_str());
    m.hz = hz > 0 ? (float)hz : 60.0f;
  }
  m.patLen = ver > 0x17 ? (int)r.u32() : r.u8();
  int ordLen = r.u8();
  if (m.patLen <= 0 || m.patLen > 256 || ordLen > 127) {
    err = "Corrupt DefleMask header";
    return false;
  }
  if (ver < 20) r.u8();  // arpeggio speed
  m.speeds = {s1 * (timeBase + 1)};
  if (s2 != s1) m.speeds.push_back(s2 * (timeBase + 1));
  m.vtNum = m.vtDen = 1;

  m.orders.assign(nch, std::vector<int>(ordLen));
  for (int c = 0; c < nch; c++)
    for (int o = 0; o < ordLen; o++) {
      m.orders[c][o] = r.u8() & 0x7f;
      if (ver > 0x18) pstr(r);  // pattern name
    }

  // Instruments.
  int insCount = r.u8();
  bool wide = ver >= 0x0e;
  for (int i = 0; i < insCount && !r.eof(); i++) {
    FurIns ins;
    ins.name = pstr(r);
    int mode = r.u8();
    if (mode) {
      ins.type = isOPLL ? 13 : sys == 0x08 ? 33 : 1;
      ins.hasFM = true;
      ins.alg = r.u8();
      if (ver < 0x13) r.u8();
      ins.fb = r.u8();
      if (ver < 0x13) r.u8();
      r.u8();  // fms
      if (ver < 0x13) {
        r.u8();
        r.u8();
      }
      r.u8();  // ams
      ins.opCount = 4;
      for (int o = 0; o < 4; o++) {
        FurIns::Op& op = ins.ops[o];
        r.u8();  // am
        op.ar = r.u8();
        if (isOPLL) op.ar &= 15;
        if (ver < 0x13) r.u8();
        op.dr = r.u8();
        if (isOPLL) op.dr &= 15;
        if (ver < 0x13) {
          r.u8();
          op.egt = r.u8();
          r.u8();
          if (ver < 0x11) op.ksr = r.u8();
        }
        op.mult = r.u8();
        op.rr = r.u8();
        op.sl = r.u8();
        if (ver < 0x13) r.u8();
        op.tl = r.u8();
        if (ver < 0x13) {
          r.u8();
          r.u8();
        } else {
          int b = r.u8();
          if (isOPLL && o == 0) ins.llPatch = b;
        }
        if (isOPLL) {
          op.ksr = r.u8();
          r.u8();
          r.u8();
          r.u8();
        } else {
          op.rs = r.u8();
          op.dt = r.u8();
          op.d2r = r.u8();
          r.u8();  // ssg-eg
        }
      }
    } else {
      ins.type = isGB ? 2 : isC64 ? 3 : isNES ? 34 : isNeo ? 6 : sys == 0x05 ? 5 : sys == 0x0a ? 6 : 0;
      if (!isGB || ver < 0x12) readMacro(r, ins.macros[0], wide);
      readMacro(r, ins.macros[1], wide);
      int arpMode = ver > 0x0f ? r.u8() : 0;
      for (int& v : ins.macros[1].values) {
        if (arpMode) v ^= 0x40000000;  // fixed notes
        else v -= 12;
      }
      readMacro(r, ins.macros[2], wide);
      readMacro(r, ins.macros[3], wide);
      if (isC64) {
        int tri = r.u8(), saw = r.u8(), pulse = r.u8(), noise = r.u8();
        ins.hasC64 = true;
        ins.c64Wave = (tri ? 1 : 0) | (saw ? 2 : 0) | (pulse ? 4 : 0) | (noise ? 8 : 0);
        ins.c64A = r.u8();
        ins.c64D = r.u8();
        ins.c64S = r.u8();
        ins.c64R = r.u8();
        ins.c64Duty = r.u8() * 4095 / 100;
        ins.c64Ring = r.u8();
        ins.c64Sync = r.u8();
        ins.c64ToFilter = r.u8();
        bool volIsCutoff = ver < 0x11 ? r.u32() != 0 : r.u8() != 0;
        r.u8();  // init filter
        ins.c64Res = r.u8() & 15;
        ins.c64Cut = r.u8() * 2047 / 100;
        ins.c64Hp = r.u8();
        ins.c64Bp = r.u8();
        ins.c64Lp = r.u8();
        r.u8();  // ch3 off
        // DefleMask's C64 macros are relative, scaled (duty x4, cutoff x7).
        ins.c64DutyAbs = false;
        ins.c64FilterAbs = false;
        ins.c64MultiplyRel = true;
        if (volIsCutoff) {
          ins.macros[5] = ins.macros[0];
          for (int& v : ins.macros[5].values) v = -(v - 18);
          ins.macros[0] = FurMacro();
        }
        for (int& v : ins.macros[2].values) v -= 12;
      }
      if (isGB && ver > 0x11) {
        ins.hasGB = true;
        ins.gbVol = r.u8();
        ins.gbDir = r.u8();
        ins.gbLen = r.u8();
        r.u8();  // sound length
      } else if (isGB) {
        ins.hasGB = true;
        ins.gbSoft = 1;
      }
    }
    m.instruments.push_back(std::move(ins));
  }

  // Wavetables.
  int waveCount = r.u8();
  int waveMax = isGB ? 15 : sys == 0x86 ? 63 : sys == 0x0a ? 255 : 31;
  for (int i = 0; i < waveCount && !r.eof(); i++) {
    int len = r.u32() & 255;
    if (len > 65) {
      err = "Corrupt DefleMask wavetable";
      return false;
    }
    Wavetable w;
    w.data.resize(len);
    for (float& v : w.data) {
      int x = (wide ? (int)r.u32() : r.u8()) & waveMax;
      v = std::clamp(x / (float)waveMax * 2.0f - 1.0f, -1.0f, 1.0f);
    }
    m.waves.push_back(std::move(w));
  }
  if (m.waves.size() == 1 && m.waves[0].data.empty()) m.waves.clear();

  // Patterns: one per order and channel, stored in order.
  m.fxCols.assign(nch, 1);
  for (int c = 0; c < nch; c++) {
    int cols = ver < 0x0a ? 1 : r.u8();
    if (cols < 1 || cols > 4) {
      err = "Corrupt DefleMask patterns";
      return false;
    }
    m.fxCols[c] = cols;
    for (int o = 0; o < ordLen; o++) {
      std::vector<FurCell>& rows = m.patterns[{c, m.orders[c][o]}];
      rows.assign(std::min(m.patLen, MAX_ROWS), FurCell());
      for (int k = 0; k < m.patLen; k++) {
        FurCell cell;
        int note = r.s16(), oct = r.s16();
        if (sys == 0x03 && ver < 0x0e && oct > 0) oct--;
        else if (sys == 0x02 && ver < 0x0e && oct > 0 && c > 5) oct--;
        if (isGB && ver < 0x12 && c == 3 && oct > 0) oct -= 2;
        if (note == 100) {
          cell.note = NOTE_OFF;
        } else if (note != 0 || oct != 0) {
          int n = oct * 12 + note;
          if (n >= 0 && n < NOTE_COUNT) cell.note = n;
        }
        int vol = r.s16();
        if (isGB && ver < 0x12 && c == 2 && vol > 0) vol = (vol & 3) * 5;
        cell.vol = vol;
        for (int e = 0; e < cols; e++) {
          int cmd = r.s16(), val = r.s16();
          if (ver < 0x14 && cmd == 0xe5 && val != -1) val = 128 + (val - 128) / 4;
          if ((cmd == 0x09 || cmd == 0x0f) && val != -1) val = std::clamp(val * (timeBase + 1), 1, 255);
          cell.fx[e][0] = cmd;
          cell.fx[e][1] = val;
        }
        cell.ins = r.s16();
        if (k < (int)rows.size()) rows[k] = cell;
      }
    }
  }

  // Samples: DefleMask plays them on its sample channels, the note picking
  // the sample (C = first). Each becomes an instrument at its own rate.
  static const double pitches[11] = {1 / 6.0, 0.2, 0.25, 1 / 3.0, 0.5, 1, 2, 3, 4, 5, 6};
  static const int rates[6] = {4000, 8000, 11025, 16000, 22050, 32000};
  int smpCount = r.eof() ? 0 : r.u8();
  for (int i = 0; i < smpCount && !r.eof(); i++) {
    int length = (int)r.u32();
    if (length < 0 || length > (1 << 26)) {
      err = "Corrupt DefleMask sample";
      return false;
    }
    Sample s;
    if (ver > 0x16) s.name = pstr(r);
    int rate = rates[std::min<int>(r.u8(), 5)];
    int pitch = r.u8();
    if (pitch > 10) pitch = 5;
    int vol = r.u8();
    int depth = ver > 0x15 ? r.u8() : 16;
    if (depth != 8 && depth != 16) depth = 16;
    int cutStart = 0, cutEnd = -1;
    if (ver >= 0x1b) {
      cutStart = (int)r.u32();
      cutEnd = (int)r.u32();
    }
    std::vector<int16_t> data(length);
    for (auto& v : data) v = r.s16();
    // Resample by the pitch setting and apply the volume, like DefleMask.
    float mult = vol / 50.0f;
    for (double j = 0; j < length; j += pitches[pitch]) {
      float x = depth == 8 ? (data[(size_t)j] - 0x80) / 128.0f : data[(size_t)j] / 32768.0f;
      s.data.push_back(std::clamp(x * mult, -1.0f, 1.0f));
    }
    if (cutEnd >= 0 && cutStart >= 0 && cutStart <= cutEnd && cutEnd <= (int)s.data.size())
      s.data = std::vector<float>(s.data.begin() + cutStart, s.data.begin() + cutEnd);
    s.rate = rate;
    if (s.name.empty()) s.name = "Sample " + std::to_string(i);
    m.samples.push_back(std::move(s));

    Instrument ins;
    ins.name = m.samples.back().name;
    ins.type = INS_SAMPLE;
    ins.sample = i;
    ins.macros[MACRO_ARP].values = {ARP_FIXED + 60};  // always at the sample's own rate
    m.rawInstruments.push_back(ins);
  }
  m.legacySampleNotes = !m.rawInstruments.empty();
  return buildFurSong(m, song, err);
}
