// Song data model: instruments, patterns, orders, file I/O.
#pragma once
#include <array>
#include <cstdint>
#include <string>
#include <vector>

constexpr int MAX_CHANNELS = 16;
constexpr int MAX_PATTERNS = 256;
constexpr int MAX_ROWS = 256;
constexpr int MAX_EFFECTS = 4;
constexpr int MAX_INSTRUMENTS = 256;
constexpr int MAX_ORDERS = 256;
constexpr int WAVETABLE_LEN = 32;
constexpr int MAX_MACRO_LEN = 128;

// Cell note values. 0..119 are C-0..B-9.
constexpr int16_t NOTE_EMPTY = -1;
constexpr int16_t NOTE_OFF = -2;

enum WaveType : int {
  WAVE_PULSE = 0,
  WAVE_TRIANGLE,
  WAVE_SAW,
  WAVE_NOISE,
  WAVE_SINE,
  WAVE_TABLE,
  WAVE_COUNT
};
extern const char* const WAVE_NAMES[WAVE_COUNT];

struct Effect {
  int16_t cmd = -1;  // -1 = empty
  int16_t val = -1;
};

struct Cell {
  int16_t note = NOTE_EMPTY;
  int16_t ins = -1;
  int16_t vol = -1;  // 00..7F
  Effect fx[MAX_EFFECTS];
  bool empty() const;
};

struct Pattern {
  std::vector<Cell> rows;  // MAX_ROWS cells once allocated, empty until then
  bool allocated() const { return !rows.empty(); }
  bool isEmpty() const;
};

enum MacroType : int { MACRO_VOL = 0, MACRO_ARP, MACRO_DUTY, MACRO_WAVE, MACRO_PITCH, MACRO_COUNT };
extern const char* const MACRO_NAMES[MACRO_COUNT];
extern const int MACRO_MIN[MACRO_COUNT];
extern const int MACRO_MAX[MACRO_COUNT];

struct Macro {
  std::vector<int> values;
  int loop = -1;     // index to loop back to, -1 = no loop
  int release = -1;  // macro holds here until note off, -1 = none
};

struct Instrument {
  std::string name = "Instrument";
  int wave = WAVE_PULSE;
  int duty = 2;  // 0=12.5% 1=25% 2=50% 3=75%
  int volume = 15;  // 0..15
  std::array<int, WAVETABLE_LEN> wavetable{};  // 0..15
  Macro macros[MACRO_COUNT];
  Instrument();
};

struct ChannelInfo {
  std::string name;
  int effectCols = 1;
  bool muted = false;
  std::vector<Pattern> patterns;  // MAX_PATTERNS entries
};

struct Song {
  std::string name = "Untitled";
  std::string author;
  float tickRate = 60.0f;  // ticks per second
  int speed = 6;           // ticks per row
  int patternLength = 64;
  int highlight1 = 4;
  int highlight2 = 16;
  std::vector<ChannelInfo> channels;
  std::vector<std::array<uint8_t, MAX_CHANNELS>> orders;
  std::vector<Instrument> instruments;

  Song();
  void reset(int channelCount = 8);
  void setChannelCount(int n);
  int channelCount() const { return (int)channels.size(); }
  // Returns the pattern, allocating it when create is true. Null if not allocated.
  Pattern* pattern(int ch, int idx, bool create);
  const Pattern* pattern(int ch, int idx) const;
  Cell* cell(int ch, int order, int row, bool create);
  const Cell* cell(int ch, int order, int row) const;

  bool save(const std::string& path, std::string& err) const;
  bool load(const std::string& path, std::string& err);
};

std::string noteName(int note);  // "C-4", "OFF", "---"
