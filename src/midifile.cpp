// Standard MIDI files: import (notes quantized to rows, one song channel per
// voice) and export (the song played by the engine, one track per channel).
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <map>

#include "engine.h"
#include "import.h"
#include "midifile.h"

namespace {

// ---------------------------------------------------------------------------
// Import
// ---------------------------------------------------------------------------

struct MidiNote {
  uint32_t on = 0, off = 0;  // MIDI ticks
  int key = 60, vel = 100;
};

struct Part {
  int track = 0, channel = 0, program = 0;
  std::string name;
  std::vector<MidiNote> notes;
};

struct Tempo {
  uint32_t tick;
  double bpm;
};

uint32_t readVarLen(ByteReader& r, size_t end) {
  uint32_t v = 0;
  for (int i = 0; i < 4 && r.pos() < end; i++) {
    uint8_t b = r.u8();
    v = (v << 7) | (b & 0x7f);
    if (!(b & 0x80)) break;
  }
  return v;
}

uint32_t be32(ByteReader& r) {
  uint32_t v = 0;
  for (int i = 0; i < 4; i++) v = (v << 8) | r.u8();
  return v;
}

const char* const FAMILY_NAMES[16] = {"Piano", "Chromatic", "Organ", "Guitar", "Bass", "Strings", "Ensemble", "Brass",
                                      "Reed", "Pipe", "Synth lead", "Synth pad", "Synth FX", "Ethnic", "Percussive", "Sound FX"};

Macro mac(std::initializer_list<int> v, int loop = -1, int release = -1) {
  Macro m;
  m.values = v;
  m.loop = loop;
  m.release = release;
  return m;
}

// A chip instrument in the spirit of the General MIDI program.
Instrument gmInstrument(int program) {
  Instrument i;
  int fam = std::clamp(program, 0, 127) / 8;
  i.name = FAMILY_NAMES[fam];
  switch (fam) {
    case 0:  // piano
      i.duty = 1;
      i.macros[MACRO_VOL] = mac({15, 14, 13, 12, 11, 10, 10, 9, 9, 8, 8, 7});
      i.fadeout = 0.08f;
      break;
    case 1:  // bells, mallets
      i.duty = 0;
      i.macros[MACRO_VOL] = mac({15, 12, 10, 8, 7, 6, 5, 4, 3, 3, 2, 2, 1, 1, 0});
      break;
    case 2:  // organ
      i.duty = 2;
      i.volume = 12;
      break;
    case 3:  // guitar
      i.duty = 0;
      i.macros[MACRO_VOL] = mac({15, 13, 11, 10, 9, 8, 7, 6, 6, 5, 5, 4});
      i.fadeout = 0.1f;
      break;
    case 4:  // bass
      i.wave = WAVE_TRIANGLE;
      break;
    case 5:
    case 6:  // strings
      i.wave = WAVE_SAW;
      i.macros[MACRO_VOL] = mac({5, 8, 10, 11, 12});
      i.fadeout = 0.04f;
      break;
    case 7:  // brass
      i.duty = 2;
      i.macros[MACRO_VOL] = mac({10, 13, 15, 14, 13});
      i.fadeout = 0.1f;
      break;
    case 8:  // reed
      i.duty = 1;
      i.macros[MACRO_VOL] = mac({10, 13, 14});
      i.fadeout = 0.1f;
      break;
    case 9:  // pipe
      i.wave = WAVE_SINE;
      i.macros[MACRO_VOL] = mac({9, 12, 14});
      i.fadeout = 0.1f;
      break;
    case 10:  // synth lead
      if (program % 8 == 0) i.duty = 2;
      else i.wave = WAVE_SAW;
      break;
    case 11:  // synth pad
      i.wave = WAVE_SINE;
      i.macros[MACRO_VOL] = mac({4, 6, 8, 10, 11, 12});
      i.fadeout = 0.03f;
      break;
    default:
      i.duty = 1;
      i.macros[MACRO_VOL] = mac({15, 13, 11, 10, 9, 8});
      i.fadeout = 0.1f;
      break;
  }
  return i;
}

// General MIDI drum kit (channel 10) on chip drums.
enum Drum { KICK, SNARE, HAT, OPEN_HAT, TOM, CRASH, DRUM_COUNT };

Instrument drumInstrument(int d) {
  Instrument i;
  switch (d) {
    case KICK:
      i.name = "Kick";
      i.wave = WAVE_TRIANGLE;
      i.macros[MACRO_VOL] = mac({15, 15, 14, 12, 9, 6, 3, 0});
      i.macros[MACRO_ARP] = mac({24, 14, 7, 2, 0, -2, -4, -5});
      break;
    case SNARE:
      i.name = "Snare";
      i.wave = WAVE_NOISE;
      i.duty = 0;
      i.macros[MACRO_VOL] = mac({15, 13, 10, 8, 6, 4, 3, 2, 1, 0});
      i.macros[MACRO_WAVE] = mac({WAVE_TRIANGLE, WAVE_NOISE});
      i.macros[MACRO_ARP] = mac({-12, 0});
      break;
    case HAT:
      i.name = "Hihat";
      i.wave = WAVE_NOISE;
      i.duty = 1;
      i.volume = 9;
      i.macros[MACRO_VOL] = mac({10, 6, 3, 1, 0});
      break;
    case OPEN_HAT:
      i.name = "Open hat";
      i.wave = WAVE_NOISE;
      i.duty = 1;
      i.macros[MACRO_VOL] = mac({10, 9, 8, 7, 6, 5, 4, 3, 3, 2, 2, 1, 1, 0});
      break;
    case TOM:
      i.name = "Tom";
      i.wave = WAVE_TRIANGLE;
      i.macros[MACRO_VOL] = mac({15, 14, 12, 10, 8, 6, 4, 2, 0});
      i.macros[MACRO_ARP] = mac({7, 4, 2, 0, -1, -2, -3, -4});
      break;
    default:
      i.name = "Crash";
      i.wave = WAVE_NOISE;
      i.duty = 0;
      i.macros[MACRO_VOL] = mac({14, 13, 12, 11, 10, 9, 8, 8, 7, 7, 6, 6, 5, 5, 4, 4, 3, 3, 2, 2, 1, 1, 0});
      break;
  }
  return i;
}

// Drum and pitch for a GM drum key.
void drumFor(int key, int& drum, int& note) {
  switch (key) {
    case 35: case 36: drum = KICK; note = 36; break;
    case 42: case 44: drum = HAT; note = 96; break;
    case 46: drum = OPEN_HAT; note = 96; break;
    case 41: case 43: case 45: case 47: case 48: case 50: drum = TOM; note = key - 12; break;
    case 49: case 51: case 52: case 53: case 55: case 57: case 59: drum = CRASH; note = 84; break;
    default: drum = SNARE; note = 72; break;
  }
}

}  // namespace

