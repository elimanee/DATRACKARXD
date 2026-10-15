// Song data model: instruments, samples, patterns, orders, file I/O.
#pragma once
#include <algorithm>
#include <array>
#include <cstdint>
#include <iosfwd>
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

enum InstrumentType : int {
  INS_STANDARD = 0, INS_FM, INS_SAMPLE, INS_AY, INS_SN, INS_GB, INS_OPL, INS_OPN, INS_NES, INS_SCC, INS_PCE, INS_TYPE_COUNT
};
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

enum MacroType : int { MACRO_VOL = 0, MACRO_ARP, MACRO_DUTY, MACRO_WAVE, MACRO_PITCH, MACRO_CUTOFF, MACRO_COUNT };
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

// Emulated sound chips (one chip voice per channel).
struct ChipSettings {
  int clock = 0;  // the chip's clock, see chipClockNames()
  // AY-3-8910 / YM2149 (ZX Spectrum, Amstrad CPC, Atari ST, MSX).
  int ayModel = 0;  // 0 AY-3-8910, 1 YM2149
  bool ayTone = true, ayNoise = false, ayEnv = false;
  int ayNoiseFreq = 16;  // noise period 0-31 (also the Duty macro)
  int ayEnvShape = 8;    // envelope shape 0-15 (8 saw down, 10 triangle, 12 saw up)
  int ayEnvAuto = 3;     // envelope follows the note: 0 manual, 1..5 = x1/4, x1/2, x1, x2, x4
  int ayEnvPeriod = 1000;  // manual envelope period 1-65535
  // SN76489 (Master System, Game Gear, BBC Micro).
  bool snNoise = false;  // the noise channel instead of a tone
  bool snWhite = true;   // white noise, else periodic (tonal) noise
  // Game Boy: the instrument's volume is the envelope's start level.
  int gbChannel = 0;   // 0 pulse, 1 wave (the instrument's wavetable), 2 noise
  int gbEnvDir = 0;    // 0 down, 1 up
  int gbEnvLen = 2;    // envelope step time 0-7 (0 = no change)
  int gbSweepTime = 0, gbSweepDir = 0, gbSweepShift = 0;  // pulse frequency sweep
  bool gbShortNoise = false;  // 7-bit noise (metallic)
  // YM2612 / YM3438 (Mega Drive): the operators are the instrument's FM settings.
  int opnModel = 0;  // 0 YM2612 (with its "ladder" distortion), 1 YM3438 (clean)
  int opnLfo = 0;    // chip LFO: 0 off, 1-8 = speeds 3.98 to 72.2 Hz
  int opnAms = 0;    // LFO depth on the volume 0-3, on the operators of opnAmOps
  int opnFms = 0;    // LFO depth on the pitch 0-7
  int opnAmOps = 0;  // bit per operator (op 1 = bit 0)
  // NES 2A03.
  int nesChannel = 0;  // 0 pulse, 1 triangle, 2 noise, 3 DPCM (plays the instrument's sample)
  bool nesEnv = false;  // hardware volume decay instead of a constant volume
  int nesEnvPeriod = 4; // 0-15, decay step = (period + 1) / 240 s
  bool nesEnvLoop = false;
  bool nesSweep = false;  // pulse pitch sweep
  int nesSweepPeriod = 3, nesSweepShift = 3;  // 0-7
  bool nesSweepDown = false;
  bool nesShortNoise = false;  // 93-step noise (metallic, tonal)
  // PC Engine (HuC6280): channels 5 and 6 can play noise.
  bool pceNoise = false;
};

