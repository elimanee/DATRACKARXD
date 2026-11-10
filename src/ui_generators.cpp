// Generators window: melodies, euclidean rhythms, chord progressions and
// arpeggios, written into the pattern at the cursor (one undo step).
#include <imgui.h>

#include <algorithm>
#include <cstdio>
#include <random>

#include "app.h"
#include "ui_widgets.h"
#include "icons.h"
#include "i18n.h"
#include "theme.h"
#include "theory.h"

namespace {

const char* const PROGRESSIONS[] = {"I V vi IV", "vi IV I V", "I vi IV V", "ii V I", "i VI III VII", "i iv v i",
                                    "I IV V IV", "i VII VI VII", "ii7 V7 I7"};

// A note slider showing the note's name.
bool noteSlider(const char* label, int* note, int lo, int hi) {
  std::string name = noteName(*note);
  return ImGui::SliderInt(label, note, lo, hi, name.c_str());
}

}  // namespace

// The rows the generators write into: the selection's rows on its first
// channel, or the cursor channel's whole pattern.
void App::generatorRange(int& ch, int& row0, int& row1) const {
  if (hasSel_) {
    int ch0, col0, r0, ch1, col1, r1;
    selectionBounds(ch0, col0, r0, ch1, col1, r1);
    ch = ch0;
    row0 = r0;
    row1 = r1 + 1;
  } else {
    ch = curCh_;
    row0 = 0;
    row1 = song_.patternLength;
  }
  row0 = std::clamp(row0, 0, song_.patternLength);
  row1 = std::clamp(row1, row0, song_.patternLength);
}

// Writes generated notes (rows relative to row0) into one channel, with a
// note off after each note that ends before the next one.
void App::writeGenerated(int ch, int row0, int row1, const std::vector<GenNote>& notes, bool replace) {
  if (ch < 0 || ch >= song_.channelCount()) return;
  if (replace)
    for (int r = row0; r < row1; r++)
      if (Cell* c = song_.cell(ch, curOrder_, r, false)) {
        c->note = NOTE_EMPTY;
        c->ins = -1;
        c->vol = -1;
      }
  std::vector<GenNote> sorted = notes;
  std::sort(sorted.begin(), sorted.end(), [](const GenNote& a, const GenNote& b) { return a.row < b.row; });
  for (size_t i = 0; i < sorted.size(); i++) {
    const GenNote& n = sorted[i];
    int row = row0 + n.row;
    if (row < row0 || row >= row1 || n.note < 0 || n.note >= NOTE_COUNT) continue;
    Cell* c = song_.cell(ch, curOrder_, row, true);
    if (!c) continue;
    c->note = (int16_t)n.note;
    c->ins = (int16_t)curIns_;
    c->vol = (int16_t)n.vol;
    int end = row + std::max(n.len, 1);
    int next = i + 1 < sorted.size() ? row0 + sorted[i + 1].row : row1;
    if (n.len > 0 && end < next && end < row1) {
      Cell* off = song_.cell(ch, curOrder_, end, true);
      if (off && off->note == NOTE_EMPTY) off->note = NOTE_OFF;
    }
  }
  dirty_ = true;
}

