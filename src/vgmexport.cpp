#include "vgmexport.h"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <map>
#include <sstream>

#include "chips.h"
#include "engine.h"

namespace {

// What a song channel plays on: the chip type of its first chip instrument,
// with the kind of voice it needs.
struct ChannelUse {
  int type = INS_STANDARD;
  int gbKind = 0;      // Game Boy: 0 pulse, 1 wave, 2 noise
  bool snNoise = false;
  bool oplFour = false;
  int nesKind = 0;     // NES: 0 pulse, 1 triangle, 2 noise, 3 DPCM
  bool pceNoise = false;
};

ChannelUse channelUse(const Song& song, int c) {
  ChannelUse u;
  for (auto& o : song.orders) {
    const Pattern* p = song.pattern(c, o[c]);
    if (!p) continue;
    for (int r = 0; r < song.patternLength; r++) {
      int i = p->rows[r].ins;
      if (i < 0 || i >= (int)song.instruments.size() || !isChipType(song.instruments[i].type)) continue;
      const Instrument& ins = song.instruments[i];
      u.type = ins.type;
      u.gbKind = ins.chip.gbChannel;
      u.snNoise = ins.chip.snNoise;
      u.oplFour = ins.opl.fourOp;
      u.nesKind = ins.chip.nesChannel;
      u.pceNoise = ins.chip.pceNoise;
      return u;
    }
  }
  return u;
}

struct Vgm {
  std::vector<uint8_t> data;
  std::map<std::string, int> dropped;
  // Last values written, per chip register (-1 = never).
  std::vector<int> ay[2], gb, opl;
  std::vector<int> sn;  // per SN "register": 0-2 tone periods, 3 noise, 4-7 attenuations
  std::vector<int> opn[2], nes[2], scc[2], pce[2];  // pce: 6 channels x 16 registers, then the globals
  int pceSel[2] = {-1, -1};
  Vgm() {
    ay[0].assign(16, -1);
    ay[1].assign(16, -1);
    gb.assign(0x30, -1);
    opl.assign(0x200, -1);
    sn.assign(8, -1);
    for (int k = 0; k < 2; k++) {
      opn[k].assign(0x200, -1);
      nes[k].assign(0x20, -1);
      scc[k].assign(0x400, -1);
      pce[k].assign(6 * 16 + 16, -1);
    }
  }
  void opnWrite(int chip, int reg, int val, bool force = false) {
    if (!force && opn[chip][reg] == val) return;
    opn[chip][reg] = val;
    u8((chip ? 0xA2 : 0x52) + ((reg >> 8) & 1));
    u8(reg & 0xFF);
    u8(val);
  }
  void nesWrite(int chip, int reg, int val, bool force = false) {
    if (!force && nes[chip][reg] == val) return;
    nes[chip][reg] = val;
    u8(0xB4);
    u8(reg | (chip ? 0x80 : 0));
    u8(val);
  }
  // SCC: port 0 waveform, 1 frequency, 2 volume, 3 key on (cache index port * 256 + reg).
  void sccWrite(int chip, int port, int reg, int val, bool force = false) {
    int k = port * 256 + reg;
    if (!force && scc[chip][k] == val) return;
    scc[chip][k] = val;
    u8(0xD2);
    u8(port | (chip ? 0x80 : 0));
    u8(reg);
    u8(val);
  }
  void pceRaw(int chip, int reg, int val) {
    u8(0xB9);
    u8(reg | (chip ? 0x80 : 0));
    u8(val);
  }
  void pceSelect(int chip, int ch) {
    if (pceSel[chip] == ch) return;
    pceSel[chip] = ch;
    pceRaw(chip, 0, ch);
  }
  // Channel registers 2-7 of channel ch (selected first), cached per channel.
  void pceWrite(int chip, int ch, int reg, int val, bool force = false) {
    int k = ch * 16 + reg;
    if (!force && pce[chip][k] == val) return;
    pce[chip][k] = val;
    pceSelect(chip, ch);
    pceRaw(chip, reg, val);
  }
  void u8(int v) { data.push_back((uint8_t)v); }
  void wait(int n) {
    while (n > 0) {
      if (n == 735) { u8(0x62); return; }
      if (n == 882) { u8(0x63); return; }
      if (n <= 16) { u8(0x70 + n - 1); return; }
      int k = std::min(n, 65535);
      u8(0x61);
      u8(k & 255);
      u8(k >> 8);
      n -= k;
    }
  }
  void ayWrite(int chip, int reg, int val, bool force = false) {
    if (!force && ay[chip][reg] == val) return;
    ay[chip][reg] = val;
    u8(0xA0);
    u8(reg | (chip ? 0x80 : 0));
    u8(val);
  }
  void snByte(int b) {
    u8(0x50);
    u8(b);
  }
  void gbWrite(int reg, int val, bool force = false) {
    if (!force && gb[reg] == val) return;
    gb[reg] = val;
    u8(0xB3);
    u8(reg);
    u8(val);
  }
  void oplWrite(int reg, int val, bool force = false) {
    if (!force && opl[reg] == val) return;
    opl[reg] = val;
    u8(reg & 0x100 ? 0x5F : 0x5E);
    u8(reg & 0xFF);
    u8(val);
  }
};

void put32(std::vector<uint8_t>& b, size_t at, uint32_t v) {
  for (int i = 0; i < 4; i++) b[at + i] = (uint8_t)(v >> (8 * i));
}

std::vector<uint8_t> gd3(const Song& song) {
  std::vector<uint8_t> s;
  auto str = [&](const std::string& t) {
    // UTF-8 to UTF-16 for the Latin-1 range; other characters become '?'.
    for (size_t i = 0; i < t.size(); i++) {
      unsigned c = (unsigned char)t[i];
      if (c >= 0xC0 && c < 0xE0 && i + 1 < t.size()) {
        c = ((c & 0x1F) << 6) | ((unsigned char)t[++i] & 0x3F);
      } else if (c >= 0x80) {
        while (i + 1 < t.size() && ((unsigned char)t[i + 1] & 0xC0) == 0x80) i++;
        c = '?';
      }
      s.push_back((uint8_t)(c & 255));
      s.push_back((uint8_t)(c >> 8));
    }
    s.push_back(0);
    s.push_back(0);
  };
  str(song.name);
  str("");
  str("");  // game
  str("");
  str("DATRACKARXD");  // system
  str("");
  str(song.author);
  str("");
  str("");  // date
  str("DATRACKARXD " DATRACKARXD_VERSION);
  str(song.comment);
  std::vector<uint8_t> out = {'G', 'd', '3', ' ', 0, 1, 0, 0, 0, 0, 0, 0};
  put32(out, 8, (uint32_t)s.size());
  out.insert(out.end(), s.begin(), s.end());
  return out;
}

}  // namespace

