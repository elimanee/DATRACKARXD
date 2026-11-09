#include "theory.h"

#include <algorithm>
#include <cctype>
#include <random>
#include <sstream>

const ScaleInfo SCALES[SCALE_COUNT] = {
    {"Chromatic (no scale)", 12, {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11}},
    {"Major", 7, {0, 2, 4, 5, 7, 9, 11}},
    {"Minor", 7, {0, 2, 3, 5, 7, 8, 10}},
    {"Harmonic minor", 7, {0, 2, 3, 5, 7, 8, 11}},
    {"Melodic minor", 7, {0, 2, 3, 5, 7, 9, 11}},
    {"Dorian", 7, {0, 2, 3, 5, 7, 9, 10}},
    {"Phrygian", 7, {0, 1, 3, 5, 7, 8, 10}},
    {"Lydian", 7, {0, 2, 4, 6, 7, 9, 11}},
    {"Mixolydian", 7, {0, 2, 4, 5, 7, 9, 10}},
    {"Locrian", 7, {0, 1, 3, 5, 6, 8, 10}},
    {"Major pentatonic", 5, {0, 2, 4, 7, 9}},
    {"Minor pentatonic", 5, {0, 3, 5, 7, 10}},
    {"Blues", 6, {0, 3, 5, 6, 7, 10}},
    {"Whole tone", 6, {0, 2, 4, 6, 8, 10}},
};

const char* const KEY_NAMES[12] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
const char* const CHORD_NAMES[CHORD_COUNT] = {"Off", "Triad", "7th", "Sus2", "Sus4", "Power (5th)"};

namespace {

const ScaleInfo& scaleOf(int scale) { return SCALES[std::clamp(scale, 0, SCALE_COUNT - 1)]; }
int mod12(int n) { return ((n % 12) + 12) % 12; }

}  // namespace

bool inScale(int note, int root, int scale) { return scaleDegree(note, root, scale) >= 0; }

int scaleDegree(int note, int root, int scale) {
  const ScaleInfo& s = scaleOf(scale);
  int pc = mod12(note - root);
  for (int i = 0; i < s.count; i++)
    if (s.steps[i] == pc) return i;
  return -1;
}

int snapToScale(int note, int root, int scale) {
  for (int d = 0; d < 12; d++) {
    if (inScale(note + d, root, scale)) return note + d;
    if (inScale(note - d, root, scale)) return note - d;
  }
  return note;
}

int scaleStep(int note, int steps, int root, int scale) {
  const ScaleInfo& s = scaleOf(scale);
  auto floorDiv = [](int a, int b) { return a >= 0 ? a / b : -((-a + b - 1) / b); };
  int n = snapToScale(note, root, scale);
  int deg = scaleDegree(n, root, scale);
  int octave = floorDiv(n - root - s.steps[deg], 12);  // a multiple of 12 apart
  int total = deg + steps;
  int oct = floorDiv(total, s.count);
  return root + (octave + oct) * 12 + s.steps[total - oct * s.count];
}

std::vector<int> chordOn(int note, ChordType type, int root, int scale) {
  switch (type) {
    case CHORD_OFF: return {note};
    case CHORD_POWER: return {note, note + 7, note + 12};
    case CHORD_SUS2: return {note, note + 2, note + 7};
    case CHORD_SUS4: return {note, note + 5, note + 7};
    default: break;
  }
  int count = type == CHORD_SEVENTH ? 4 : 3;
  std::vector<int> out;
  if (scale == 0 || !inScale(note, root, scale)) {
    // No scale to follow: a major chord (dominant 7th).
    static const int MAJ[4] = {0, 4, 7, 10};
    for (int i = 0; i < count; i++) out.push_back(note + MAJ[i]);
    return out;
  }
  for (int i = 0; i < count; i++) out.push_back(scaleStep(note, i * 2, root, scale));
  return out;
}

std::string chordLabel(const std::vector<int>& notes) {
  if (notes.empty()) return "";
  int bass = notes[0];
  std::string name = KEY_NAMES[mod12(bass)];
  std::vector<int> iv;
  for (int n : notes) iv.push_back(mod12(n - bass));
  auto has = [&](int i) { return std::find(iv.begin(), iv.end(), i) != iv.end(); };
  if (notes.size() == 1) return name;
  if (has(7) && !has(3) && !has(4) && !has(2) && !has(5)) return name + "5";
  if (has(2) && has(7) && !has(3) && !has(4)) return name + "sus2";
  if (has(5) && has(7) && !has(3) && !has(4)) return name + "sus4";
  bool minor = has(3) && !has(4);
  bool dim = minor && has(6) && !has(7);
  bool aug = has(4) && has(8) && !has(7);
  std::string q = dim ? "dim" : aug ? "aug" : minor ? "m" : "";
  if (has(10)) return name + (dim ? "m7b5" : q + "7");
  if (has(11)) return name + (minor ? "mMaj7" : "maj7");
  return name + q;
}

bool parseProgression(const std::string& text, std::vector<ProgChord>& out) {
  out.clear();
  std::istringstream ss(text);
  std::string tok;
  static const char* const NUM[7] = {"i", "ii", "iii", "iv", "v", "vi", "vii"};
  while (ss >> tok) {
    std::string t;
    bool seventh = false;
    for (char c : tok) {
      if (c == '7') seventh = true;
      else if (std::isalpha((unsigned char)c)) t += (char)std::tolower((unsigned char)c);
      // Quality marks (°, +, -) are ignored: the scale decides.
    }
    int degree = -1;
    for (int i = 0; i < 7; i++)
      if (t == NUM[i]) degree = i;
    if (degree < 0) return false;
    out.push_back({degree, seventh});
  }
  return !out.empty();
}