// OPL3 (Yamaha YMF262, AdLib / Sound Blaster) instrument: 2 or 4 operators.
struct OPLOperator {
  int am = 0, vib = 0, sus = 1, ksr = 0;
  int mult = 1;  // 0-15
  int ksl = 0;   // 0-3
  int tl = 0;    // 0-63 attenuation
  int ar = 15, dr = 4, sl = 4, rr = 6;  // 0-15
  int ws = 0;    // waveform 0-7
};
struct OPLParams {
  bool fourOp = false;
  int alg = 0;  // 2-op: 0 FM, 1 additive; 4-op: 0-3
  int fb = 0;   // 0-7, on operator 1
  bool deepAm = false, deepVib = false;
  OPLOperator ops[4];
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

// SID-style resonant filter on the instrument's output (any instrument type).
struct InsFilter {
  bool on = false;
  int mode = 0;        // 0 low-pass, 1 band-pass, 2 high-pass
  int cutoff = 128;    // 0..255, exponential from ~30 Hz to ~18 kHz
  int resonance = 8;   // 0..15
};

// ADSR volume envelope, on top of the instrument's own volume (any type).
struct InsEnvelope {
  bool on = false;
  float attack = 0.0f;   // seconds from silence to full
  float decay = 0.3f;    // seconds to settle on the sustain level
  float sustain = 0.7f;  // 0..1
  float release = 0.3f;  // seconds to fade out after note off
};

enum LfoTarget : int { LFO_PITCH = 0, LFO_VOLUME, LFO_PULSE, LFO_CUTOFF, LFO_PAN, LFO_TARGET_COUNT };
enum LfoShape : int { LFO_SINE = 0, LFO_TRIANGLE, LFO_SAW, LFO_SQUARE, LFO_RANDOM, LFO_SHAPE_COUNT };
extern const char* const LFO_TARGET_NAMES[LFO_TARGET_COUNT];
extern const char* const LFO_SHAPE_NAMES[LFO_SHAPE_COUNT];

// Low-frequency oscillator restarted by each note. depth is in the target's
// unit: semitones (pitch), 0..1 (volume, pulse width, pan), 0..255 (cutoff).
struct InsLfo {
  bool on = false;
  int target = LFO_PITCH;
  int shape = LFO_SINE;
  float rate = 5.0f;   // Hz
  float depth = 0.3f;
  float delay = 0.0f;  // seconds before it fades in
  bool isDefault() const { return !on && target == LFO_PITCH && shape == LFO_SINE && rate == 5.0f && depth == 0.3f && delay == 0.0f; }
};
constexpr int INS_LFOS = 2;

// Wavetable morph: the wave moves from the instrument's wavetable to a second
// one over time (chip wavetable instruments).
struct InsMorph {
  bool on = false;
  std::array<int, WAVETABLE_LEN> table{};  // 0..15, the wave it morphs to
  float time = 1.0f;  // seconds for one way
  int mode = 0;       // 0 once, 1 loop, 2 ping-pong
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
  ChipSettings chip;
  OPLParams opl;
  int songWave = -1;              // song wavetable used instead of the own one (-1 = own)
  int sample = -1;                // default sample (sample instruments)
  std::vector<int16_t> sampleMap;  // empty, or NOTE_COUNT sample indexes (-1 = default)
  float fadeout = 0;               // volume lost per tick after note off (0 = none)
  InsFilter filter;
  bool ringMod = false;  // multiply by the previous channel's square (SID ring mod)
  bool sync = false;     // restart the waveform with the previous channel (hard sync)
  int pulseWidth = -1;   // 1..255 fine pulse width (duty macro and 12xx too); -1 = 4 duty steps
  int pwmSweep = 0;      // pulse width change per tick, bouncing between the ends
  InsEnvelope env;
  InsLfo lfo[INS_LFOS];
  InsMorph morph;
  bool sidDefault() const {
    return !filter.on && !ringMod && !sync && pulseWidth < 0 && pwmSweep == 0 && filter.cutoff == 128 && filter.resonance == 8 &&
           filter.mode == 0;
  }
  Instrument();
  void macroRange(int macro, int& lo, int& hi) const;
};

// Per-channel insert effects, run in order on the channel before the mixer.
enum ChanFxType : int { CFX_BITCRUSH = 0, CFX_DISTORTION, CFX_CHORUS, CFX_EQ, CFX_COMPRESSOR, CFX_COUNT };
constexpr int MAX_CHANNEL_FX = 8;
struct ChanFxParam {
  const char* name;
  float min, max, def;
  const char* format;   // slider format
  bool log;             // logarithmic slider
  const char* choices;  // a combo instead of a slider ("A\0B\0"), or null
};
struct ChanFxInfo {
  const char* name;
  int count;
  ChanFxParam params[4];
};
extern const ChanFxInfo CHAN_FX_INFO[CFX_COUNT];

struct ChannelEffect {
  int type = CFX_BITCRUSH;
  bool on = true;
  float p[4] = {0, 0, 0, 0};
  static ChannelEffect make(int type);  // with the default settings
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
  float reverbSend = 0.0f;   // 0..1, how much goes to the master reverb
  float delaySend = 0.0f;    // 0..1, how much goes to the master delay
  std::vector<ChannelEffect> effects;  // insert effects, up to MAX_CHANNEL_FX
  std::vector<Pattern> patterns;  // MAX_PATTERNS entries
};

// Master effects: sends from the channels to a reverb and a delay, then a
// filter and the master volume.
struct MasterFx {
  bool reverb = false;
  float roomSize = 0.7f, damping = 0.5f, reverbLevel = 0.35f;
  bool delay = false;
  int delayRows = 3;  // echo time, follows the song speed
  float delayFeedback = 0.35f, delayLevel = 0.35f;
  bool pingPong = true;
  bool filter = false;
  int filterType = 0;  // 0 low-pass, 1 band-pass, 2 high-pass
  float cutoff = 6000.0f, resonance = 0.2f;
  float volume = 1.0f;
};

// Automation: a curve over the song's timeline for a channel or master setting.
enum AutoTarget : int {
  AUTO_VOLUME = 0,
  AUTO_PAN,
  AUTO_CUTOFF,
  AUTO_REVERB,
  AUTO_DELAY,
  AUTO_FX,
  AUTO_MASTER_VOLUME,
  AUTO_MASTER_CUTOFF,
  AUTO_TARGET_COUNT
};
extern const char* const AUTO_TARGET_NAMES[AUTO_TARGET_COUNT];

struct AutoPoint {
  float pos;    // in orders: 2.5 = halfway through order 2
  float value;  // 0..1
};

struct AutomationLane {
  int target = AUTO_VOLUME;
  int channel = 0;              // channel targets
  int fxSlot = 0, fxParam = 0;  // AUTO_FX: which insert effect parameter
  bool on = true;
  std::vector<AutoPoint> points;  // sorted by pos
  bool isMaster() const { return target == AUTO_MASTER_VOLUME || target == AUTO_MASTER_CUTOFF; }
  // Linear between points, held before the first and after the last; NaN without points.
  float valueAt(float pos) const;
  void sort();
};

// A song can hold several subsongs: each has its own orders, patterns, tempo
// and automation; instruments, samples and channel settings are shared.
struct SubSong {
  std::string name;
  float tickRate = 60.0f;
  std::vector<int> speeds{6};
  int patternLength = 64;
  std::vector<std::array<uint8_t, MAX_CHANNELS>> orders;
  std::vector<std::vector<Pattern>> patterns;  // per channel, MAX_PATTERNS entries
  std::vector<AutomationLane> automation;
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
  MasterFx fx;
  std::vector<AutomationLane> automation;  // of the current subsong