void App::generatorsWindow() {
  ImGui::SetNextWindowSize(ImVec2(S(560), S(430)), ImGuiCond_FirstUseEver);
  if (!ImGui::Begin(withIcon(ICON_SPARK, TW("Generators")).c_str(), &showGenerators_)) {
    ImGui::End();
    return;
  }
  GeneratorSettings& g = gen_;
  int ch, row0, row1;
  generatorRange(ch, row0, row1);
  int rows = row1 - row0;
  std::string ins = song_.instruments.empty() ? "" : song_.instruments[std::clamp(curIns_, 0, (int)song_.instruments.size() - 1)].name;
  ImGui::TextWrapped(T("Writes into channel %d (%s), rows %02X-%02X of order %02X, with instrument %02X (%s)."), ch + 1,
                     song_.channels[ch].name.c_str(), row0, std::max(row0, row1 - 1), curOrder_, curIns_, ins.c_str());
  ImGui::TextDisabled(T("Key: %s %s (Song window). A selection limits the rows."), KEY_NAMES[song_.keyRoot],
                      T(SCALES[song_.keyScale].name));
  ImGui::Checkbox(T("Replace the notes already there"), &g.replace);
  ImGui::Separator();
  const float w = S(200);

  if (ImGui::BeginTabBar("gentabs")) {
    if (ImGui::BeginTabItem(T("Melody"))) {
      MelodyParams& m = g.melody;
      ImGui::SetNextItemWidth(w);
      noteSlider(T("Lowest note"), &m.low, 24, 108);
      ImGui::SetNextItemWidth(w);
      noteSlider(T("Highest note"), &m.high, 24, 108);
      ImGui::SetNextItemWidth(w);
      ImGui::SliderInt(T("Grid (rows)"), &m.stepRows, 1, 16);
      ImGui::SetNextItemWidth(w);
      ImGui::SliderFloat(T("Density"), &m.density, 0.05f, 1.0f, "%.2f");
      ImGui::SetNextItemWidth(w);
      ImGui::SliderInt(T("Largest leap"), &m.maxLeap, 1, 7);
      ImGui::SetNextItemWidth(w);
      ImGui::Combo(T("Contour"), &m.contour, TC("Free\0Rising\0Falling\0Arch\0"));
      ImGui::SetNextItemWidth(w);
      ImGui::SliderInt(T("Note length"), &m.noteLen, 0, 16, m.noteLen ? "%d" : T("legato"));
      int seed = (int)m.seed;
      ImGui::SetNextItemWidth(w);
      if (ImGui::InputInt(T("Seed"), &seed)) m.seed = (uint32_t)std::max(seed, 0);
      ImGui::SameLine();
      bool dice = ImGui::Button(T("New seed and generate"));
      if (dice) m.seed = (uint32_t)std::random_device{}() % 100000;
      if (ImGui::Button(T("Generate melody"), ImVec2(S(200), 0)) || dice) {
        m.root = song_.keyRoot;
        m.scale = song_.keyScale;
        pushUndo(ch, ch);
        writeGenerated(ch, row0, row1, generateMelody(m, rows), g.replace);
        setStatus(T("Melody written"));
      }
      ImGui::EndTabItem();
    }

    if (ImGui::BeginTabItem(T("Rhythm"))) {
      ImGui::SetNextItemWidth(w);
      ImGui::SliderInt(T("Steps"), &g.steps, 2, 32);
      g.hits = std::min(g.hits, g.steps);
      ImGui::SetNextItemWidth(w);
      ImGui::SliderInt(T("Hits"), &g.hits, 0, g.steps);
      ImGui::SetNextItemWidth(w);
      ImGui::SliderInt(T("Rotation"), &g.rotate, 0, g.steps - 1);
      ImGui::SetNextItemWidth(w);
      ImGui::SliderInt(T("Rows per step"), &g.stepRows, 1, 8);
      ImGui::SetNextItemWidth(w);
      noteSlider(T("Note"), &g.rhythmNote, 0, NOTE_COUNT - 1);
      ImGui::SetNextItemWidth(w);
      ImGui::SliderInt(T("Accent every"), &g.accent, 0, 16, g.accent ? T("%d hits") : T("off"));
      if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", T("Every n-th hit is full volume, the others softer."));
      // The pattern, as boxes.
      std::vector<bool> e = euclid(g.hits, g.steps, g.rotate);
      ImDrawList* dl = ImGui::GetWindowDrawList();
      ImVec2 p = ImGui::GetCursorScreenPos();
      float bw = std::min(S(18), (ImGui::GetContentRegionAvail().x - 4) / g.steps);
      for (int i = 0; i < g.steps; i++) {
        ImVec2 a(p.x + i * bw, p.y), b(p.x + (i + 1) * bw - 2, p.y + S(16));
        dl->AddRectFilled(a, b, e[i] ? IM_COL32(120, 220, 255, 255) : IM_COL32(50, 50, 70, 255), 2);
      }
      ImGui::Dummy(ImVec2(bw * g.steps, S(20)));
      if (ImGui::Button(T("Generate rhythm"), ImVec2(S(200), 0))) {
        std::vector<GenNote> out;
        int hitIdx = 0;
        for (int r = 0, i = 0; r < rows; r += g.stepRows, i++) {
          if (!e[i % g.steps]) continue;
          int vol = g.accent > 0 && hitIdx % g.accent != 0 ? 0x50 : -1;
          out.push_back({r, g.rhythmNote, 0, vol});
          hitIdx++;
        }
        pushUndo(ch, ch);
        writeGenerated(ch, row0, row1, out, g.replace);
        setStatus(T("Rhythm written"));
      }
      ImGui::EndTabItem();
    }

    if (ImGui::BeginTabItem(T("Chords"))) {
      ImGui::SetNextItemWidth(w);
      if (ImGui::BeginCombo(T("Presets"), "")) {
        for (const char* pr : PROGRESSIONS)
          if (ImGui::Selectable(pr)) std::snprintf(g.progression, sizeof(g.progression), "%s", pr);
        ImGui::EndCombo();
      }
      ImGui::SetNextItemWidth(w);
      ImGui::InputText(T("Progression"), g.progression, sizeof(g.progression));
      std::vector<ProgChord> prog;
      bool ok = parseProgression(g.progression, prog);
      if (!ok) ImGui::TextColored(ImVec4(1, 0.5f, 0.3f, 1), "%s", T("Use roman numerals: I ii iii IV V vi vii (7 for a 7th)."));
      ImGui::SetNextItemWidth(w);
      ImGui::SliderInt(T("Rows per chord"), &g.rowsPerChord, 1, 64);
      int maxVoices = std::min(8, song_.channelCount() - ch);
      g.voices = std::clamp(g.voices, 1, std::max(1, maxVoices));
      ImGui::SetNextItemWidth(w);
      ImGui::Combo(T("Style"), &g.chordStyle, TC("Block chords (one channel per voice)\0Arpeggio (one channel)\0"));
      if (g.chordStyle == 0) {
        ImGui::SetNextItemWidth(w);
        ImGui::SliderInt(T("Voices"), &g.voices, 1, std::max(1, maxVoices));
        ImGui::SameLine();
        ImGui::TextDisabled(T("channels %d-%d"), ch + 1, ch + g.voices);
      } else {
        ImGui::SetNextItemWidth(w);
        ImGui::Combo(T("Pattern"), &g.arpPattern, TC("Up\0Down\0Up-down\0Random\0"));
        ImGui::SetNextItemWidth(w);
        ImGui::SliderInt(T("Rows per note"), &g.arpStep, 1, 8);
        ImGui::SetNextItemWidth(w);
        ImGui::SliderInt(T("Octaves"), &g.arpOctaves, 1, 3);
      }
      ImGui::SetNextItemWidth(w);
      noteSlider(T("Octave of the tonic"), &g.chordOctave, 24, 84);
      ImGui::Checkbox(T("Sevenths"), &g.sevenths);
      if (ok) {
        // The chords this gives, by name.
        std::string names;
        int tonic = g.chordOctave;
        for (const ProgChord& pc : prog) {
          int sc = song_.keyScale ? song_.keyScale : 1;
          int base = scaleStep(tonic, pc.degree, song_.keyRoot, sc);
          names += chordLabel(chordOn(base, (g.sevenths || pc.seventh) ? CHORD_SEVENTH : CHORD_TRIAD, song_.keyRoot, sc)) + "  ";
        }
        ImGui::TextColored(ImVec4(0.6f, 0.9f, 1, 1), "%s", names.c_str());
      }
      ImGui::BeginDisabled(!ok);
      if (ImGui::Button(T("Generate chords"), ImVec2(S(200), 0))) {
        if (g.chordStyle == 0) {
          auto voices = generateChords(prog, song_.keyRoot, song_.keyScale, g.chordOctave, g.sevenths, g.voices, g.rowsPerChord, rows);
          pushUndo(ch, ch + g.voices - 1);
          for (int v = 0; v < g.voices; v++) writeGenerated(ch + v, row0, row1, voices[v], g.replace);
        } else {
          auto voices = generateChords(prog, song_.keyRoot, song_.keyScale, g.chordOctave, g.sevenths, 1, g.rowsPerChord, rows);
          std::vector<GenNote> arp = arpeggiate(voices[0], g.sevenths ? CHORD_SEVENTH : CHORD_TRIAD, song_.keyRoot,
                                                song_.keyScale ? song_.keyScale : 1, g.arpPattern, g.arpStep, g.arpOctaves,
                                                (uint32_t)g.melody.seed, rows);
          pushUndo(ch, ch);
          writeGenerated(ch, row0, row1, arp, g.replace);
        }
        setStatus(T("Chords written"));
      }
      ImGui::EndDisabled();
      ImGui::EndTabItem();
    }

    if (ImGui::BeginTabItem(T("Arpeggiator"))) {
      ImGui::TextWrapped("%s", T("Turns every note already in the rows into an arpeggio of its chord, until the next note."));
      ImGui::SetNextItemWidth(w);
      ImGui::Combo(T("Chord"), &g.arpChord, [](void*, int i) { return T(CHORD_NAMES[i]); }, nullptr, CHORD_COUNT);
      ImGui::SetNextItemWidth(w);
      ImGui::Combo(T("Pattern"), &g.arpPattern, TC("Up\0Down\0Up-down\0Random\0"));
      ImGui::SetNextItemWidth(w);
      ImGui::SliderInt(T("Rows per note"), &g.arpStep, 1, 8);
      ImGui::SetNextItemWidth(w);
      ImGui::SliderInt(T("Octaves"), &g.arpOctaves, 1, 3);
      if (ImGui::Button(T("Arpeggiate"), ImVec2(S(200), 0))) {
        // The held notes: each lasts until the next note or note off.
        std::vector<GenNote> held;
        for (int r = row0; r < row1; r++) {
          const Cell* c = song_.cell(ch, curOrder_, r);
          if (!c || c->note == NOTE_EMPTY) continue;
          if (!held.empty() && held.back().len == 0) held.back().len = r - row0 - held.back().row;
          if (c->note >= 0) held.push_back({r - row0, c->note, 0, c->vol});
        }
        if (!held.empty() && held.back().len == 0) held.back().len = rows - held.back().row;
        std::vector<GenNote> arp = arpeggiate(held, (ChordType)g.arpChord, song_.keyRoot, song_.keyScale, g.arpPattern, g.arpStep,
                                              g.arpOctaves, (uint32_t)g.melody.seed, rows);
        pushUndo(ch, ch);
        writeGenerated(ch, row0, row1, arp, true);
        setStatus(held.empty() ? T("No notes to arpeggiate in these rows") : T("Arpeggio written"));
      }
      ImGui::EndTabItem();
    }
    ImGui::EndTabBar();
  }
  ImGui::End();
}
