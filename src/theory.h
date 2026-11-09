// Music theory helpers: scales, diatonic chords, progressions, and the
// pattern generators (melody, euclidean rhythm, chords, arpeggios).
#pragma once
#include <cstdint>
#include <string>
#include <vector>

struct ScaleInfo {
  const char* name;
  int count;      // notes per octave
  int steps[12];  // semitones above the root
};
constexpr int SCALE_COUNT = 14;
extern const ScaleInfo SCALES[SCALE_COUNT];  // 0 = chromatic (no scale)
extern const char* const KEY_NAMES[12];      // "C", "C#", ...

bool inScale(int note, int root, int scale);
// The nearest note of the scale (ties go up).
int snapToScale(int note, int root, int scale);
// Degree 0..count-1 of a note in the scale, or -1.
int scaleDegree(int note, int root, int scale);
// The scale note `steps` degrees above (or below) a note of the scale.
int scaleStep(int note, int steps, int root, int scale);

enum ChordType : int { CHORD_OFF = 0, CHORD_TRIAD, CHORD_SEVENTH, CHORD_SUS2, CHORD_SUS4, CHORD_POWER, CHORD_COUNT };
extern const char* const CHORD_NAMES[CHORD_COUNT];
// The chord built on a note: thirds of the scale stacked on it (a major or
// minor triad from the chromatic scale), sus and power chords by interval.
std::vector<int> chordOn(int note, ChordType type, int root, int scale);
// "Am", "G7", "Dsus4"... from the chord's notes (bass first).
std::string chordLabel(const std::vector<int>& notes);

// Roman numeral progression ("I V vi IV", "ii7 V7 I") -> chord roots as
// scale degrees (0-based) and whether a 7th was asked. False on a bad token.
struct ProgChord {
  int degree;
  bool seventh;
};
bool parseProgression(const std::string& text, std::vector<ProgChord>& out);

// Generators. They return notes as (row, note, length) in a pattern of
// `rows` rows; the caller writes them with its instrument.
struct GenNote {
  int row, note, len, vol;  // vol -1 = none
};
// Euclidean rhythm: `hits` spread as evenly as possible over `steps`.
std::vector<bool> euclid(int hits, int steps, int rotate);

struct MelodyParams {
  int root = 0, scale = 1;
  int low = 60, high = 72;     // note range
  int stepRows = 2;            // grid
  float density = 0.7f;        // chance of a note on a grid step
  int maxLeap = 3;             // scale steps between notes
  int contour = 0;             // 0 free, 1 rising, 2 falling, 3 arch
  int noteLen = 0;             // rows, 0 = until the next note
  uint32_t seed = 1;
};
std::vector<GenNote> generateMelody(const MelodyParams& p, int rows);

// Chords of a progression, `rowsPerChord` each, one vector per voice (bass
// first), each voice a list of notes.
std::vector<std::vector<GenNote>> generateChords(const std::vector<ProgChord>& prog, int root, int scale, int octaveNote,
                                                 bool sevenths, int voices, int rowsPerChord, int rows);

// Arpeggio over held notes: every note becomes its chord's notes in turn.
// pattern: 0 up, 1 down, 2 up-down, 3 random. octaves 1-3.
std::vector<GenNote> arpeggiate(const std::vector<GenNote>& held, ChordType type, int root, int scale, int pattern,
                                int stepRows, int octaves, uint32_t seed, int rows);