  // Subsongs. The current one lives in the fields above (orders, patterns in
  // the channels, tempo, automation); its entry in subsongs only keeps a slot.
  std::string subsongName;
  std::vector<SubSong> subsongs;
  int curSubsong = 0;
  int subsongCount() const { return std::max(1, (int)subsongs.size()); }
  std::string subsongNameAt(int i) const;
  void selectSubsong(int i);
  int addSubsong();  // a new empty subsong after the others (not selected)
  void removeSubsong(int i);
  Song currentSubsongOnly() const;  // a copy with just the current subsong
  // Keep the automation in place when orders are inserted or removed.
  void insertOrderTime(int at);
  void removeOrderTime(int at);
  void swapOrderTime(int a, int b);

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
  // The same .dtk text in memory (the keygen player embeds it).
  std::string toText() const;
  bool fromText(const std::string& text, std::string& err);
  void write(std::ostream& out) const;
  bool read(std::istream& in, const std::string& name, std::string& err);
};

std::string noteName(int note);  // "C-4", "OFF", "---"

// Sets effect cmd in a cell: reuses a column with that command, else the
// first free one (widening effectCols if needed). False if all are taken.
bool putCellEffect(Cell& cell, int& effectCols, int cmd, int val);
// Clears every column holding effect cmd.
void removeCellEffect(Cell& cell, int cmd);
// The value of effect cmd in the cell, or -1 (-2 if present without a value).
int cellEffect(const Cell& cell, int cmd);
// Pitch bend effects: E8xx (1/16 semitone, +-8) or E9xx (1/4 semitone, +-32).
void bendEffect(double semitones, int& cmd, int& val);
// The bend set in the cell (E8/E9), or NaN if none.
double cellBend(const Cell& cell);

// Reads a WAV file into a sample. Returns false and sets err on failure.
bool loadWavSample(const std::string& path, Sample& out, std::string& err);
