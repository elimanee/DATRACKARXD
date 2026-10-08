// Furnace (.fur) importer. Furnace emulates many sound chips; each chip
// channel is mapped to the closest DATRACKARXD voice (FM, pulse, triangle,
// noise, wavetable or sample) and instruments are converted the same way.
#include <algorithm>
#include <cmath>
#include <cstring>

#include "import.h"

namespace {

enum class Kind { FM, Pulse, Triangle, Saw, Noise, Wave, Sample, Any };

struct ChanDesc {
  Kind kind = Kind::Pulse;
  int volMax = 15;
  bool waveFx = false;  // 10xx selects a wavetable
  bool snNoise = false;  // 20xy picks white or periodic noise
  int fixedDuty = -1;
  int minNote = 0;
  float mix = 1.0f;
  bool dutyFx = false;  // 12xx sets the pulse duty
  int dutyShift = 0;
  std::string name;
};

// Channel layout of a Furnace sound chip.
void chipChannels(int id, int count, std::vector<ChanDesc>& out) {
  auto add = [&](Kind k, int vol, const std::string& name, bool wave = false, bool duty = false, int dutyShift = 0) {
    ChanDesc d;
    d.kind = k;
    d.volMax = vol;
    d.name = name;
    d.waveFx = wave;
    d.dutyFx = duty;
    d.dutyShift = dutyShift;
    out.push_back(d);
  };
  auto n = [](const char* p, int i) { return std::string(p) + std::to_string(i + 1); };
  auto opn = [&](int adpcm, int ssg, const char* chip) {
    int fm = std::max(0, count - ssg - adpcm);
    for (int i = 0; i < fm; i++) add(Kind::FM, 127, n(chip, i));
    for (int i = 0; i < ssg; i++) {
      add(Kind::Pulse, 15, n("SSG ", i));
      out.back().fixedDuty = 2;
      out.back().mix = 0.4f;
    }
    for (int i = 0; i < adpcm - 1 && adpcm > 0; i++) add(Kind::Sample, 31, n("ADPCM-A ", i));
    if (adpcm > 0) add(Kind::Sample, 255, "ADPCM-B");
  };
  switch (id) {
    case 0x83: case 0xa0: case 0xbd: case 0xbe: case 0xc1:
      for (int i = 0; i < count; i++) add(Kind::FM, 127, n("FM ", i));
      break;
    case 0x82: case 0x98:
      for (int i = 0; i < count; i++) add(Kind::FM, 127, n("FM ", i));
      break;
    case 0x89: case 0xa7: case 0x9d:
      for (int i = 0; i < count; i++) add(Kind::FM, 15, n("FM ", i));
      break;
    case 0x8f: case 0x90: case 0x91: case 0xa2: case 0xa3: case 0xa4: case 0xb2: case 0xb3: case 0xd1:
      for (int i = 0; i < count; i++) add(Kind::FM, 63, n("FM ", i));
      break;
    case 0xae: case 0xaf: {
      int fm = std::min(count, id == 0xae ? 18 : 20);
      for (int i = 0; i < fm; i++) add(Kind::FM, 63, n("FM ", i));
      for (int i = fm; i < count; i++) add(Kind::Sample, 127, n("PCM ", i - fm));
      break;
    }
    case 0x8d: case 0xb6: case 0xc3: opn(0, 3, "FM "); break;
    case 0x8e: case 0xb7: case 0xc4: case 0xa5: case 0xa6: case 0xc2: case 0x9e: case 0xde: case 0xc5: opn(7, 3, "FM "); break;
    case 0x03: case 0xbf:
      for (int i = 0; i < 3; i++) {
        add(Kind::Pulse, 15, n("Square ", i));
        out.back().mix = 0.4f;
        out.back().fixedDuty = 2;
        out.back().minNote = 33;  // 10-bit period limit
      }
      add(Kind::Noise, 15, "Noise");
      out.back().snNoise = true;
      out.back().mix = 0.4f;
      break;
    case 0x04:
      add(Kind::Pulse, 15, "Pulse 1", false, true);
      add(Kind::Pulse, 15, "Pulse 2", false, true);
      add(Kind::Wave, 15, "Wave", true);
      add(Kind::Noise, 15, "Noise", false, true);
      break;
    case 0x06: case 0xf1:
      add(Kind::Pulse, 15, "Pulse 1", false, true);
      add(Kind::Pulse, 15, "Pulse 2", false, true);
      add(Kind::Triangle, 15, "Triangle");
      add(Kind::Noise, 15, "Noise", false, true);
      add(Kind::Sample, 15, "DPCM");
      break;
    case 0x8b:
      add(Kind::Pulse, 15, "Pulse 1", false, true);
      add(Kind::Pulse, 15, "Pulse 2", false, true);
      add(Kind::Sample, 255, "PCM");
      break;
    case 0x88:
      add(Kind::Pulse, 15, "VRC6 1", false, true, 1);
      add(Kind::Pulse, 15, "VRC6 2", false, true, 1);
      add(Kind::Saw, 63, "Saw");
      break;
    case 0x05: for (int i = 0; i < count; i++) add(Kind::Wave, 31, n("Wave ", i), true); break;
    case 0x8a: add(Kind::Wave, 32, "FDS", true); break;
    case 0x8c: case 0x96: case 0xa1: case 0xb4: case 0x9c: case 0xb9: case 0xba: case 0xbb: case 0xe4: case 0xad:
      for (int i = 0; i < count; i++) add(Kind::Wave, 15, n("Wave ", i), true);
      break;
    case 0xc8: for (int i = 0; i < count; i++) add(Kind::Wave, 31, n("Wave ", i), true); break;
    case 0x80: case 0x97: case 0x84: case 0x85: case 0x94: case 0xe3: case 0xcb: case 0x99: case 0x9f: case 0xca: case 0x86: case 0x93:
      for (int i = 0; i < count; i++) {
        add(Kind::Pulse, 15, n("Square ", i));
        out.back().fixedDuty = 2;
      }
      break;
    case 0x9a:
      for (int i = 0; i < count; i++) {
        add(Kind::Pulse, 31, n("Square ", i));
        out.back().fixedDuty = 2;
      }
      break;
    case 0xcd: for (int i = 0; i < count; i++) add(Kind::Pulse, 8, n("Square ", i)); break;
    case 0xd5: case 0xac: for (int i = 0; i < count; i++) add(Kind::Pulse, 63, n("Pulse ", i)); break;
    case 0xb5: case 0xbc: case 0xa8: for (int i = 0; i < count; i++) add(Kind::Pulse, 127, n("Pulse ", i)); break;
    case 0xd4: for (int i = 0; i < count; i++) add(Kind::Noise, 15, n("Noise ", i)); break;
    case 0x07: case 0x47: case 0xf0: case 0xf5:
      for (int i = 0; i < std::min(count, 3); i++) add(Kind::Any, 15, n("SID ", i));
      for (int i = 3; i < count; i++) add(Kind::Sample, 15, "PCM");
      break;
    case 0xe2:
      for (int i = 0; i < 3; i++) add(Kind::Any, 15, n("SID ", i));
      add(Kind::Sample, 15, "PCM");
      break;
    case 0x81: for (int i = 0; i < count; i++) add(Kind::Sample, 64, n("Amiga ", i)); break;
    case 0x87: case 0x9b: case 0xcc: case 0xd6: case 0x92: for (int i = 0; i < count; i++) add(Kind::Sample, 127, n("PCM ", i)); break;
    case 0xc6: case 0xb0: for (int i = 0; i < count; i++) add(Kind::Sample, 15, n("PCM ", i)); break;
    case 0xaa: for (int i = 0; i < count; i++) add(Kind::Sample, 8, n("ADPCM ", i)); break;
    case 0xb1: for (int i = 0; i < count; i++) add(Kind::Sample, 4095, n("PCM ", i)); break;
    case 0xc0: case 0x95: case 0xe0: case 0xb8: case 0xc7: case 0xce: case 0xcf: case 0xd7: case 0xd8: case 0xab:
      for (int i = 0; i < count; i++) add(Kind::Sample, 255, n("PCM ", i));
      break;
    default: for (int i = 0; i < count; i++) add(Kind::Pulse, 15, n("Ch ", i)); break;
  }
  out.resize(out.size());
}

int nominalChannels(int id) {
  static const std::pair<int, int> table[] = {
      {0x01, 17}, {0x03, 4}, {0x04, 4}, {0x05, 6}, {0x06, 5}, {0x07, 3}, {0x47, 3}, {0x80, 3}, {0x81, 4}, {0x82, 8},
      {0x83, 6}, {0x84, 2}, {0x85, 4}, {0x86, 1}, {0x87, 8}, {0x88, 3}, {0x89, 9}, {0x8a, 1}, {0x8b, 3}, {0x8c, 8},
      {0x8d, 6}, {0x8e, 16}, {0x8f, 9}, {0x90, 9}, {0x91, 18}, {0x92, 28}, {0x93, 1}, {0x94, 4}, {0x95, 8}, {0x96, 4},
      {0x97, 6}, {0x98, 8}, {0x99, 1}, {0x9a, 3}, {0x9b, 16}, {0x9c, 6}, {0x9d, 6}, {0x9e, 16}, {0x9f, 6}, {0xa0, 9},
      {0xa1, 5}, {0xa2, 11}, {0xa3, 11}, {0xa4, 20}, {0xa5, 14}, {0xa6, 17}, {0xa7, 11}, {0xa8, 4}, {0xaa, 4},
      {0xab, 1}, {0xac, 17}, {0xad, 2}, {0xae, 42}, {0xaf, 44}, {0xb0, 16}, {0xb1, 32}, {0xb2, 10}, {0xb3, 12},
      {0xb4, 5}, {0xb5, 8}, {0xb6, 9}, {0xb7, 19}, {0xb8, 8}, {0xb9, 3}, {0xba, 8}, {0xbb, 8}, {0xbc, 8}, {0xbd, 11},
      {0xbe, 7}, {0xbf, 4}, {0xc0, 1}, {0xc1, 10}, {0xc2, 18}, {0xc3, 10}, {0xc4, 20}, {0xc5, 20}, {0xc6, 2},
      {0xc7, 4}, {0xc8, 3}, {0xca, 5}, {0xcb, 3}, {0xcc, 4}, {0xcd, 2}, {0xce, 24}, {0xcf, 16}, {0xd1, 18},
      {0xd4, 4}, {0xd5, 6}, {0xd6, 16}, {0xd7, 2}, {0xd8, 16}, {0xd9, 4}, {0xe0, 19}, {0xe2, 4}, {0xe3, 4},
      {0xe4, 8}, {0xe5, 4}, {0xf0, 3}, {0xf1, 5}, {0xf5, 7}, {0xfc, 1}, {0xfd, 8},
  };
  for (auto& e : table)
    if (e.first == id) return e.second;
  return 0;
}

// Legacy compound systems are split into their chips.
void flattenChip(int id, std::vector<std::pair<int, int>>& chips) {
  switch (id) {
    case 0x02: chips.push_back({0x83, 6}); chips.push_back({0x03, 4}); break;
    case 0x42: chips.push_back({0xa0, 9}); chips.push_back({0x03, 4}); break;
    case 0x08: chips.push_back({0x82, 8}); chips.push_back({0x9b, 5}); break;
    case 0x43: chips.push_back({0x03, 4}); chips.push_back({0x89, 9}); break;
    case 0x46: chips.push_back({0x06, 5}); chips.push_back({0x9d, 6}); break;
    case 0x09: chips.push_back({0xa5, 13}); break;
    case 0x49: chips.push_back({0xa6, 16}); break;
    case 0xa9: chips.push_back({0x9b, 5}); break;
    default: chips.push_back({id, nominalChannels(id)}); break;
  }
}

// --- instruments ------------------------------------------------------------

struct FurMacro {
  std::vector<int> values;
  int loop = -1, release = -1, mode = 0, type = 0, delay = 0, speed = 1;
};

struct FurIns {
  std::string name;
  int type = 0;
  bool hasFM = false;
  int alg = 0, fb = 0, opCount = 4, llPatch = 0;
  struct Op {
    int mult = 1, dt = 3, tl = 0, ar = 31, dr = 0, sl = 0, d2r = 0, rr = 15, rs = 0, ksr = 0, egt = 0;
    bool enabled = true;
  } ops[4];
  FurMacro macros[6];  // vol, arp, duty, wave, pitch, cutoff (Furnace's ALG macro)
  bool hasGB = false;
  int gbVol = 15, gbDir = 0, gbLen = 0, gbSoft = 0;
  bool hasC64 = false;
  int c64Wave = 4, c64A = 0, c64D = 0, c64S = 15, c64R = 0, c64Duty = 2048;
  int c64Cut = 1024, c64Res = 0;
  bool c64Ring = false, c64Sync = false, c64ToFilter = false, c64Lp = true, c64Bp = false, c64Hp = false;
  bool c64DutyAbs = true, c64FilterAbs = true;
  int initSample = -1;
  bool useSample = false, useWave = false;
  std::vector<std::pair<int, int>> sampleMap;  // our note -> sample
  int firstWave = -1;
};

bool isFMType(int t) { return t == 1 || t == 33 || t == 19 || t == 14 || t == 13 || t == 32 || t == 55; }
bool isOPL(int t) { return t == 14 || t == 13 || t == 32; }

bool isSampleType(int t) {
  switch (t) {
    case 4: case 27: case 28: case 29: case 35: case 36: case 37: case 38: case 39: case 40: case 41: case 42:
    case 45: case 46: case 50: case 53: case 54: case 59: case 60: case 61: case 25:
      return true;
    default:
      return false;
  }
}

bool isWaveType(int t) {
  switch (t) {
    case 2: case 5: case 15: case 16: case 17: case 18: case 22: case 25: case 31: case 48: return true;
    default: return false;
  }
}

int typeVolMax(int t) {
  switch (t) {
    case 1: case 33: case 19: case 55: return 127;
    case 14: case 32: return 63;
    case 13: return 15;
    case 5: case 7: case 48: case 37: return 31;
    case 11: case 21: return 1;
    case 15: return 32;
    case 23: case 30: case 43: case 28: case 29: case 39: case 50: case 59: return 127;
    case 24: case 26: case 58: return 63;
    case 4: return 64;
    case 52: return 8;
    case 27: return 4095;
    case 36: return 8;
    case 40: case 41: case 42: case 46: case 53: case 54: case 38: case 60: case 61: case 62: case 66: return 255;
    default: return 15;
  }
}

void readMacroData(ByteReader& r, FurMacro& m, int len, int wordSize) {
  m.values.resize(len);
  for (int& v : m.values) {
    switch (wordSize) {
      case 0: v = r.u8(); break;
      case 1: v = r.s8(); break;
      case 2: v = r.s16(); break;
      default: v = r.s32(); break;
    }
  }
}

void parseINS2(ByteReader& r, size_t end, FurIns& ins, int songVersion) {
  int insVersion = r.u16();
  ins.type = r.u16();
  (void)songVersion;
  while (r.pos() + 4 <= end) {
    std::string code = r.str(2);
    if (code == "EN") break;
    int len = r.u16();
    size_t next = r.pos() + len;
    if (code == "NA") {
      ins.name = r.cstr();
    } else if (code == "FM") {
      ins.hasFM = true;
      int flags = r.u8();
      ins.opCount = std::max(1, flags & 15);
      static const int order[4] = {0, 2, 1, 3};
      for (int k = 0; k < 4; k++) ins.ops[order[k]].enabled = flags & (16 << k);
      int b = r.u8();
      ins.alg = (b >> 4) & 7;
      ins.fb = b & 7;
      r.u8();
      ins.llPatch = r.u8() & 31;
      if (insVersion >= 224) r.u8();
      for (int o = 0; o < ins.opCount && o < 4; o++) {
        FurIns::Op& op = ins.ops[o];
        int b0 = r.u8(), b1 = r.u8(), b2 = r.u8(), b3 = r.u8(), b4 = r.u8(), b5 = r.u8();
        r.u8();
        r.u8();
        op.ksr = b0 >> 7;
        op.dt = (b0 >> 4) & 7;
        op.mult = b0 & 15;
        op.tl = b1 & 127;
        op.rs = b2 >> 6;
        op.ar = b2 & 31;
        op.dr = b3 & 31;
        op.egt = b4 >> 7;
        op.d2r = b4 & 31;
        op.sl = b5 >> 4;
        op.rr = b5 & 15;
      }
    } else if (code == "MA") {
      int hdrLen = r.u16();
      while (r.pos() < next) {
        size_t start = r.pos();
        int mc = r.u8();
        if (mc == 255) break;
        int mlen = r.u8(), mloop = r.u8(), mrel = r.u8(), mmode = r.u8(), mtype = r.u8(), mdelay = r.u8(), mspeed = r.u8();
        r.seek(start + std::max(hdrLen, 8));
        FurMacro tmp;
        FurMacro& m = mc < 5 ? ins.macros[mc] : mc == 8 ? ins.macros[5] : tmp;
        readMacroData(r, m, mlen, mtype >> 6);
        m.loop = mloop == 255 || mloop >= mlen ? -1 : mloop;
        m.release = mrel == 255 || mrel >= mlen ? -1 : mrel;
        m.mode = mmode;
        m.type = (mtype >> 1) & 3;
        m.delay = mdelay;
        m.speed = std::max(1, (int)mspeed);
      }
    } else if (code == "GB") {
      ins.hasGB = true;
      int env = r.u8();
      ins.gbLen = env >> 5;
      ins.gbDir = (env >> 4) & 1;
      ins.gbVol = env & 15;
      r.u8();
      ins.gbSoft = r.u8() & 1;
    } else if (code == "64") {
      ins.hasC64 = true;
      int f1 = r.u8();
      int f2 = r.u8();
      int ad = r.u8(), sr = r.u8();
      ins.c64Duty = r.u16() & 4095;
      int cr = r.u16();
      ins.c64Cut = cr & 2047;
      ins.c64Res = cr >> 12;
      ins.c64DutyAbs = f1 & 0x80;
      ins.c64ToFilter = f1 & 0x10;
      ins.c64Sync = f2 & 0x80;
      ins.c64Ring = f2 & 0x40;
      ins.c64FilterAbs = f2 & 0x10;
      ins.c64Bp = f2 & 4;
      ins.c64Hp = f2 & 2;
      ins.c64Lp = f2 & 1;
      ins.c64Wave = f1 & 15;
      ins.c64A = ad >> 4;
      ins.c64D = ad & 15;
      ins.c64S = sr >> 4;
      ins.c64R = sr & 15;
    } else if (code == "SM") {
      ins.initSample = r.s16();
      int flags = r.u8();
      r.u8();
      ins.useSample = flags & 2;
      ins.useWave = flags & 4;
      if (flags & 1) {
        int entries = insVersion >= 246 ? 180 : 120;
        int base = insVersion >= 246 ? 60 : 0;
        for (int i = 0; i < entries; i++) {
          r.u16();  // note to play
          int smp = r.s16();
          int note = i - base;
          if (note >= 0 && note < NOTE_COUNT && smp >= 0) ins.sampleMap.push_back({note, smp});
        }
      }
    } else if (code == "WS") {
      ins.firstWave = r.s32();
      r.s32();
      r.skip(2);
      if (!r.u8()) ins.firstWave = -1;
    } else if (code == "N1") {
      ins.firstWave = r.s32();
    }
    r.seek(next);
  }
}

void parseINST(ByteReader& r, FurIns& ins, int songVersion) {
  r.u16();  // instrument format version
  ins.type = r.u8();
  r.u8();
  ins.name = r.cstr();
  ins.hasFM = isFMType(ins.type);
  ins.alg = r.u8();
  ins.fb = r.u8();
  r.u8();
  r.u8();
  ins.opCount = r.u8() == 2 ? 2 : 4;
  ins.llPatch = r.u8();
  r.skip(2);
  for (int o = 0; o < 4; o++) {
    FurIns::Op& op = ins.ops[o];
    r.u8();  // am
    op.ar = r.u8();
    op.dr = r.u8();
    op.mult = r.u8();
    op.rr = r.u8();
    op.sl = r.u8();
    op.tl = r.u8();
    r.u8();  // dt2
    op.rs = r.u8();
    op.dt = r.u8();
    op.d2r = r.u8();
    r.u8();  // ssg
    r.skip(2);
    op.egt = r.u8();
    r.u8();  // ksl
    r.skip(3);
    op.ksr = r.u8();
    int en = r.u8();
    op.enabled = songVersion >= 114 ? en != 0 : true;
    r.skip(11);
  }
  // Game Boy
  ins.gbVol = r.u8();
  ins.gbDir = r.u8();
  ins.gbLen = r.u8();
  r.u8();
  ins.hasGB = ins.type == 2;
  // C64
  int tri = r.u8(), saw = r.u8(), pulse = r.u8(), noise = r.u8();
  ins.c64Wave = (tri ? 1 : 0) | (saw ? 2 : 0) | (pulse ? 4 : 0) | (noise ? 8 : 0);
  ins.c64A = r.u8();
  ins.c64D = r.u8();
  ins.c64S = r.u8();
  ins.c64R = r.u8();
  ins.c64Duty = r.u16() & 4095;
  ins.c64Ring = r.u8();
  ins.c64Sync = r.u8();
  ins.c64ToFilter = r.u8();
  r.u8();  // init filter
  r.u8();  // volume is cutoff
  ins.c64Res = r.u8() & 15;
  ins.c64Lp = r.u8();
  ins.c64Bp = r.u8();
  ins.c64Hp = r.u8();
  r.u8();  // ch3 off
  ins.c64Cut = r.u16() & 2047;
  ins.c64DutyAbs = r.u8();
  ins.c64FilterAbs = r.u8();
  ins.hasC64 = ins.type == 3;
  // Amiga
  ins.initSample = r.s16();
  ins.useWave = r.u8() == 1;
  r.u8();
  r.skip(12);
  ins.useSample = true;
  // Standard macros
  int lens[8], loops[8];
  for (int& l : lens) l = std::clamp(r.s32(), 0, 256);
  for (int& l : loops) l = r.s32();
  int arpMode = r.u8();
  r.skip(3);
  for (int m = 0; m < 8; m++) {
    FurMacro tmp;
    FurMacro& mac = m < 5 ? ins.macros[m] : tmp;
    mac.values.resize(lens[m]);
    for (int& v : mac.values) v = r.s32();
    mac.loop = loops[m] >= 0 && loops[m] < lens[m] ? loops[m] : -1;
  }
  if (songVersion < 31)
    for (int& v : ins.macros[1].values) v -= 12;
  if (songVersion < 112 && arpMode)
    for (int& v : ins.macros[1].values) v |= 1 << 30;
}

// SID cutoff register (0-2047, about 30 Hz to 12 kHz, roughly linear) to
// our exponential 0-255 cutoff.
int sidCutoff(int reg) {
  double hz = 30.0 + reg * 5.85;
  return std::clamp((int)std::lround(std::log2(hz / 30.0) / 9.2 * 255.0), 0, 255);
}

Macro convertMacro(const FurMacro& fm, int which, const FurIns& ins, int ourType) {
  Macro m;
  if (fm.values.empty() || fm.type != 0) return m;
  int acc = which == MACRO_CUTOFF ? ins.c64Cut : ins.c64Duty;  // for relative SID macros
  int typeMax = typeVolMax(ins.type);
  for (int v : fm.values) {
    switch (which) {
      case MACRO_VOL: {
        int hi = ourType == INS_FM ? 127 : ourType == INS_SAMPLE ? 64 : 15;
        v = (int)std::lround(std::clamp(v, 0, typeMax) * (double)hi / typeMax);
        break;
      }
      case MACRO_ARP:
        if (v & (1 << 30)) v = ARP_FIXED + std::clamp(v & 0x3FFFFFFF, 0, NOTE_COUNT - 1);
        else v = std::clamp(v, -60, 60);
        break;
      case MACRO_DUTY:
        if (ins.type == 3) {
          // SID pulse width (12 bits) to our fine width; relative macros add up.
          if (!ins.c64DutyAbs) v = acc = std::clamp(acc - v, 0, 4095);
          v = std::clamp((v & 4095) / 16, 1, 255);
        }
        else if (ins.type == 0) v = (v & 1) ? 0 : 3;  // SN noise: white or periodic
        else if (ins.type == 12) v = std::clamp(v, 0, 7) / 2;
        else v &= 3;
        break;
      case MACRO_WAVE:
        if (ins.type == 3) v = (v & 8) ? WAVE_NOISE : (v & 4) ? WAVE_PULSE : (v & 2) ? WAVE_SAW : WAVE_TRIANGLE;
        else v = std::clamp(v, 0, 255);
        break;
      case MACRO_PITCH: v = std::clamp(v / 4, -128, 127); break;
      case MACRO_CUTOFF:
        if (!ins.c64FilterAbs) v = acc = std::clamp(acc + v, 0, 2047);
        v = sidCutoff(std::clamp(v, 0, 2047));
        break;
    }
    m.values.push_back(v);
  }
  // Absolute pitch macros become per-tick changes.
  if (which == MACRO_PITCH && fm.mode == 0) {
    int prev = 0;
    for (int& v : m.values) {
      int d = v - prev;
      prev = v;
      v = std::clamp(d, -128, 127);
    }
  }
  m.loop = fm.loop;
  m.release = fm.release;
  // Speed and delay: stretch the macro.
  if (fm.speed > 1 || fm.delay > 0) {
    Macro s;
    for (int i = 0; i < fm.delay; i++) s.values.push_back(which == MACRO_PITCH ? 0 : m.values[0]);
    for (size_t i = 0; i < m.values.size(); i++) {
      if ((int)i == m.loop) s.loop = (int)s.values.size();
      for (int k = 0; k < fm.speed; k++) {
        bool keep = which != MACRO_PITCH || k == 0;
        s.values.push_back(keep ? m.values[i] : 0);
      }
      if ((int)i == m.release) s.release = (int)s.values.size() - 1;
    }
    m = s;
  }
  if ((int)m.values.size() > MAX_MACRO_LEN) {
    m.values.resize(MAX_MACRO_LEN);
    if (m.loop >= MAX_MACRO_LEN) m.loop = -1;
    if (m.release >= MAX_MACRO_LEN) m.release = -1;
  }
  return m;
}

// Envelope as a volume macro: start level, step every `ticksPerStep`.
Macro rampMacro(int from, int to, double ticksPerStep) {
  Macro m;
  int v = from;
  int dir = to > from ? 1 : -1;
  int hold = std::max(1, (int)std::lround(ticksPerStep));
  while ((int)m.values.size() < MAX_MACRO_LEN) {
    for (int k = 0; k < hold && (int)m.values.size() < MAX_MACRO_LEN; k++) m.values.push_back(v);
    if (v == to) break;
    v += dir;
  }
  return m;
}

Instrument convertInstrument(const FurIns& f, float tickRate, bool haveWaves) {
  Instrument ins;
  ins.name = f.name.empty() ? "Instrument" : f.name;
  if (isFMType(f.type)) {
    ins.type = INS_FM;
  } else if (isSampleType(f.type) || (f.type == 34 && f.useSample && f.initSample >= 0)) {
    ins.type = (f.useWave && f.type == 4) ? INS_STANDARD : INS_SAMPLE;
  } else {
    ins.type = INS_STANDARD;
  }

  if (ins.type == INS_FM) {
    FMParams& fm = ins.fm;
    fm.fb = f.fb;
    if (isOPL(f.type) && f.opCount <= 2) {
      // 2-operator OPL/OPLL: modulator -> carrier (or both carriers).
      fm.alg = (f.alg & 1) ? 7 : 4;
      for (int o = 0; o < 2; o++) {
        const FurIns::Op& s = f.ops[o];
        FMOperator& d = fm.ops[o];
        d.enabled = true;
        d.mult = s.mult;
        d.dt = 3;
        d.tl = std::min(127, s.tl & 63);
        d.ar = std::min(31, s.ar * 2);
        d.dr = std::min(31, s.dr * 2);
        d.sl = s.sl;
        d.rr = s.rr;
        d.d2r = s.egt ? 0 : std::min(31, s.rr * 2);
        d.ksr = s.ksr ? 2 : 0;
      }
      fm.ops[2].enabled = fm.ops[3].enabled = false;
      if (f.type == 13 && f.llPatch) {
        // OPLL built-in patch: use a generic bright tone.
        fm.ops[0] = FMOperator();
        fm.ops[0].mult = 1;
        fm.ops[0].tl = 30;
        fm.ops[0].dr = 8;
        fm.ops[0].sl = 3;
        fm.ops[1] = FMOperator();
        fm.ops[1].dr = 4;
        fm.ops[1].sl = 2;
        fm.ops[1].rr = 7;
        fm.fb = 5;
      }
    } else {
      // OPN/OPM operator data is stored in 1/3/2/4 order.
      static const int logical[4] = {0, 2, 1, 3};
      fm.alg = isOPL(f.type) ? (f.alg == 0 ? 0 : f.alg == 1 ? 6 : f.alg == 2 ? 4 : 7) : f.alg;
      for (int o = 0; o < 4; o++) {
        const FurIns::Op& s = f.ops[isOPL(f.type) ? o : logical[o]];
        FMOperator& d = fm.ops[o];
        d.enabled = s.enabled;
        d.mult = s.mult;
        d.dt = s.dt;
        d.tl = s.tl;
        d.ar = s.ar;
        d.dr = s.dr;
        d.sl = s.sl;
        d.d2r = s.d2r;
        d.rr = s.rr;
        d.ksr = s.rs;
        if (isOPL(f.type)) {
          d.ar = std::min(31, s.ar * 2);
          d.dr = std::min(31, s.dr * 2);
          d.d2r = s.egt ? 0 : std::min(31, s.rr * 2);
          d.tl = s.tl & 63;
          d.dt = 3;
        }
      }
    }
  }

  for (int m = 0; m < MACRO_COUNT; m++) {
    if (ins.type == INS_FM && (m == MACRO_DUTY || m == MACRO_WAVE)) continue;
    if (m == MACRO_DUTY && (f.type == 6 || f.type == 7 || f.type == 9 || f.type == 5)) continue;
    if (m == MACRO_CUTOFF && !(f.hasC64 || f.type == 3)) continue;
    if (m == MACRO_WAVE && !isWaveType(f.type) && f.type != 3 && !(f.type == 4 && f.useWave)) continue;
    ins.macros[m] = convertMacro(f.macros[m], m, f, ins.type);
  }

  if (ins.type == INS_STANDARD) {
    ins.volume = 15;
    ins.wave = WAVE_PULSE;
    if (isWaveType(f.type) || (f.type == 4 && f.useWave)) {
      ins.wave = WAVE_TABLE;
      if (haveWaves) ins.songWave = std::max(0, f.firstWave);
    }
    if (f.type == 26) ins.wave = WAVE_SAW;
    if (f.hasC64 || f.type == 3) {
      int w = f.c64Wave;
      ins.wave = (w & 8) ? WAVE_NOISE : (w & 4) ? WAVE_PULSE : (w & 2) ? WAVE_SAW : WAVE_TRIANGLE;
      int d = f.c64Duty;
      ins.duty = d < 768 ? 0 : d < 1536 ? 1 : d < 2560 ? 2 : 3;
      ins.pulseWidth = std::clamp(d / 16, 1, 255);
      ins.ringMod = f.c64Ring;
      ins.sync = f.c64Sync;
      ins.filter.on = f.c64ToFilter;
      ins.filter.mode = f.c64Lp ? 0 : f.c64Bp ? 1 : f.c64Hp ? 2 : 0;
      ins.filter.cutoff = sidCutoff(f.c64Cut);
      ins.filter.resonance = f.c64Res & 15;
      if (ins.macros[MACRO_VOL].values.empty()) {
        // SID ADSR as a volume macro.
        static const int atk[16] = {2, 8, 16, 24, 38, 56, 68, 80, 100, 250, 500, 800, 1000, 3000, 5000, 8000};
        double tpm = tickRate / 1000.0;
        Macro m;
        int aT = std::max(1, (int)std::lround(atk[f.c64A] * tpm));
        for (int i = 1; i <= aT && (int)m.values.size() < 120; i++) m.values.push_back(15 * i / aT);
        int dT = std::max(1, (int)std::lround(atk[f.c64D] * 3 * tpm));
        for (int i = 1; i <= dT && (int)m.values.size() < 180; i++) m.values.push_back(15 - (15 - f.c64S) * i / dT);
        if (m.values.empty()) m.values.push_back(f.c64S);
        m.release = (int)m.values.size() - 1;
        int rT = std::max(1, (int)std::lround(atk[f.c64R] * 3 * tpm));
        for (int i = 1; i <= rT && (int)m.values.size() < MAX_MACRO_LEN; i++) m.values.push_back(f.c64S - f.c64S * i / rT);
        ins.macros[MACRO_VOL] = m;
      }
    }
    if (f.hasGB && !f.gbSoft && ins.macros[MACRO_VOL].values.empty()) {
      // Game Boy hardware envelope: one step every len/64 s.
      if (f.gbLen == 0)
        ins.macros[MACRO_VOL].values = {f.gbVol};
      else
        ins.macros[MACRO_VOL] = rampMacro(f.gbVol, f.gbDir ? 15 : 0, f.gbLen * tickRate / 64.0);
    }
  } else if (ins.type == INS_SAMPLE) {
    ins.sample = f.initSample;
    if (!f.sampleMap.empty()) {
      ins.sampleMap.assign(NOTE_COUNT, -1);
      for (auto& e : f.sampleMap) ins.sampleMap[e.first] = (int16_t)e.second;
    }
  }
  return ins;
}

// --- samples and waves --------------------------------------------------------

void parseSample(ByteReader& r, size_t end, const std::string& id, int version, Sample& s) {
  s.name = r.cstr();
  int length = r.u32();
  int depth, loopStart = -1, loopEnd = -1, loopDir = 0;
  double c4rate;
  if (id == "SMP2") {
    r.u32();  // compatibility rate
    c4rate = r.u32();
    depth = r.u8();
    loopDir = r.u8();
    r.u8();
    r.u8();
    loopStart = r.s32();
    loopEnd = r.s32();
    r.skip(16);
  } else {
    int rate = r.u32();
    r.u16();
    r.u16();
    depth = r.u8();
    r.u8();
    int c4 = r.u16();
    c4rate = version >= 32 && c4 ? c4 : rate;
    if (version >= 19) loopStart = r.s32();
    if (version < 58) depth = 16;
    if (loopStart >= 0) loopEnd = length;
  }
  size_t bytes = end > r.pos() ? end - r.pos() : 0;
  if (depth == 16) {
    int n = std::min<int>(length, (int)(bytes / 2));
    s.data.resize(n);
    for (int i = 0; i < n; i++) s.data[i] = r.s16() / 32768.0f;
  } else if (depth == 8) {
    int n = std::min<int>(length, (int)bytes);
    s.data.resize(n);
    for (int i = 0; i < n; i++) s.data[i] = r.s8() / 128.0f;
  } else if (depth == 1) {
    // NES DPCM: 1-bit deltas on a 7-bit counter.
    int n = std::min<int>(length, (int)(bytes * 8));
    s.data.resize(n);
    int counter = 64, byte = 0;
    for (int i = 0; i < n; i++) {
      if ((i & 7) == 0) byte = r.u8();
      counter += (byte >> (i & 7)) & 1 ? 2 : -2;
      counter = std::clamp(counter, 0, 127);
      s.data[i] = (counter - 64) / 64.0f;
    }
  }
  s.rate = std::max(1, (int)std::lround(c4rate * 2));
  s.volume = 64;
  int len = (int)s.data.size();
  if (loopStart >= 0 && loopEnd > loopStart && loopEnd <= len) {
    s.loopMode = loopDir == 1 ? 3 : loopDir == 2 ? 2 : 1;
    s.loopStart = loopStart;
    s.loopEnd = loopEnd;
  } else {
    s.loopMode = 0;
    s.loopStart = 0;
    s.loopEnd = len;
  }
}

void parseWave(ByteReader& r, Wavetable& w) {
  r.cstr();
  int width = std::clamp<int>(r.s32(), 0, 4096);
  r.u32();
  int height = r.s32();
  if (height <= 0) height = 15;
  w.data.resize(width);
  for (float& v : w.data) v = std::clamp(r.s32() / (float)height * 2.0f - 1.0f, -1.0f, 1.0f);
}

}  // namespace