bool exportVGM(const Song& song, const std::string& path, std::string& err, std::string& report) {
  int nch = song.channelCount();
  Vgm v;
  // What the chips can't do: it stays in the tracker only.
  if (song.fx.reverb || song.fx.delay || song.fx.filter) v.dropped["master reverb / delay / filter (not exported)"]++;
  if (std::any_of(song.automation.begin(), song.automation.end(), [](const AutomationLane& l) { return !l.points.empty(); }))
    v.dropped["automation curves (not exported)"]++;
  if (song.subsongCount() > 1) v.dropped["other subsongs (only the current one is exported)"]++;
  for (int c = 0; c < nch; c++)
    if (!song.channels[c].effects.empty()) {
      v.dropped["channel effects (not exported)"]++;
      break;
    }

  // --- Allocate chip voices to the song channels ---
  std::vector<ChannelUse> use(nch);
  for (int c = 0; c < nch; c++) use[c] = channelUse(song, c);
  std::vector<int> ayCh(6, -1), snTone(3, -1), gbCh(4, -1);
  std::vector<int> opnCh(12, -1), sccCh(10, -1), pceCh(12, -1);
  int nesCh[2][5] = {{-1, -1, -1, -1, -1}, {-1, -1, -1, -1, -1}};  // pulse 1, pulse 2, triangle, noise, DPCM
  // PC Engine noise only exists on channels 5 and 6: those go first.
  for (int c = 0; c < nch; c++)
    if (use[c].type == INS_PCE && use[c].pceNoise) {
      for (int k : {4, 5, 10, 11})
        if (pceCh[k] < 0) {
          pceCh[k] = c;
          break;
        }
    }
  int snNoiseCh = -1;
  struct OplSlot {
    int ch = -1;   // song channel
    int port = 0, hw = 0;
    bool four = false;
  };
  std::vector<OplSlot> opl;
  bool anySnNoise = false;
  for (int c = 0; c < nch; c++)
    if (use[c].type == INS_SN && use[c].snNoise) anySnNoise = true;
  // OPL: 4-op voices on the pairs (channels 0-2 with 3-5 of each port),
  // 2-op voices on the channels left.
  std::vector<std::pair<int, int>> pairs = {{0, 0}, {0, 1}, {0, 2}, {1, 0}, {1, 1}, {1, 2}};
  std::vector<std::pair<int, int>> singles = {{0, 6}, {0, 7}, {0, 8}, {1, 6}, {1, 7}, {1, 8}};
  int pairsUsed = 0;
  for (int c = 0; c < nch; c++)
    if (use[c].type == INS_OPL && use[c].oplFour && pairsUsed < 6) {
      opl.push_back({c, pairs[pairsUsed].first, pairs[pairsUsed].second, true});
      pairsUsed++;
    }
  // Pairs not taken by 4-op voices give two 2-op channels each.
  for (int k = pairsUsed; k < 6; k++) {
    singles.push_back({pairs[k].first, pairs[k].second});
    singles.push_back({pairs[k].first, pairs[k].second + 3});
  }
  size_t singleNext = 0;
  int fourOpMask = (1 << pairsUsed) - 1;
  for (int c = 0; c < nch; c++) {
    const ChannelUse& u = use[c];
    bool placed = false;
    if (u.type == INS_AY) {
      for (int k = 0; k < 6 && !placed; k++)
        if (ayCh[k] < 0) ayCh[k] = c, placed = true;
    } else if (u.type == INS_SN) {
      if (u.snNoise) {
        if (snNoiseCh < 0) snNoiseCh = c, placed = true;
      } else {
        for (int k = 0; k < (anySnNoise ? 2 : 3) && !placed; k++)
          if (snTone[k] < 0) snTone[k] = c, placed = true;
      }
    } else if (u.type == INS_GB) {
      if (u.gbKind == 0) {
        for (int k = 0; k < 2 && !placed; k++)
          if (gbCh[k] < 0) gbCh[k] = c, placed = true;
      } else {
        int h = u.gbKind == 1 ? 2 : 3;
        if (gbCh[h] < 0) gbCh[h] = c, placed = true;
      }
    } else if (u.type == INS_OPL) {
      if (u.oplFour) {
        placed = std::any_of(opl.begin(), opl.end(), [&](const OplSlot& s) { return s.ch == c; });
      } else if (singleNext < singles.size()) {
        opl.push_back({c, singles[singleNext].first, singles[singleNext].second, false});
        singleNext++;
        placed = true;
      }
    } else if (u.type == INS_OPN) {
      for (int k = 0; k < 12 && !placed; k++)
        if (opnCh[k] < 0) opnCh[k] = c, placed = true;
    } else if (u.type == INS_NES) {
      for (int chip = 0; chip < 2 && !placed; chip++) {
        if (u.nesKind == 0) {
          for (int k = 0; k < 2 && !placed; k++)
            if (nesCh[chip][k] < 0) nesCh[chip][k] = c, placed = true;
        } else {
          int k = u.nesKind + 1;
          if (nesCh[chip][k] < 0) nesCh[chip][k] = c, placed = true;
        }
      }
    } else if (u.type == INS_SCC) {
      for (int k = 0; k < 10 && !placed; k++)
        if (sccCh[k] < 0) sccCh[k] = c, placed = true;
    } else if (u.type == INS_PCE) {
      if (u.pceNoise) {
        placed = std::find(pceCh.begin(), pceCh.end(), c) != pceCh.end();
      } else {
        for (int k : {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11})
          if (!placed && pceCh[k] < 0) pceCh[k] = c, placed = true;
      }
    } else {
      v.dropped["channels without an emulated chip instrument (left out)"]++;
      continue;
    }
    if (!placed) v.dropped["channels beyond the chips' voices (left out)"]++;
  }
  bool haveAy = std::any_of(ayCh.begin(), ayCh.end(), [](int c) { return c >= 0; });
  bool ay2 = std::any_of(ayCh.begin() + 3, ayCh.end(), [](int c) { return c >= 0; });
  bool haveSn = snNoiseCh >= 0 || std::any_of(snTone.begin(), snTone.end(), [](int c) { return c >= 0; });
  bool haveGb = std::any_of(gbCh.begin(), gbCh.end(), [](int c) { return c >= 0; });
  bool haveOpl = !opl.empty();
  auto any = [](const std::vector<int>& v, size_t from, size_t to) {
    return std::any_of(v.begin() + from, v.begin() + to, [](int c) { return c >= 0; });
  };
  int opnChips = any(opnCh, 6, 12) ? 2 : any(opnCh, 0, 6) ? 1 : 0;
  int sccChips = any(sccCh, 5, 10) ? 2 : any(sccCh, 0, 5) ? 1 : 0;
  int pceChips = any(pceCh, 6, 12) ? 2 : any(pceCh, 0, 6) ? 1 : 0;
  int nesChips = 0;
  for (int chip = 0; chip < 2; chip++)
    for (int k = 0; k < 5; k++)
      if (nesCh[chip][k] >= 0) nesChips = chip + 1;
  if (!haveAy && !haveSn && !haveGb && !haveOpl && !opnChips && !nesChips && !sccChips && !pceChips) {
    err = "No channel plays an emulated chip instrument";
    return false;
  }

  // --- Chips' initial state, before the loop point ---
  if (haveGb) {
    v.gbWrite(0x16, 0x80);  // NR52: sound on
    v.gbWrite(0x14, 0x77);  // NR50: full volume
    v.gbWrite(0x15, 0xFF);  // NR51: every channel to both sides
  }
  if (haveOpl) {
    v.oplWrite(0x105, 0x01);  // OPL3 mode
    v.oplWrite(0x104, fourOpMask);
  }
  if (haveAy) {
    v.ayWrite(0, 7, 0x3F);
    if (ay2) v.ayWrite(1, 7, 0x3F);
  }
  if (haveSn)
    for (int k = 0; k < 4; k++) v.snByte(0x9F | (k << 5));
  for (int chip = 0; chip < opnChips; chip++) {
    v.opnWrite(chip, 0x27, 0x00);
    v.opnWrite(chip, 0x2B, 0x00);  // DAC off
    for (int ch : {0, 1, 2, 4, 5, 6}) v.opnWrite(chip, 0x28, ch, true);
  }
  for (int chip = 0; chip < nesChips; chip++) {
    v.nesWrite(chip, 0x17, 0x40);  // frame counter: 4 steps, no IRQ
    v.nesWrite(chip, 0x15, 0x0F);
    v.nesWrite(chip, 0x01, 0x08);  // sweeps off
    v.nesWrite(chip, 0x05, 0x08);
    v.nesWrite(chip, 0x00, 0x30);
    v.nesWrite(chip, 0x04, 0x30);
    v.nesWrite(chip, 0x08, 0x00);
    v.nesWrite(chip, 0x0C, 0x30);
  }
  for (int chip = 0; chip < sccChips; chip++) v.sccWrite(chip, 3, 0, 0x00);
  for (int chip = 0; chip < pceChips; chip++) {
    v.pceRaw(chip, 1, 0xFF);  // main balance
    for (int ch = 0; ch < 6; ch++) v.pceWrite(chip, ch, 4, 0x00);
  }
  size_t loopAt = v.data.size();

  // --- Play the song once, tick by tick ---
  Engine e(song, 44100);
  e.play(0, 0);
  double clockAy = 0, clockSn = 0;
  int ayType = 0;
  std::vector<uint32_t> shapeSeen(6, UINT32_MAX), snResetSeen(1, UINT32_MAX), gbTrigSeen(4, UINT32_MAX);
  std::vector<uint32_t> oplRetrigSeen(opl.size(), UINT32_MAX);
  std::vector<bool> gbSilent(4, true);
  std::vector<int> gbSoftSeen(4, -2);
  std::vector<std::vector<int>> gbWaveSeen(1);
  double clockOpn = 0, clockNes = 0;
  std::vector<uint32_t> opnRetrigSeen(12, UINT32_MAX), nesTrigSeen(10, UINT32_MAX);
  std::vector<bool> opnKeyOn(12, false), nesSilent(10, true);
  std::vector<std::vector<int>> pceWaveSeen(12);
  std::map<std::vector<uint8_t>, int> dpcmAddr;
  size_t dpcmNext = 0xC000;
  bool sccShared = false;
  double samplesAcc = 0;
  uint32_t totalSamples = 0;
  const double maxSamples = 44100.0 * 60 * 20;
  bool mixedTypes = false;

  auto voiceRegs = [&](int c, int type, ChipRegs& r) {
    if (c < 0) return false;
    const ChannelState& s = e.channel(c);
    const ChipVoice* voice = e.chipVoice(c);
    if (!voice || song.channels[c].muted) return false;
    if (voice->type() != type) {
      mixedTypes = true;
      return false;
    }
    voice->regs(r);
    return s.active && s.targetGain > 0;
  };

  while (e.loops() < 1 && e.playing() && totalSamples < maxSamples) {
    e.tick();

    // AY / YM2149.
    for (int chip = 0; chip < (ay2 ? 2 : haveAy ? 1 : 0); chip++) {
      int mixer = 0x3F;
      for (int k = 0; k < 3; k++) {
        int slot = chip * 3 + k;
        ChipRegs r;
        bool on = voiceRegs(ayCh[slot], INS_AY, r);
        if (ayCh[slot] >= 0 && r.type == INS_AY && clockAy == 0) {
          clockAy = r.clock;
          ayType = r.ym ? 0x10 : 0x00;
        }
        if (!on) {
          v.ayWrite(chip, 8 + k, 0);
          continue;
        }
        v.ayWrite(chip, 2 * k, r.ayPeriod & 0xFF);
        v.ayWrite(chip, 2 * k + 1, (r.ayPeriod >> 8) & 15);
        if (r.ayTone) mixer &= ~(1 << k);
        if (r.ayNoiseOn) {
          mixer &= ~(8 << k);
          v.ayWrite(chip, 6, r.ayNoise & 31);  // shared: the last voice wins
        }
        if (r.ayVol == 16) {
          v.ayWrite(chip, 11, r.ayEnvPeriod & 0xFF);
          v.ayWrite(chip, 12, (r.ayEnvPeriod >> 8) & 0xFF);
          if (shapeSeen[slot] != r.ayShapeWrites) {
            shapeSeen[slot] = r.ayShapeWrites;
            v.ayWrite(chip, 13, r.ayEnvShape & 15, true);  // restarts the envelope
          }
          v.ayWrite(chip, 8 + k, 0x10);
        } else {
          v.ayWrite(chip, 8 + k, r.ayVol & 15);
        }
      }
      v.ayWrite(chip, 7, mixer);
    }

    // SN76489: tones, and the noise clocked by tone 3.
    if (haveSn) {
      auto tone = [&](int k, int period, int atten) {
        period = std::clamp(period, 1, 1023);
        if (v.sn[k] != period) {
          v.sn[k] = period;
          v.snByte(0x80 | (k << 5) | (period & 15));
          v.snByte((period >> 4) & 0x3F);
        }
        if (v.sn[4 + k] != atten) {
          v.sn[4 + k] = atten;
          v.snByte(0x90 | (k << 5) | atten);
        }
      };
      for (int k = 0; k < 3; k++) {
        if (snNoiseCh >= 0 && k == 2) continue;
        ChipRegs r;
        bool on = voiceRegs(snTone[k], INS_SN, r);
        if (snTone[k] >= 0 && r.type == INS_SN && clockSn == 0) clockSn = r.clock;
        if (on) tone(k, r.snPeriod, r.snAtten);
        else if (v.sn[4 + k] != 15) tone(k, std::max(1, v.sn[k]), 15);
      }
      if (snNoiseCh >= 0) {
        ChipRegs r;
        bool on = voiceRegs(snNoiseCh, INS_SN, r);
        if (r.type == INS_SN && clockSn == 0) clockSn = r.clock;
        tone(2, on ? r.snPeriod : std::max(1, v.sn[2]), 15);  // tone 3 only clocks the noise
        int mode = 0xE0 | (r.snWhite ? 4 : 0) | 3;
        if (on && (v.sn[3] != mode || snResetSeen[0] != r.snNoiseResets)) {
          v.sn[3] = mode;
          snResetSeen[0] = r.snNoiseResets;
          v.snByte(mode);
        }
        int atten = on ? r.snAtten : 15;
        if (v.sn[7] != atten) {
          v.sn[7] = atten;
          v.snByte(0xF0 | atten);
        }
      }
    }

    // Game Boy: NRxx registers at 0xFF10 + reg.
    for (int h = 0; h < 4 && haveGb; h++) {
      if (gbCh[h] < 0) continue;
      ChipRegs r;
      bool on = voiceRegs(gbCh[h], INS_GB, r) && r.type == INS_GB;
      static const int BASE[4] = {0x00, 0x05, 0x0A, 0x0F};  // NR10, NR20, NR30, NR40
      int b = BASE[h];
      if (!on) {
        if (!gbSilent[h]) {
          if (h == 2) v.gbWrite(0x0A, 0x00);  // wave DAC off
          else v.gbWrite(b + 2, 0x00);        // envelope 0: DAC off
          gbSilent[h] = true;
        }
        continue;
      }
      bool trigger = gbTrigSeen[h] != r.gbTriggers || gbSilent[h];
      gbTrigSeen[h] = r.gbTriggers;
      int env = r.gbSoftVol >= 0 ? (r.gbSoftVol << 4) : (r.gbEnvVol << 4) | (r.gbEnvDir << 3) | r.gbEnvLen;
      if (r.gbSoftVol >= 0 && gbSoftSeen[h] != r.gbSoftVol) trigger = true;  // software envelope: rewrite and restart
      gbSoftSeen[h] = r.gbSoftVol;
      if (h < 2) {
        if (h == 0) v.gbWrite(0x00, r.gbSweep);
        v.gbWrite(b + 1, (r.gbDuty & 3) << 6);
        if (trigger) v.gbWrite(b + 2, env, true);
        v.gbWrite(b + 3, r.gbFreq & 0xFF);
        v.gbWrite(b + 4, (trigger ? 0x80 : 0) | ((r.gbFreq >> 8) & 7), trigger);
      } else if (h == 2) {
        std::vector<int> wave(r.gbWave, r.gbWave + WAVETABLE_LEN);
        if (gbWaveSeen[0] != wave) {
          // Wave RAM is only written with the DAC off, then restarted.
          gbWaveSeen[0] = wave;
          v.gbWrite(0x0A, 0x00, true);
          for (int i = 0; i < 16; i++) v.gbWrite(0x20 + i, (wave[i * 2] << 4) | wave[i * 2 + 1], true);
          trigger = true;
        }
        v.gbWrite(0x0A, 0x80);
        v.gbWrite(0x0C, (r.gbWaveVol & 3) << 5);
        v.gbWrite(0x0D, r.gbFreq & 0xFF);
        v.gbWrite(0x0E, (trigger ? 0x80 : 0) | ((r.gbFreq >> 8) & 7), trigger);
      } else {
        if (trigger) v.gbWrite(0x11, env, true);
        v.gbWrite(0x12, (r.gbNoiseShift << 4) | (r.gbShort ? 8 : 0) | (r.gbNoiseDiv & 7));
        if (trigger) v.gbWrite(0x13, 0x80, true);
      }
      gbSilent[h] = false;
    }

    // OPL3.
    for (size_t k = 0; k < opl.size(); k++) {
      const OplSlot& slot = opl[k];
      ChipRegs r;
      bool on = voiceRegs(slot.ch, INS_OPL, r) && r.type == INS_OPL;
      static const int SLOTS[9] = {0, 1, 2, 8, 9, 10, 16, 17, 18};
      static const int BASE[5] = {0x20, 0x40, 0x60, 0x80, 0xE0};
      int port = slot.port << 8;
      int nops = slot.four ? 4 : 2;
      int opSlot[4] = {SLOTS[slot.hw], SLOTS[slot.hw] + 3, slot.four ? SLOTS[slot.hw + 3] : 0, slot.four ? SLOTS[slot.hw + 3] + 3 : 0};
      int keyReg = port | (0xB0 + slot.hw);
      if (r.type != INS_OPL) continue;
      if (!on) {
        // Cut: key off with the carriers silent.
        if (v.opl[keyReg] > 0 && (v.opl[keyReg] & 0x20)) v.oplWrite(keyReg, v.opl[keyReg] & ~0x20);
        continue;
      }
      for (int op = 0; op < nops; op++)
        for (int j = 0; j < 5; j++) v.oplWrite(port | (BASE[j] + opSlot[op]), r.oplOp[op][j]);
      v.oplWrite(port | (0xC0 + slot.hw), r.oplC0 | 0x30);
      if (slot.four) v.oplWrite(port | (0xC0 + slot.hw + 3), r.oplC3 | 0x30);
      v.oplWrite(0xBD, r.oplBD);
      v.oplWrite(port | (0xA0 + slot.hw), r.oplFnum & 0xFF);
      int b0 = ((r.oplFnum >> 8) & 3) | (r.oplBlock << 2);
      if (oplRetrigSeen[k] != r.oplRetriggers) {
        // The note restarts: key off for a sample first.
        if (oplRetrigSeen[k] != UINT32_MAX && v.opl[keyReg] > 0 && (v.opl[keyReg] & 0x20)) {
          v.oplWrite(keyReg, b0, true);
          v.wait(1);
          samplesAcc -= 1;
          totalSamples += 1;
        }
        oplRetrigSeen[k] = r.oplRetriggers;
      }
      v.oplWrite(keyReg, b0 | (r.oplKey ? 0x20 : 0));
    }

    // YM2612: channels 1-3 on port 0, 4-6 on port 1.
    for (int k = 0; k < opnChips * 6; k++) {
      if (opnCh[k] < 0) continue;
      int chip = k / 6, port = (k % 6) / 3, c = k % 3;
      int keyCode = port * 4 + c, base = port << 8;
      ChipRegs r;
      bool on = voiceRegs(opnCh[k], INS_OPN, r) && r.type == INS_OPN;
      if (r.type != INS_OPN) continue;
      if (clockOpn == 0) clockOpn = r.clock;
      if (!on) {
        if (opnKeyOn[k]) v.opnWrite(chip, 0x28, keyCode, true);
        opnKeyOn[k] = false;
        continue;
      }
      static const int SLOT[4] = {0, 8, 4, 12};
      for (int op = 0; op < 4; op++)
        for (int j = 0; j < 7; j++) v.opnWrite(chip, base | (0x30 + j * 0x10 + SLOT[op] + c), r.opnOp[op][j]);
      v.opnWrite(chip, base | (0xB0 + c), r.opnB0);
      int pan = song.channels[opnCh[k]].pan;
      int lr = pan < 64 ? 0x80 : pan > 192 ? 0x40 : 0xC0;
      v.opnWrite(chip, base | (0xB4 + c), lr | (r.opnB4 & 0x3F));
      v.opnWrite(chip, 0x22, r.opnLfo);  // shared: the last voice wins
      int a4 = (r.opnBlock << 3) | (r.opnFnum >> 8), a0 = r.opnFnum & 0xFF;
      if (v.opn[chip][base | (0xA4 + c)] != a4 || v.opn[chip][base | (0xA0 + c)] != a0) {
        v.opnWrite(chip, base | (0xA4 + c), a4, true);  // A4 is latched by the A0 write
        v.opnWrite(chip, base | (0xA0 + c), a0, true);
      }
      if (opnRetrigSeen[k] != r.opnRetriggers) {
        if (opnRetrigSeen[k] != UINT32_MAX && opnKeyOn[k]) {
          v.opnWrite(chip, 0x28, keyCode, true);
          v.wait(1);
          samplesAcc -= 1;
          totalSamples += 1;
          opnKeyOn[k] = false;
        }
        opnRetrigSeen[k] = r.opnRetriggers;
      }
      if (r.opnKey != opnKeyOn[k]) v.opnWrite(chip, 0x28, (r.opnKey ? 0xF0 : 0) | keyCode, true);
      opnKeyOn[k] = r.opnKey;
    }

    // NES 2A03: $4000 + register.
    for (int chip = 0; chip < nesChips; chip++) {
      for (int k = 0; k < 5; k++) {
        int c = nesCh[chip][k];
        if (c < 0) continue;
        int kind = k < 2 ? 0 : k - 1;
        int b = k < 2 ? k * 4 : k == 2 ? 0x08 : k == 3 ? 0x0C : 0x10;
        ChipRegs r;
        bool on = voiceRegs(c, INS_NES, r) && r.type == INS_NES;
        if (r.type != INS_NES) continue;
        if (clockNes == 0) clockNes = r.clock;
        if (r.nesKind != kind) {
          if (on) v.dropped["notes of another NES channel kind (silent)"]++;
          on = false;
        }
        int slot = chip * 5 + k;
        if (kind == 3) {
          // DPCM: the sample goes to the 2A03's memory ($C000-$FFFF) once.
          if (on && r.nesDpcm && !r.nesDpcm->empty() && nesTrigSeen[slot] != r.nesTriggers) {
            nesTrigSeen[slot] = r.nesTriggers;
            const std::vector<uint8_t>& d = *r.nesDpcm;
            auto it = dpcmAddr.find(d);
            int addr = -1;
            if (it != dpcmAddr.end()) {
              addr = it->second;
            } else if (chip == 0) {
              size_t len = ((d.size() - 1 + 15) / 16) * 16 + 1;  // 16n + 1 bytes
              if (dpcmNext + len <= 0x10000) {
                addr = (int)dpcmNext;
                dpcmAddr[d] = addr;
                std::vector<uint8_t> block = d;
                block.resize(len, 0x55);  // silence: up and down
                v.u8(0x67);
                v.u8(0x66);
                v.u8(0xC2);
                uint32_t size = (uint32_t)block.size() + 2;
                for (int i = 0; i < 4; i++) v.u8((size >> (8 * i)) & 0xFF);
                v.u8(addr & 0xFF);
                v.u8(addr >> 8);
                v.data.insert(v.data.end(), block.begin(), block.end());
                dpcmNext = (addr + len + 63) / 64 * 64;
              } else {
                v.dropped["DPCM samples beyond the 2A03's 16 KB (silent)"]++;
              }
            } else {
              v.dropped["DPCM on the second 2A03 (silent)"]++;
            }
            if (addr >= 0) {
              size_t len = ((d.size() - 1 + 15) / 16) * 16 + 1;
              v.nesWrite(chip, 0x10, (r.nesDpcmLoop ? 0x40 : 0) | (r.nesDpcmRate & 15));
              v.nesWrite(chip, 0x11, r.nesDpcmStart & 0x7F, true);
              v.nesWrite(chip, 0x12, (addr - 0xC000) / 64);
              v.nesWrite(chip, 0x13, (int)((len - 1) / 16));
              v.nesWrite(chip, 0x15, 0x0F, true);  // stop first: a playing sample ignores a restart
              v.nesWrite(chip, 0x15, 0x1F, true);
              nesSilent[slot] = false;
            }
          } else if (!on && !nesSilent[slot]) {
            v.nesWrite(chip, 0x15, 0x0F, true);  // stop
            nesSilent[slot] = true;
          }
          continue;
        }
        if (!on) {
          if (!nesSilent[slot]) {
            v.nesWrite(chip, b, kind == 1 ? 0x00 : 0x30);  // triangle: linear counter to 0, then it stops
            nesSilent[slot] = true;
          }
          continue;
        }
        bool restart = nesTrigSeen[slot] != r.nesTriggers || nesSilent[slot];
        nesTrigSeen[slot] = r.nesTriggers;
        v.nesWrite(chip, b, r.nesReg[0]);
        if (kind == 0) v.nesWrite(chip, b + 1, r.nesReg[1]);
        v.nesWrite(chip, b + 2, r.nesReg[2]);
        v.nesWrite(chip, b + 3, r.nesReg[3], restart);
        nesSilent[slot] = false;
      }
    }

    // Konami SCC.
    for (int chip = 0; chip < sccChips; chip++) {
      int keys = 0;
      for (int k = 0; k < 5; k++) {
        int c = sccCh[chip * 5 + k];
        if (c < 0) continue;
        ChipRegs r;
        bool on = voiceRegs(c, INS_SCC, r) && r.type == INS_SCC;
        if (!on) {
          v.sccWrite(chip, 2, k, 0);
          continue;
        }
        // Channels 4 and 5 share the last waveform.
        int w = std::min(k, 3);
        if (k == 4 && sccCh[chip * 5 + 3] >= 0) {
          bool same = true;
          for (int i = 0; i < 32; i++) same &= v.scc[chip][w * 32 + i] == (r.sccWave[i] & 0xFF);
          if (!same) sccShared = true;
        }
        for (int i = 0; i < 32; i++) v.sccWrite(chip, 0, w * 32 + i, r.sccWave[i] & 0xFF);
        v.sccWrite(chip, 1, k * 2, r.sccPeriod & 0xFF);
        v.sccWrite(chip, 1, k * 2 + 1, (r.sccPeriod >> 8) & 15);
        v.sccWrite(chip, 2, k, r.sccVol & 15);
        keys |= 1 << k;
      }
      v.sccWrite(chip, 3, 0, keys);
    }

    // PC Engine (HuC6280).
    for (int chip = 0; chip < pceChips; chip++) {
      for (int k = 0; k < 6; k++) {
        int c = pceCh[chip * 6 + k];
        if (c < 0) continue;
        ChipRegs r;
        bool on = voiceRegs(c, INS_PCE, r) && r.type == INS_PCE;
        if (!on) {
          v.pceWrite(chip, k, 4, 0x00);
          continue;
        }
        std::vector<int> wave(r.pceWave, r.pceWave + 32);
        if (!r.pceNoise && pceWaveSeen[chip * 6 + k] != wave) {
          // Wave RAM: DDA on then off resets the write index; the channel stays off meanwhile.
          pceWaveSeen[chip * 6 + k] = wave;
          v.pceWrite(chip, k, 4, 0x40, true);
          v.pceWrite(chip, k, 4, 0x00, true);
          for (int i = 0; i < 32; i++) v.pceWrite(chip, k, 6, wave[i] & 31, true);
        }
        v.pceWrite(chip, k, 2, r.pcePeriod & 0xFF);
        v.pceWrite(chip, k, 3, (r.pcePeriod >> 8) & 15);
        int pan = song.channels[c].pan;
        int l = pan <= 128 ? 15 : (int)std::lround(15.0 * (255 - pan) / 127);
        int rr = pan >= 128 ? 15 : (int)std::lround(15.0 * pan / 128);
        v.pceWrite(chip, k, 5, (l << 4) | rr);
        if (k >= 4) v.pceWrite(chip, k, 7, r.pceNoise ? 0x80 | (r.pceNoiseFreq & 31) : 0x00);
        v.pceWrite(chip, k, 4, 0x80 | (r.pceVol & 31));
      }
    }

    samplesAcc += 44100.0 / std::max(e.tickRate(), 1.0f);
    int n = (int)samplesAcc;
    if (n > 0) {
      v.wait(n);
      samplesAcc -= n;
      totalSamples += n;
    }
  }
  v.u8(0x66);  // end of data
  if (mixedTypes) v.dropped["notes of another chip on a channel (silent)"]++;
  if (sccShared) v.dropped["SCC: channels 4 and 5 share their waveform"]++;

  // --- Header ---
  std::vector<uint8_t> out(0x100, 0);
  out[0] = 'V';
  out[1] = 'g';
  out[2] = 'm';
  out[3] = ' ';
  put32(out, 0x08, 0x171);
  if (haveSn) {
    put32(out, 0x0C, (uint32_t)std::lround(clockSn > 0 ? clockSn : 3579545));
    out[0x28] = 0x09;  // Sega noise feedback
    out[0x29] = 0x00;
    out[0x2A] = 16;    // shift register width
  }
  put32(out, 0x18, totalSamples);
  put32(out, 0x1C, (uint32_t)(0x100 + loopAt - 0x1C));
  put32(out, 0x20, totalSamples);
  put32(out, 0x34, 0x100 - 0x34);
  if (haveOpl) put32(out, 0x5C, 14318180);
  if (haveAy) {
    put32(out, 0x74, (uint32_t)std::lround(clockAy > 0 ? clockAy : 1773400) | (ay2 ? 0x40000000u : 0));
    out[0x78] = (uint8_t)ayType;
    out[0x79] = 0x01;  // legacy output
  }
  if (haveGb) put32(out, 0x80, 4194304);
  if (opnChips) put32(out, 0x2C, (uint32_t)std::lround(clockOpn > 0 ? clockOpn : 7670453) | (opnChips > 1 ? 0x40000000u : 0));
  if (nesChips) put32(out, 0x84, (uint32_t)std::lround(clockNes > 0 ? clockNes : 1789773) | (nesChips > 1 ? 0x40000000u : 0));
  if (sccChips) put32(out, 0x9C, 1789773u |  // the SCC clock as VGM counts it (CPU clock / 2)
                     (sccChips > 1 ? 0x40000000u : 0));
  if (pceChips) put32(out, 0xA4, 3579545u | (pceChips > 1 ? 0x40000000u : 0));
  out.insert(out.end(), v.data.begin(), v.data.end());
  size_t gd3At = out.size();
  std::vector<uint8_t> tag = gd3(song);
  out.insert(out.end(), tag.begin(), tag.end());
  put32(out, 0x14, (uint32_t)(gd3At - 0x14));
  put32(out, 0x04, (uint32_t)(out.size() - 4));

  std::ofstream f(path, std::ios::binary);
  if (!f) {
    err = "Cannot open " + path + " for writing";
    return false;
  }
  f.write((const char*)out.data(), (std::streamsize)out.size());
  if (!f) {
    err = "Write error on " + path;
    return false;
  }
  std::ostringstream ss;
  for (auto& [what, n] : v.dropped) ss << n << " x " << what << "\n";
  report = ss.str();
  return true;
}
