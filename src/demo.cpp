// A small keygen-style demo song, built in code so the tracker starts with music.
#include "demo.h"

namespace {

Macro mac(std::vector<int> values, int loop = -1, int release = -1) {
  Macro m;
  m.values = std::move(values);
  m.loop = loop;
  m.release = release;
  return m;
}

void put(Song& s, int ch, int pat, int row, int note, int ins = -1, int vol = -1, int cmd = -1, int val = -1) {
  Cell& c = s.pattern(ch, pat, true)->rows[row];
  c.note = (int16_t)note;
  c.ins = (int16_t)ins;
  c.vol = (int16_t)vol;
  c.fx[0].cmd = (int16_t)cmd;
  c.fx[0].val = (int16_t)val;
}

// Note helper: n(9, 4) = A-4.
constexpr int n(int semitone, int octave) { return octave * 12 + semitone; }
constexpr int C = 0, D = 2, E = 4, F = 5, G = 7, A = 9, B = 11;

struct MelodyNote {
  int row, note;
};

}  // namespace

void loadDemoSong(Song& s) {
  s.reset(8);
  s.name = "Keygen Groove";
  s.author = "DATRACKARXD";
  s.speed = 6;
  s.tickRate = 60;
  s.patternLength = 64;

  const char* names[8] = {"Lead", "Arp", "Bass", "Drums", "Hats", "Echo", "Free 1", "Free 2"};
  for (int i = 0; i < 8; i++) s.channels[i].name = names[i];
  s.channels[0].effectCols = 2;
  s.channels[5].effectCols = 2;

  s.instruments.clear();
  {
    Instrument i;
    i.name = "Lead";
    i.wave = WAVE_PULSE;
    i.duty = 1;
    i.macros[MACRO_VOL] = mac({15, 14, 13, 12, 12, 11, 11, 10, 10, 9});
    i.macros[MACRO_DUTY] = mac({2, 2, 1});
    s.instruments.push_back(i);
  }
  {
    Instrument i;
    i.name = "Arp pluck";
    i.wave = WAVE_PULSE;
    i.duty = 0;
    i.volume = 11;
    i.macros[MACRO_VOL] = mac({15, 13, 11, 10, 9, 8, 7, 7, 6, 6, 5});
    s.instruments.push_back(i);
  }
  {
    Instrument i;
    i.name = "Bass";
    i.wave = WAVE_PULSE;
    i.duty = 2;
    i.macros[MACRO_VOL] = mac({15, 14, 12, 11, 10, 9, 8});
    i.macros[MACRO_DUTY] = mac({3, 2});
    s.instruments.push_back(i);
  }
  {
    Instrument i;
    i.name = "Kick";
    i.wave = WAVE_TRIANGLE;
    i.macros[MACRO_VOL] = mac({15, 15, 14, 12, 9, 6, 3, 0});
    i.macros[MACRO_ARP] = mac({24, 14, 7, 2, 0, -2, -4, -5});
    s.instruments.push_back(i);
  }
  {
    Instrument i;
    i.name = "Snare";
    i.wave = WAVE_NOISE;
    i.duty = 0;
    i.macros[MACRO_VOL] = mac({15, 13, 10, 8, 6, 4, 3, 2, 1, 0});
    i.macros[MACRO_WAVE] = mac({WAVE_TRIANGLE, WAVE_NOISE});
    i.macros[MACRO_ARP] = mac({-12, 0});
    s.instruments.push_back(i);
  }
  {
    Instrument i;
    i.name = "Hihat";
    i.wave = WAVE_NOISE;
    i.duty = 1;
    i.volume = 9;
    i.macros[MACRO_VOL] = mac({10, 6, 3, 1, 0});
    s.instruments.push_back(i);
  }
  {
    Instrument i;
    i.name = "Wave pad";
    i.wave = WAVE_TABLE;
    for (int k = 0; k < WAVETABLE_LEN; k++) i.wavetable[k] = k < 16 ? k : 15 - (k - 16) / 2;
    i.macros[MACRO_VOL] = mac({4, 7, 10, 12, 12, 11, 10, 9, 8, 6, 4, 2, 0}, -1, 4);
    s.instruments.push_back(i);
  }
  const int LEAD = 0, ARP = 1, BASS = 2, KICK = 3, SNARE = 4, HAT = 5;

  // Chord progression: Am - F - C - G, 16 rows each.
  const int roots[4] = {n(A, 2), n(F, 2), n(C, 3), n(G, 2)};
  const int arpNotes[4] = {n(A, 4), n(F, 4), n(C, 5), n(G, 4)};
  const int arpFx[4] = {0x37, 0x47, 0x47, 0x47};

  for (int chord = 0; chord < 4; chord++) {
    int base = chord * 16;
    for (int r = 0; r < 16; r += 4) put(s, 1, 0, base + r, arpNotes[chord], ARP, -1, 0x00, arpFx[chord]);
    for (int r = 0; r < 16; r += 2) put(s, 2, 0, base + r, roots[chord] + ((r / 2) % 2 ? 12 : 0), BASS);
    // Drums: kick on 0, 8, 10 and snare on 4, 12.
    put(s, 3, 0, base + 0, n(C, 3), KICK);
    put(s, 3, 0, base + 4, n(C, 6), SNARE);
    put(s, 3, 0, base + 8, n(C, 3), KICK);
    put(s, 3, 0, base + 10, n(C, 3), KICK);
    put(s, 3, 0, base + 12, n(C, 6), SNARE);
    // Hats on every 8th, accented off-beats.
    for (int r = 0; r < 16; r += 2) put(s, 4, 1, base + r, n(C, 8), HAT, r % 4 == 2 ? 0x7f : 0x40);
  }

  const MelodyNote melodyA[] = {
      {0, n(A, 5)},  {3, n(G, 5)},  {6, n(E, 5)},  {8, n(D, 5)},  {10, n(E, 5)}, {12, n(C, 5)}, {14, n(D, 5)},
      {16, n(A, 5)}, {19, n(G, 5)}, {22, n(F, 5)}, {24, n(E, 5)}, {26, n(F, 5)}, {28, n(A, 5)}, {30, n(C, 6)},
      {32, n(G, 5)}, {38, n(E, 5)}, {40, n(G, 5)}, {42, n(A, 5)}, {44, n(G, 5)}, {46, n(E, 5)},
      {48, n(D, 5)}, {51, n(E, 5)}, {54, n(G, 5)}, {56, n(B, 5)}, {58, n(A, 5)}, {60, n(G, 5)}, {62, n(D, 5)},
  };
  const MelodyNote melodyBEnd[] = {
      {48, n(E, 5)}, {50, n(G, 5)}, {52, n(A, 5)}, {54, n(B, 5)},
      {56, n(C, 6)}, {58, n(B, 5)}, {60, n(G, 5)}, {62, n(E, 5)},
  };

  for (int pat = 1; pat <= 2; pat++) {
    for (const MelodyNote& m : melodyA) {
      if (pat == 2 && m.row >= 48) continue;
      put(s, 0, pat, m.row, m.note, LEAD);
    }
    if (pat == 2)
      for (const MelodyNote& m : melodyBEnd) put(s, 0, pat, m.row, m.note, LEAD);
    Pattern* lead = s.pattern(0, pat, true);
    lead->rows[0].fx[0] = {0x04, 0x44};  // vibrato
    lead->rows[0].fx[1] = {0x08, 0xFC};  // slightly left

    // Echo channel: the lead delayed by 3 rows, quieter and panned right.
    Pattern* echo = s.pattern(5, pat, true);
    for (int r = 0; r < 64; r++) {
      const Cell& src = lead->rows[r];
      if (src.note < 0) continue;
      int dst = (r + 3) % 64;
      echo->rows[dst].note = src.note;
      echo->rows[dst].ins = LEAD;
      echo->rows[dst].vol = 0x30;
    }
    echo->rows[0].fx[1] = {0x08, 0x5F};
    echo->rows[0].fx[0] = {0x04, 0x44};
  }

  // The end of the second melody loops back to order 1 (skipping the intro).
  s.pattern(0, 2, true)->rows[63].fx[1] = {0x0B, 0x01};

  // Wave pad swell on the free channel during the second melody.
  put(s, 6, 1, 0, n(A, 3), 6, 0x60);
  put(s, 6, 1, 28, NOTE_OFF);
  put(s, 6, 1, 32, n(C, 4), 6, 0x60);
  put(s, 6, 1, 60, NOTE_OFF);

  s.orders.clear();
  //            lead arp bass drm hat echo pad
  s.orders.push_back({0, 0, 0, 0, 0, 0, 0, 0});
  s.orders.push_back({1, 0, 0, 0, 1, 1, 0, 0});
  s.orders.push_back({2, 0, 0, 0, 1, 2, 1, 0});
}
