// Song data model: instruments, samples, patterns, orders, file I/O.
#pragma once
#include <array>
#include <cstdint>
#include <string>
#include <vector>

constexpr int MAX_CHANNELS = 32;
constexpr int MAX_PATTERNS = 256;
constexpr int MAX_ROWS = 256;
constexpr int MAX_EFFECTS = 8;
constexpr int MAX_INSTRUMENTS = 256;
constexpr int MAX_SAMPLES = 256;
constexpr int MAX_WAVETABLES = 256;
constexpr int MAX_ORDERS = 256;
constexpr int WAVETABLE_LEN = 32;
constexpr int MAX_MACRO_LEN = 256;
constexpr int MAX_GROOVE = 16;
constexpr int NOTE_COUNT = 120;

// Arpeggio macro values at or above this play a fixed note (value - ARP_FIXED).
constexpr int ARP_FIXED = 1000;

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

enum InstrumentType : int { INS_STANDARD = 0, INS_FM, INS_SAMPLE, INS_TYPE_COUNT };
extern const char* const INS_TYPE_NAMES[INS_TYPE_COUNT];

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

struct Macro {
  std::vector<int> values;
  int loop = -1;     // index to loop back to, -1 = no loop
  int release = -1;  // macro holds here until note off, -1 = none
};

// One FM operator, with Yamaha OPN ranges.
struct FMOperator {
  bool enabled = true;
  int mult = 1;  // 0-15 (0 = x0.5)
  int dt = 3;    // 0-7, 3 = no detune
  int tl = 0;    // 0-127 attenuation, 0.75 dB steps
  int ar = 31;   // 0-31
  int dr = 0;    // 0-31
  int sl = 0;    // 0-15
  int d2r = 0;   // 0-31
  int rr = 15;   // 0-15
  int ksr = 0;   // 0-3 rate key scaling
  int ssg = 0;   // SSG-EG, 0 = off, 8-15 = modes
};

// 4-operator FM, operators in logical order (op 1 to op 4).
struct FMParams {
  int alg = 4;  // 0-7
  int fb = 0;   // 0-7
  FMOperator ops[4];
};

struct Sample {
  std::string name = "Sample";
  std::vector<float> data;  // mono, -1..1
  int rate = 44100;         // playback rate at C-5
  int loopStart = 0, loopEnd = 0;
  int loopMode = 0;  // 0 none, 1 forward, 2 ping-pong, 3 backward
  int volume = 64;   // default volume 0-64
};
extern const char* const LOOP_NAMES[4];

struct Wavetable {
  std::vector<float> data;  // -1..1, any length
};

struct Instrument {
  std::string name = "Instrument";
  int type = INS_STANDARD;
  int wave = WAVE_PULSE;
  int duty = 2;     // 0=12.5% 1=25% 2=50% 3=75%
  int volume = 15;  // 0..15
  std::array<int, WAVETABLE_LEN> wavetable{};  // 0..15
  Macro macros[MACRO_COUNT];
  FMParams fm;
  int songWave = -1;              // song wavetable used instead of the own one (-1 = own)
  int sample = -1;                // default sample (sample instruments)
  std::vector<int16_t> sampleMap;  // empty, or NOTE_COUNT sample indexes (-1 = default)
  float fadeout = 0;               // volume lost per tick after note off (0 = none)
  Instrument();
  void macroRange(int macro, int& lo, int& hi) const;
};

struct ChannelInfo {
  std::string name;
  int effectCols = 1;
  bool muted = false;
  int pan = 128;         // initial panning, 0 = left, 128 = center, 255 = right
  int forceWave = -1;    // waveform this channel always uses (-1 = from instrument)
  int volSlideUnit = 512;  // 0Axy step, in 1/256 of a 00-7F volume unit (512 = MOD-like)
  int fixedDuty = -1;      // duty this channel always uses (-1 = from instrument)
  int minNote = 0;         // lowest pitch the channel can play (like a chip's range)
  float mix = 1.0f;        // channel output level
  std::vector<Pattern> patterns;  // MAX_PATTERNS entries
};

struct Song {
  std::string name = "Untitled";
  std::string author;
  std::string comment;
  float tickRate = 60.0f;   // ticks per second
  std::vector<int> speeds;  // ticks per row, cycled row by row (groove)
  int patternLength = 64;
  int highlight1 = 4;
  int highlight2 = 16;
  bool insResetsVolume = false;  // MOD/XM/IT behavior: an instrument number resets the volume
  std::vector<ChannelInfo> channels;
  std::vector<std::array<uint8_t, MAX_CHANNELS>> orders;
  std::vector<Instrument> instruments;
  std::vector<Sample> samples;
  std::vector<Wavetable> wavetables;

  Song();
  void reset(int channelCount = 8);
  void setChannelCount(int n);
  int channelCount() const { return (int)channels.size(); }
  int speed() const { return speeds.empty() ? 6 : speeds[0]; }
  // Returns the pattern, allocating it when create is true. Null if not allocated.
  Pattern* pattern(int ch, int idx, bool create);
  const Pattern* pattern(int ch, int idx) const;
  Cell* cell(int ch, int order, int row, bool create);
  const Cell* cell(int ch, int order, int row) const;
  // Removes patterns that no order uses and empty patterns.
  void cleanup();

  bool save(const std::string& path, std::string& err) const;
  bool load(const std::string& path, std::string& err);
};

std::string noteName(int note);  // "C-4", "OFF", "---"

// Reads a WAV file into a sample. Returns false and sets err on failure.
bool loadWavSample(const std::string& path, Sample& out, std::string& err);
