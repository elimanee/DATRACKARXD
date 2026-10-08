// Internal to the Furnace-family importers (.fur, .dmf, .ftm): a module in
// Furnace's terms (chips, instruments, effect codes) and the conversion of
// such a module to a DATRACKARXD song.
#pragma once
#include <map>
#include <string>
#include <utility>
#include <vector>

#include "import.h"

namespace fur {

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
  bool c64MultiplyRel = false;  // relative duty/cutoff steps x4/x7 (DefleMask)
  int initSample = -1;
  bool useSample = false, useWave = false;
  std::vector<std::pair<int, int>> sampleMap;  // our note -> sample
  int firstWave = -1;
  // Instrument files (.fui) carry their own samples and wavetables:
  // original index -> file offset of the SMP2/WAVE block.
  std::vector<std::pair<int, uint32_t>> sampleList, waveList;
};

// A pattern cell in Furnace's terms: our note numbering, the chip's volume
// range and Furnace effect codes (-1 = empty).
struct FurCell {
  int note = NOTE_EMPTY;
  int ins = -1;
  int vol = -1;
  int fx[8][2];
  FurCell() {
    for (auto& e : fx) e[0] = e[1] = -1;
  }
};

struct FurModule {
  std::string name, author, comment;
  float hz = 60;
  int vtNum = 150, vtDen = 150;
  std::vector<int> speeds;
  int patLen = 64, hlA = 4, hlB = 16;
  std::vector<std::pair<int, int>> chips;  // Furnace chip id, channel count
  std::vector<float> chipVol;
  int totalCh = 0;
  std::vector<std::vector<int>> orders;  // [channel][order]
  std::vector<int> fxCols;
  std::vector<std::string> chanNames;
  std::vector<Wavetable> waves;
  std::vector<Sample> samples;
  std::vector<FurIns> instruments;
  std::map<std::pair<int, int>, std::vector<FurCell>> patterns;  // (channel, index) -> rows
  // Instruments used as they are, after the converted ones. With
  // legacySampleNotes, a note on a sample channel picks rawInstruments[note % 12].
  std::vector<Instrument> rawInstruments;
  bool legacySampleNotes = false;
  // On sample channels, instrument i plays rawInstruments[sampleInsRemap[i]].
  std::map<int, int> sampleInsRemap;
};

Instrument convertInstrument(const FurIns& f, float tickRate, bool haveWaves);
bool buildFurSong(const FurModule& m, Song& song, std::string& err);

}  // namespace fur