bool importMIDI(const std::vector<uint8_t>& data, Song& song, std::string& err) {
  ByteReader r(data);
  if (r.str(4) != "MThd") {
    err = "Not a MIDI file";
    return false;
  }
  uint32_t hdrLen = be32(r);
  size_t hdrEnd = 8 + hdrLen;
  r.u16be();  // format: 0, 1 or 2 are all read as parallel tracks
  int ntracks = r.u16be();
  int division = r.u16be();
  if (division <= 0 || (division & 0x8000)) division = 96;  // SMPTE time: assume 96 per beat
  r.seek(hdrEnd);

  std::vector<Part> parts;
  std::map<std::pair<int, int>, int> partOf;  // (track, channel) -> part
  std::vector<Tempo> tempos;
  std::vector<std::string> trackNames;

  for (int t = 0; t < ntracks && r.left() >= 8; t++) {
    std::string id = r.str(4);
    uint32_t len = be32(r);
    size_t end = std::min(r.pos() + len, data.size());
    if (id != "MTrk") {
      r.seek(end);
      continue;
    }
    std::string trackName;
    int program[16] = {};
    std::map<int, size_t> open[16];  // key -> note index in its part (sounding notes)
    uint32_t tick = 0;
    int status = 0;
    auto partFor = [&](int ch) {
      auto key = std::make_pair(t, ch);
      auto it = partOf.find(key);
      if (it != partOf.end()) return it->second;
      Part p;
      p.track = t;
      p.channel = ch;
      p.program = program[ch];
      parts.push_back(p);
      return partOf[key] = (int)parts.size() - 1;
    };
    auto noteOff = [&](int ch, int key) {
      auto it = open[ch].find(key);
      if (it == open[ch].end()) return;
      parts[partFor(ch)].notes[it->second].off = tick;
      open[ch].erase(it);
    };
    while (r.pos() < end) {
      tick += readVarLen(r, end);
      int b = r.u8();
      if (b < 0x80) {  // running status
        if (!status) break;
        r.seek(r.pos() - 1);
        b = status;
      }
      if (b == 0xff) {
        int type = r.u8();
        uint32_t mlen = readVarLen(r, end);
        size_t mend = std::min(r.pos() + mlen, end);
        if (type == 0x51 && mlen >= 3) {
          uint32_t us = (r.u8() << 16) | (r.u8() << 8) | r.u8();
          if (us > 0) tempos.push_back({tick, 60000000.0 / us});
        } else if (type == 0x03) {
          trackName = r.str(mlen);
        } else if (type == 0x2f) {
          r.seek(mend);
          break;
        }
        r.seek(mend);
        continue;
      }
      if (b == 0xf0 || b == 0xf7) {  // sysex
        uint32_t slen = readVarLen(r, end);
        r.skip(slen);
        continue;
      }
      status = b;
      int type = b & 0xf0, ch = b & 15;
      int d1 = r.u8() & 0x7f;
      int d2 = (type == 0xc0 || type == 0xd0) ? 0 : r.u8() & 0x7f;
      if (type == 0x90 && d2 > 0) {
        noteOff(ch, d1);  // a repeated key ends the previous one
        int p = partFor(ch);
        MidiNote n;
        n.on = n.off = tick;
        n.key = d1;
        n.vel = d2;
        parts[p].notes.push_back(n);
        open[ch][d1] = parts[p].notes.size() - 1;
      } else if (type == 0x80 || type == 0x90) {
        noteOff(ch, d1);
      } else if (type == 0xc0) {
        program[ch] = d1;
        auto it = partOf.find({t, ch});
        if (it != partOf.end() && parts[it->second].notes.empty()) parts[it->second].program = d1;
      }
    }
    for (int ch = 0; ch < 16; ch++)
      for (auto& [key, idx] : open[ch]) parts[partFor(ch)].notes[idx].off = tick;
    trackNames.push_back(trackName);
    // The track name names its part, unless the track holds several.
    int inTrack = 0;
    for (auto& p : parts) inTrack += p.track == t && !p.notes.empty();
    for (auto& p : parts)
      if (p.track == t && p.name.empty() && inTrack == 1) p.name = trackName;
    r.seek(end);
  }
  parts.erase(std::remove_if(parts.begin(), parts.end(), [](const Part& p) { return p.notes.empty(); }), parts.end());
  if (parts.empty()) {
    err = "The MIDI file has no notes";
    return false;
  }

  // Grid: 4 rows per beat (16ths), or 8 when the notes don't fit 16ths.
  // Rows * speed = 24 ticks per beat, so tick rate = BPM * 0.4 and F0xx = BPM.
  int offGrid = 0, total = 0;
  for (auto& p : parts)
    for (auto& n : p.notes) {
      double pos = n.on * 4.0 / division;
      offGrid += std::fabs(pos - std::round(pos)) > 0.2;
      total++;
    }
  int rowsPerBeat = offGrid * 100 > total * 15 ? 8 : 4;
  auto rowOf = [&](uint32_t tick) { return (int)std::lround(tick * (double)rowsPerBeat / division); };

  std::sort(tempos.begin(), tempos.end(), [](const Tempo& a, const Tempo& b) { return a.tick < b.tick; });
  double bpm = tempos.empty() || tempos[0].tick > 0 ? 120.0 : tempos[0].bpm;

  song = Song();
  song.name = trackNames.empty() || trackNames[0].empty() ? "MIDI import" : trackNames[0];
  song.speeds = {24 / rowsPerBeat};
  song.tickRate = (float)(bpm * 0.4);
  song.highlight1 = rowsPerBeat;
  song.highlight2 = rowsPerBeat * 4;
  song.patternLength = 64;
  song.instruments.clear();

  // Instruments: one per melodic part, a shared drum kit for channel 10.
  int drumIns[DRUM_COUNT];
  std::fill(drumIns, drumIns + DRUM_COUNT, -1);
  std::vector<int> partIns(parts.size(), -1);
  for (size_t p = 0; p < parts.size(); p++) {
    if (parts[p].channel == 9) continue;
    if ((int)song.instruments.size() >= MAX_INSTRUMENTS) break;
    Instrument ins = gmInstrument(parts[p].program);
    if (!parts[p].name.empty()) ins.name = parts[p].name;
    partIns[p] = (int)song.instruments.size();
    song.instruments.push_back(ins);
  }

  // Voices: each part gets as many channels as notes it plays at once.
  struct Placed {
    int start, end, note, ins, vol;
  };
  struct Voice {
    int part;
    bool drums;
    std::vector<Placed> notes;
  };
  std::vector<Voice> voices;
  int maxRow = 0;
  for (size_t p = 0; p < parts.size(); p++) {
    Part& part = parts[p];
    bool drums = part.channel == 9;
    std::stable_sort(part.notes.begin(), part.notes.end(), [](const MidiNote& a, const MidiNote& b) {
      return a.on != b.on ? a.on < b.on : a.key > b.key;
    });
    std::vector<int> mine;  // voice indexes of this part
    for (const MidiNote& n : part.notes) {
      Placed pl;
      pl.start = rowOf(n.on);
      pl.end = drums ? pl.start + 1 : std::max(rowOf(n.off), pl.start + 1);
      pl.vol = std::clamp(n.vel, 1, 127);
      if (drums) {
        int d;
        drumFor(n.key, d, pl.note);
        if (drumIns[d] < 0 && (int)song.instruments.size() < MAX_INSTRUMENTS) {
          drumIns[d] = (int)song.instruments.size();
          song.instruments.push_back(drumInstrument(d));
        }
        pl.ins = drumIns[d];
      } else {
        pl.note = std::clamp(n.key - 12, 0, NOTE_COUNT - 1);  // MIDI 60 = C-4
        pl.ins = partIns[p];
      }
      if (pl.ins < 0) continue;
      int v = -1;
      for (int k : mine) {
        const Placed& last = voices[k].notes.back();
        if (last.start == pl.start && !drums) continue;  // chord note: next voice
        if (last.end <= pl.start || (drums && last.start < pl.start)) {
          v = k;
          break;
        }
      }
      if (v < 0) {
        voices.push_back({(int)p, drums, {}});
        v = (int)voices.size() - 1;
        mine.push_back(v);
      }
      // A note still sounding on this voice is cut short.
      if (!voices[v].notes.empty()) voices[v].notes.back().end = std::min(voices[v].notes.back().end, pl.start);
      voices[v].notes.push_back(pl);
      maxRow = std::max(maxRow, pl.end);
    }
  }

  // Too many voices: keep the busiest ones.
  int dropped = 0;
  if ((int)voices.size() > MAX_CHANNELS) {
    std::vector<int> order(voices.size());
    for (size_t i = 0; i < order.size(); i++) order[i] = (int)i;
    std::stable_sort(order.begin(), order.end(), [&](int a, int b) { return voices[a].notes.size() > voices[b].notes.size(); });
    std::vector<bool> keep(voices.size(), false);
    for (int i = 0; i < MAX_CHANNELS; i++) keep[order[i]] = true;
    std::vector<Voice> kept;
    for (size_t i = 0; i < voices.size(); i++) {
      if (keep[i]) kept.push_back(std::move(voices[i]));
      else dropped += (int)voices[i].notes.size();
    }
    voices = std::move(kept);
  }

  int numOrders = std::clamp((maxRow + 1 + 63) / 64, 1, MAX_ORDERS);
  int maxRows = numOrders * 64;
  song.setChannelCount((int)voices.size());
  song.orders.assign(numOrders, {});
  for (int o = 0; o < numOrders; o++)
    for (int c = 0; c < MAX_CHANNELS; c++) song.orders[o][c] = (uint8_t)o;

  std::map<int, int> partVoiceCount;
  for (size_t c = 0; c < voices.size(); c++) {
    const Voice& v = voices[c];
    const Part& part = parts[v.part];
    ChannelInfo& info = song.channels[c];
    int k = ++partVoiceCount[v.part];
    std::string base = part.name.empty() ? (v.drums ? "Drums" : FAMILY_NAMES[part.program / 8]) : part.name;
    info.name = k > 1 ? base + " " + std::to_string(k) : base;
    for (size_t i = 0; i < v.notes.size(); i++) {
      const Placed& n = v.notes[i];
      if (n.start >= maxRows) break;
      Cell* cell = song.cell((int)c, n.start / 64, n.start % 64, true);
      cell->note = (int16_t)n.note;
      cell->ins = (int16_t)n.ins;
      cell->vol = (int16_t)n.vol;
      bool nextAtEnd = i + 1 < v.notes.size() && v.notes[i + 1].start <= n.end;
      if (!v.drums && !nextAtEnd && n.end < maxRows) {
        Cell* off = song.cell((int)c, n.end / 64, n.end % 64, true);
        if (off->note == NOTE_EMPTY) off->note = NOTE_OFF;
      }
    }
  }

  // Tempo changes become F0xx (BPM) on the first channel.
  if (!voices.empty()) {
    for (const Tempo& tp : tempos) {
      int row = rowOf(tp.tick);
      if (row == 0 || row >= maxRows) continue;
      Cell* cell = song.cell(0, row / 64, row % 64, true);
      cell->fx[0] = {0xF0, (int16_t)std::clamp((int)std::lround(tp.bpm), 1, 255)};
    }
  }
  if (song.instruments.empty()) song.instruments.emplace_back();
  if (dropped) song.comment = "MIDI import: " + std::to_string(dropped) + " notes dropped (more than 32 voices).";
  return true;
}