bool importFUR(const std::vector<uint8_t>& raw, Song& song, std::string& err) {
  std::vector<uint8_t> inflated;
  const std::vector<uint8_t>* d = &raw;
  if (raw.size() < 16 || std::memcmp(raw.data(), "-Furnace module-", 16) != 0) {
    if (!zlibInflate(raw.data(), raw.size(), inflated)) {
      err = "Corrupt compressed Furnace file";
      return false;
    }
    d = &inflated;
  }
  ByteReader r(*d);
  if (d->size() < 32 || std::memcmp(d->data(), "-Furnace module-", 16) != 0) {
    err = "Not a Furnace module";
    return false;
  }
  r.seek(16);
  int version = r.u16();
  r.u16();
  uint32_t infoPtr = r.u32();
  r.seek(infoPtr);
  std::string blockId = r.str(4);
  r.u32();

  std::string name, author, comment;
  float hz = 60;
  int patLen = 64, ordLen = 1, hlA = 4, hlB = 16, vtNum = 150, vtDen = 150;
  std::vector<int> speeds;
  std::vector<std::pair<int, int>> chips;
  std::vector<float> chipVol;
  std::vector<uint32_t> insPtr, wavePtr, smpPtr, patPtr;
  int totalCh = 0;
  std::vector<std::vector<int>> orders;  // [channel][order]
  std::vector<int> fxCols;
  std::vector<std::string> chanNames;

  auto readOrders = [&](int nch) {
    orders.assign(nch, std::vector<int>(ordLen));
    for (int c = 0; c < nch; c++)
      for (int o = 0; o < ordLen; o++) orders[c][o] = r.u8();
    fxCols.resize(nch);
    for (int& f : fxCols) f = std::clamp<int>(r.u8(), 1, MAX_EFFECTS);
    r.skip(nch * 2);  // hidden, collapsed
    chanNames.resize(nch);
    for (auto& n : chanNames) n = r.cstr();
    for (int c = 0; c < nch; c++) r.cstr();  // short names
  };

  if (blockId == "INFO") {
    int timeBase = r.u8();
    int s1 = r.u8(), s2 = r.u8();
    r.u8();
    hz = r.f32();
    patLen = r.u16();
    ordLen = r.u16();
    hlA = r.u8();
    hlB = r.u8();
    int insCount = r.u16(), waveCount = r.u16(), smpCount = r.u16();
    uint32_t patCount = r.u32();
    int ids[32];
    for (int& id : ids) id = r.u8();
    int vols[32];
    for (int& v : vols) v = r.s8();
    r.skip(32 + 128);
    for (int i = 0; i < 32 && ids[i]; i++) {
      size_t before = chips.size();
      flattenChip(ids[i], chips);
      // Older files store chip volumes here (64 = 100%); newer ones elsewhere.
      float v = version < 135 ? std::clamp(vols[i] / 64.0f, 0.0f, 4.0f) : 1.0f;
      for (size_t k = before; k < chips.size(); k++) chipVol.push_back(v);
    }
    name = r.cstr();
    author = r.cstr();
    r.f32();
    r.skip(20);
    insPtr.resize(insCount);
    wavePtr.resize(waveCount);
    smpPtr.resize(smpCount);
    patPtr.resize(std::min<uint32_t>(patCount, 65536));
    for (auto& p : insPtr) p = r.u32();
    for (auto& p : wavePtr) p = r.u32();
    for (auto& p : smpPtr) p = r.u32();
    for (auto& p : patPtr) p = r.u32();
    for (auto& c : chips) totalCh += c.second;
    readOrders(totalCh);
    comment = r.cstr();
    if (version >= 59) r.f32();
    if (version >= 70) r.skip(28);
    if (version >= 96) {
      int n = r.u16(), den = r.u16();
      if (n > 0 && den > 0 && n < 1000 && den < 1000) {
        vtNum = n;
        vtDen = den;
      }
    }
    speeds = {s1 * (timeBase + 1)};
    if (s2 != s1) speeds.push_back(s2 * (timeBase + 1));
  } else if (blockId == "INF2") {
    name = r.cstr();
    author = r.cstr();
    for (int i = 0; i < 6; i++) r.cstr();
    r.f32();
    r.u8();
    r.f32();
    totalCh = r.u16();
    int numChips = r.u16();
    for (int i = 0; i < numChips; i++) {
      int id = r.u16(), cnt = r.u16();
      float vol = r.f32();
      r.skip(8);
      chips.push_back({id, cnt});
      chipVol.push_back(std::isfinite(vol) ? std::clamp(vol, 0.0f, 4.0f) : 1.0f);
    }
    uint32_t conns = r.u32();
    r.skip(conns * 4 + 1);
    uint32_t subsongPtr = 0;
    for (;;) {
      int type = r.u8();
      if (type == 0 || r.eof()) break;
      uint32_t num = r.u32();
      std::vector<uint32_t> ptrs(std::min<uint32_t>(num, 65536));
      for (auto& p : ptrs) p = r.u32();
      switch (type) {
        case 1: if (!ptrs.empty()) subsongPtr = ptrs[0]; break;
        case 4: insPtr = ptrs; break;
        case 5: wavePtr = ptrs; break;
        case 6: smpPtr = ptrs; break;
        case 7: patPtr = ptrs; break;
        default: break;
      }
    }
    if (!subsongPtr) {
      err = "Furnace file has no song";
      return false;
    }
    r.seek(subsongPtr + 8);
    hz = r.f32();
    r.u8();
    r.u8();
    patLen = r.u16();
    ordLen = r.u16();
    hlA = r.u8();
    hlB = r.u8();
    vtNum = std::max<int>(1, r.u16());
    vtDen = std::max<int>(1, r.u16());
    int spLen = std::clamp<int>(r.u8(), 1, 16);
    for (int i = 0; i < 16; i++) {
      int v = r.u16();
      if (i < spLen) speeds.push_back(std::max(1, v));
    }
    r.cstr();
    r.cstr();
    readOrders(totalCh);
  } else {
    err = "Unsupported Furnace song header";
    return false;
  }

  // Channel layout.
  std::vector<ChanDesc> desc;
  for (size_t i = 0; i < chips.size(); i++) {
    size_t before = desc.size();
    chipChannels(chips[i].first, chips[i].second, desc);
    float v = i < chipVol.size() ? chipVol[i] : 1.0f;
    for (size_t k = before; k < desc.size(); k++) desc[k].mix *= v;
  }
  desc.resize(totalCh);
  int nch = std::clamp(totalCh, 1, MAX_CHANNELS);
  song.reset(nch);
  song.name = name.empty() ? "Furnace song" : name;
  song.author = author;
  song.comment = comment;
  song.tickRate = std::clamp(hz * vtNum / (float)vtDen, 1.0f, 1000.0f);
  song.speeds = speeds.empty() ? std::vector<int>{6} : speeds;
  if ((int)song.speeds.size() > MAX_GROOVE) song.speeds.resize(MAX_GROOVE);
  song.patternLength = std::clamp(patLen, 1, MAX_ROWS);
  song.highlight1 = hlA;
  song.highlight2 = hlB;
  for (int c = 0; c < nch; c++) {
    ChannelInfo& ch = song.channels[c];
    const ChanDesc& cd = desc[c];
    ch.name = c < (int)chanNames.size() && !chanNames[c].empty() ? chanNames[c] : cd.name;
    ch.effectCols = c < (int)fxCols.size() ? fxCols[c] : 1;
    switch (cd.kind) {
      case Kind::Pulse: ch.forceWave = WAVE_PULSE; break;
      case Kind::Triangle: ch.forceWave = WAVE_TRIANGLE; break;
      case Kind::Saw: ch.forceWave = WAVE_SAW; break;
      case Kind::Noise: ch.forceWave = WAVE_NOISE; break;
      case Kind::Wave: ch.forceWave = WAVE_TABLE; break;
      default: ch.forceWave = -1; break;
    }
    // Furnace volume slides move x/4 volume steps per tick.
    ch.volSlideUnit = std::max(1, (int)std::lround(64.0 * 127.0 / std::max(cd.volMax, 1)));
    ch.fixedDuty = cd.fixedDuty;
    ch.minNote = cd.minNote;
    ch.mix = cd.mix;
  }
  song.orders.clear();
  for (int o = 0; o < std::min(ordLen, MAX_ORDERS); o++) {
    std::array<uint8_t, MAX_CHANNELS> row{};
    for (int c = 0; c < nch; c++) row[c] = (uint8_t)orders[c][o];
    song.orders.push_back(row);
  }
  if (song.orders.empty()) song.orders.push_back({});

  // Wavetables and samples.
  for (uint32_t p : wavePtr) {
    if ((int)song.wavetables.size() >= MAX_WAVETABLES) break;
    r.seek(p);
    Wavetable w;
    if (r.str(4) == "WAVE") {
      r.u32();
      parseWave(r, w);
    }
    song.wavetables.push_back(std::move(w));
  }
  for (uint32_t p : smpPtr) {
    if ((int)song.samples.size() >= MAX_SAMPLES) break;
    r.seek(p);
    std::string id = r.str(4);
    uint32_t size = r.u32();
    Sample s;
    if (id == "SMP2" || id == "SMPL") parseSample(r, p + 8 + size, id, version, s);
    song.samples.push_back(std::move(s));
  }

  // Instruments.
  song.instruments.clear();
  for (uint32_t p : insPtr) {
    if ((int)song.instruments.size() >= MAX_INSTRUMENTS) break;
    r.seek(p);
    std::string id = r.str(4);
    uint32_t size = r.u32();
    FurIns fi;
    if (id == "INS2") parseINS2(r, p + 8 + size, fi, version);
    else if (id == "INST") parseINST(r, fi, version);
    song.instruments.push_back(convertInstrument(fi, song.tickRate, !song.wavetables.empty()));
  }
  if (song.instruments.empty()) song.instruments.emplace_back();

  // Patterns.
  auto convertVol = [&](int c, int v) {
    int mx = desc[c].volMax;
    return mx == 127 ? std::min(v, 127) : std::clamp((int)std::lround(v * 127.0 / mx), 0, 127);
  };
  auto convertFx = [&](int c, int cmd, int val, Effect& out) {
    const ChanDesc& cd = desc[c];
    int mx = cd.volMax;
    switch (cmd) {
      case 0x00: case 0x01: case 0x02: case 0x03: case 0x04: case 0x05: case 0x06: case 0x07: case 0x08:
      case 0x0A: case 0x0B: case 0x0C: case 0x0D: case 0x0F: case 0x80: case 0x90: case 0x91: case 0x92:
      case 0xC0: case 0xC1: case 0xC2: case 0xC3: case 0xE1: case 0xE2: case 0xEC: case 0xED: case 0xF0:
      case 0xF1: case 0xF2:
        out = {(int16_t)cmd, (int16_t)val};
        return;
      case 0x09: out = {0x0F, (int16_t)val}; return;
      case 0xF3: case 0xF4:
        out = {(int16_t)cmd, (int16_t)(val < 0 ? -1 : std::min(127, (int)std::lround(val * 127.0 / mx)))};
        return;
      case 0x10:
        if (cd.waveFx) out = {0x10, (int16_t)val};
        return;
      case 0x12:
        if (cd.dutyFx) out = {0x12, (int16_t)(val < 0 ? -1 : (val >> cd.dutyShift) & 3)};
        return;
      case 0x20:
        if (cd.snNoise) out = {0x12, (int16_t)((val & 1) ? 0 : 3)};
        return;
      default: return;
    }
  };

  for (uint32_t p : patPtr) {
    r.seek(p);
    std::string id = r.str(4);
    uint32_t size = r.u32();
    size_t end = p + 8 + size;
    if (id == "PATN") {
      int sub = r.u8();
      int ch = version >= 240 ? r.u16() : r.u8();
      int idx = r.u16();
      r.cstr();
      if (sub != 0 || ch >= nch || idx >= MAX_PATTERNS) continue;
      Pattern* pat = song.pattern(ch, idx, true);
      int row = 0;
      while (row < patLen && r.pos() < end) {
        int b = r.u8();
        if (b == 0xFF) break;
        if (b & 0x80) {
          row += (b & 0x7F) + 2;
          continue;
        }
        if (b == 0) {
          row++;
          continue;
        }
        int mask = (b & 0x20) ? r.u8() : ((b >> 3) & 3);
        if (b & 0x40) mask |= r.u8() << 8;
        Cell cell;
        if (b & 1) {
          int n = r.u8();
          if (n == 183) {
            r.u32();
          } else if (n >= 180) {
            cell.note = NOTE_OFF;
          } else {
            int ours = n - 60;
            if (ours >= 0 && ours < NOTE_COUNT) cell.note = (int16_t)ours;
          }
        }
        if (b & 2) cell.ins = r.u8();
        if (b & 4) cell.vol = (int16_t)convertVol(ch, r.u8());
        int col = 0;
        for (int e = 0; e < 8; e++) {
          int cmd = -1, val = -1;
          if (mask & (1 << (e * 2))) cmd = r.u8();
          if (mask & (1 << (e * 2 + 1))) val = r.u8();
          if (cmd < 0 && val < 0) continue;
          Effect out;
          if (cmd >= 0) convertFx(ch, cmd, val, out);
          if (out.cmd >= 0 && e < MAX_EFFECTS) cell.fx[std::min(e, MAX_EFFECTS - 1)] = out;
          col = std::max(col, e + 1);
        }
        if (row < MAX_ROWS) pat->rows[row] = cell;
        row++;
      }
    } else if (id == "PATR") {
      int ch = r.u16();
      int idx = r.u16();
      int sub = r.u16();
      r.u16();
      if ((version >= 95 && sub != 0) || ch >= nch || idx >= MAX_PATTERNS) continue;
      int cols = ch < (int)fxCols.size() ? fxCols[ch] : 1;
      Pattern* pat = song.pattern(ch, idx, true);
      for (int row = 0; row < patLen; row++) {
        int note = r.s16(), oct = r.s16(), ins = r.s16(), vol = r.s16();
        Cell cell;
        if (note == 100 || note == 101 || note == 102) {
          cell.note = NOTE_OFF;
        } else if (note != 0 || oct != 0) {
          int o = (int8_t)(oct & 0xFF);
          int n = o * 12 + note;
          if (n >= 0 && n < NOTE_COUNT) cell.note = (int16_t)n;
        }
        if (ins >= 0) cell.ins = (int16_t)ins;
        if (vol >= 0) cell.vol = (int16_t)convertVol(ch, vol);
        for (int e = 0; e < cols; e++) {
          int cmd = r.s16(), val = r.s16();
          if (cmd < 0) continue;
          Effect out;
          convertFx(ch, cmd, val, out);
          if (out.cmd >= 0 && e < MAX_EFFECTS) cell.fx[e] = out;
        }
        if (row < MAX_ROWS) pat->rows[row] = cell;
      }
    }
  }
  return true;
}