std::vector<bool> euclid(int hits, int steps, int rotate) {
  steps = std::clamp(steps, 1, 256);
  hits = std::clamp(hits, 0, steps);
  std::vector<bool> out(steps, false);
  // Bresenham: a hit wherever floor(i * hits / steps) moves on.
  for (int i = 0; i < steps; i++) out[i] = ((i * hits) % steps) < hits;
  std::vector<bool> rot(steps);
  for (int i = 0; i < steps; i++) rot[((i + rotate) % steps + steps) % steps] = out[i];
  return rot;
}

std::vector<GenNote> generateMelody(const MelodyParams& p, int rows) {
  std::mt19937 rng(p.seed);
  std::uniform_real_distribution<float> u(0, 1);
  int scale = p.scale == 0 ? 1 : p.scale;  // a melody needs a scale: major by default
  int low = std::min(p.low, p.high), high = std::max(p.low, p.high);
  int step = std::max(1, p.stepRows);
  // Start near the middle, on a scale note.
  int cur = snapToScale((low + high) / 2, p.root, scale);
  std::vector<GenNote> out;
  int steps = (rows + step - 1) / step;
  for (int i = 0; i < steps; i++) {
    int row = i * step;
    if (i > 0 && u(rng) >= p.density) continue;
    float t = steps > 1 ? (float)i / (steps - 1) : 0;
    // The contour leans the random walk up or down.
    float bias = p.contour == 1 ? 0.7f : p.contour == 2 ? -0.7f : p.contour == 3 ? (t < 0.5f ? 0.7f : -0.7f) : 0.0f;
    int leap = std::max(1, p.maxLeap);
    int move = (int)std::lround((u(rng) * 2 - 1 + bias) * leap);
    if (i == 0) move = 0;
    int next = scaleStep(cur, move, p.root, scale);
    if (next > high) next = scaleStep(cur, -std::abs(move) - 1, p.root, scale);
    if (next < low) next = scaleStep(cur, std::abs(move) + 1, p.root, scale);
    cur = std::clamp(next, low, high);
    cur = snapToScale(cur, p.root, scale);
    out.push_back({row, cur, 0, -1});
  }
  // Lengths: fixed, or up to the next note.
  for (size_t i = 0; i < out.size(); i++) {
    int until = i + 1 < out.size() ? out[i + 1].row : rows;
    out[i].len = p.noteLen > 0 ? std::min(p.noteLen, until - out[i].row) : until - out[i].row;
  }
  return out;
}

std::vector<std::vector<GenNote>> generateChords(const std::vector<ProgChord>& prog, int root, int scale, int octaveNote,
                                                 bool sevenths, int voices, int rowsPerChord, int rows) {
  int sc = scale == 0 ? 1 : scale;
  voices = std::clamp(voices, 1, 8);
  std::vector<std::vector<GenNote>> out(voices);
  if (prog.empty()) return out;
  rowsPerChord = std::max(1, rowsPerChord);
  // The tonic in the chosen octave.
  int tonic = octaveNote - (((octaveNote - root) % 12) + 12) % 12;
  std::vector<int> prev;
  for (int row = 0, k = 0; row < rows; row += rowsPerChord, k++) {
    const ProgChord& pc = prog[k % prog.size()];
    int base = scaleStep(tonic, pc.degree, root, sc);
    std::vector<int> chord = chordOn(base, (sevenths || pc.seventh) ? CHORD_SEVENTH : CHORD_TRIAD, root, sc);
    // Voice leading: each upper note moves to the closest octave of itself.
    std::vector<int> notes;
    for (int v = 0; v < voices; v++) {
      int n = chord[v % chord.size()] + 12 * (v / (int)chord.size());
      if (v > 0 && !prev.empty() && v < (int)prev.size()) {
        while (n - prev[v] > 6) n -= 12;
        while (prev[v] - n > 6) n += 12;
      }
      notes.push_back(n);
    }
    std::sort(notes.begin() + 1, notes.end());
    prev = notes;
    int len = std::min(rowsPerChord, rows - row);
    for (int v = 0; v < voices; v++) out[v].push_back({row, notes[v], len, -1});
  }
  return out;
}

std::vector<GenNote> arpeggiate(const std::vector<GenNote>& held, ChordType type, int root, int scale, int pattern,
                                int stepRows, int octaves, uint32_t seed, int rows) {
  std::mt19937 rng(seed);
  std::vector<GenNote> out;
  stepRows = std::max(1, stepRows);
  octaves = std::clamp(octaves, 1, 3);
  for (const GenNote& h : held) {
    std::vector<int> base = chordOn(h.note, type == CHORD_OFF ? CHORD_TRIAD : type, root, scale);
    std::vector<int> tones;
    for (int o = 0; o < octaves; o++)
      for (int n : base) tones.push_back(n + 12 * o);
    std::vector<int> seq = tones;
    if (pattern == 1) std::reverse(seq.begin(), seq.end());
    if (pattern == 2 && tones.size() > 2)
      for (int i = (int)tones.size() - 2; i > 0; i--) seq.push_back(tones[i]);
    int end = std::min(rows, h.row + std::max(h.len, 1));
    int k = 0;
    for (int row = h.row; row < end; row += stepRows, k++) {
      int n = pattern == 3 ? tones[rng() % tones.size()] : seq[k % seq.size()];
      out.push_back({row, n, std::min(stepRows, end - row), h.vol});
    }
  }
  return out;
}