// ---------------------------------------------------------------------------
// Export
// ---------------------------------------------------------------------------

namespace {

struct TrackWriter {
  std::vector<uint8_t> data;
  uint32_t lastTick = 0;
  void varLen(uint32_t v) {
    uint8_t buf[5];
    int n = 0;
    buf[n++] = v & 0x7f;
    while (v >>= 7) buf[n++] = 0x80 | (v & 0x7f);
    while (n) data.push_back(buf[--n]);
  }
  void event(uint32_t tick, std::initializer_list<uint8_t> bytes) {
    varLen(tick > lastTick ? tick - lastTick : 0);
    lastTick = std::max(lastTick, tick);
    data.insert(data.end(), bytes);
  }
  void meta(uint32_t tick, uint8_t type, const std::string& s) {
    varLen(tick > lastTick ? tick - lastTick : 0);
    lastTick = std::max(lastTick, tick);
    data.push_back(0xff);
    data.push_back(type);
    varLen((uint32_t)s.size());
    data.insert(data.end(), s.begin(), s.end());
  }
};

void put32be(std::ofstream& f, uint32_t v) {
  char b[4] = {(char)(v >> 24), (char)(v >> 16), (char)(v >> 8), (char)v};
  f.write(b, 4);
}

// GM program for a chip instrument (noise goes to the drum channel instead).
int gmProgram(const Instrument& ins, int note) {
  if (ins.type == INS_FM) return 81;
  if (ins.type == INS_SAMPLE) return 0;
  switch (ins.wave) {
    case WAVE_TRIANGLE: return note < 48 ? 33 : 73;
    case WAVE_SAW: return 81;
    case WAVE_SINE: return 73;
    case WAVE_TABLE: return 88;
    default: return 80;
  }
}

}  // namespace

bool exportMIDI(const Song& song, const std::string& path, int loops, std::string& err) {
  const int PPQ = 480;
  // The song's starting tempo (a beat = Highlight 1 rows), so rows fall on
  // the beat grid when the file is opened in a sequencer.
  double bpm = song.tickRate * 60.0 / (std::max(song.speed(), 1) * std::max(song.highlight1, 1));
  bpm = std::clamp(bpm, 20.0, 400.0);
  const double TICKS_PER_SEC = PPQ * bpm / 60.0;
  uint32_t us = (uint32_t)std::lround(60000000.0 / bpm);
  int nch = song.channelCount();
  std::vector<TrackWriter> tracks(nch + 1);
  tracks[0].meta(0, 0x03, song.name);
  tracks[0].event(0, {0xff, 0x51, 0x03, (uint8_t)(us >> 16), (uint8_t)(us >> 8), (uint8_t)us});
  tracks[0].event(0, {0xff, 0x58, 0x04, 0x04, 0x02, 0x18, 0x08});

  // MIDI channel per song channel, skipping the drum channel.
  std::vector<int> midiCh(nch);
  for (int c = 0; c < nch; c++) {
    int m = c % 15;
    midiCh[c] = m >= 9 ? m + 1 : m;
    tracks[c + 1].meta(0, 0x03, song.channels[c].name);
  }

  Engine engine(song, 44100);
  engine.play(0, 0);
  struct Sounding {
    int midiCh = -1, key = -1;
    uint32_t triggers = 0;
    int program = -1;
  };
  std::vector<Sounding> state(nch);
  double t = 0;
  const double maxSeconds = 60 * 20;
  auto noteOff = [&](int c, uint32_t tick) {
    if (state[c].key < 0) return;
    tracks[c + 1].event(tick, {(uint8_t)(0x80 | state[c].midiCh), (uint8_t)state[c].key, 0x40});
    state[c].key = -1;
  };
  while (engine.loops() < std::max(loops, 1) && t < maxSeconds && engine.playing()) {
    engine.tick();
    uint32_t tick = (uint32_t)std::lround(t * TICKS_PER_SEC);
    for (int c = 0; c < nch; c++) {
      const ChannelState& s = engine.channel(c);
      bool trig = s.triggers != state[c].triggers;
      state[c].triggers = s.triggers;
      bool sounding = s.active && !s.released;
      if (trig) noteOff(c, tick);
      else if (!sounding) noteOff(c, tick);
      if (!trig || !s.active || song.channels[c].muted || s.note < 0) continue;
      const Instrument* ins = s.ins >= 0 && s.ins < (int)song.instruments.size() ? &song.instruments[s.ins] : nullptr;
      bool drum = ins && ins->type == INS_STANDARD && (ins->wave == WAVE_NOISE || s.wave == WAVE_NOISE);
      int ch = drum ? 9 : midiCh[c];
      int key = std::clamp(s.note + 12, 0, 127);
      if (drum) key = s.note >= 84 ? 42 : 38;
      else if (ins) {
        int prog = gmProgram(*ins, s.note);
        if (prog != state[c].program) {
          tracks[c + 1].event(tick, {(uint8_t)(0xc0 | ch), (uint8_t)prog});
          state[c].program = prog;
        }
      }
      int vel = std::clamp(s.vol, 1, 127);
      tracks[c + 1].event(tick, {(uint8_t)(0x90 | ch), (uint8_t)key, (uint8_t)vel});
      state[c].midiCh = ch;
      state[c].key = key;
    }
    t += 1.0 / std::max(engine.tickRate(), 1.0f);
  }
  uint32_t endTick = (uint32_t)std::lround(t * TICKS_PER_SEC);
  for (int c = 0; c < nch; c++) noteOff(c, endTick);
  for (auto& tr : tracks) tr.event(endTick, {0xff, 0x2f, 0x00});

  std::ofstream f(path, std::ios::binary);
  if (!f) {
    err = "Cannot write " + path;
    return false;
  }
  f.write("MThd", 4);
  put32be(f, 6);
  char hdr[6] = {0, 1, (char)((nch + 1) >> 8), (char)((nch + 1) & 255), (char)(PPQ >> 8), (char)(PPQ & 255)};
  f.write(hdr, 6);
  for (auto& tr : tracks) {
    f.write("MTrk", 4);
    put32be(f, (uint32_t)tr.data.size());
    f.write((const char*)tr.data.data(), tr.data.size());
  }
  if (!f) {
    err = "Write error on " + path;
    return false;
  }
  return true;
}
