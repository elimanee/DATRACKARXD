// Instrument list and instrument editor (waveform, macros, wavetable).
#include <imgui.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <sstream>

#include "app.h"

namespace {

// Macro text: values separated by spaces, "|" before the loop start,
// "/" after the step that holds until note off.
std::string macroToText(const Macro& m) {
  std::string s;
  for (int i = 0; i < (int)m.values.size(); i++) {
    if (i == m.loop) s += "| ";
    s += std::to_string(m.values[i]) + " ";
    if (i == m.release) s += "/ ";
  }
  if (!s.empty()) s.pop_back();
  return s;
}

void textToMacro(const std::string& text, Macro& m, int lo, int hi) {
  std::istringstream ss(text);
  std::string tok;
  m.values.clear();
  m.loop = m.release = -1;
  while (ss >> tok && (int)m.values.size() < MAX_MACRO_LEN) {
    if (tok == "|") {
      m.loop = (int)m.values.size();
    } else if (tok == "/") {
      if (!m.values.empty()) m.release = (int)m.values.size() - 1;
    } else {
      try {
        m.values.push_back(std::clamp(std::stoi(tok), lo, hi));
      } catch (...) {
      }
    }
  }
  if (m.loop >= (int)m.values.size()) m.loop = -1;
}

// Draws bars in [lo, hi] and lets the mouse paint them. Returns true if edited.
bool barGraph(const char* id, std::vector<int>& values, int lo, int hi, float height, ImU32 color, int highlight = -1) {
  ImVec2 pos = ImGui::GetCursorScreenPos();
  float width = std::max(ImGui::GetContentRegionAvail().x, 100.0f);
  ImGui::InvisibleButton(id, ImVec2(width, height));
  ImDrawList* dl = ImGui::GetWindowDrawList();
  dl->AddRectFilled(pos, ImVec2(pos.x + width, pos.y + height), IM_COL32(20, 20, 30, 255));
  int n = (int)values.size();
  if (n == 0) {
    dl->AddText(ImVec2(pos.x + 8, pos.y + height / 2 - 7), IM_COL32(120, 120, 140, 255), "(empty - set a length or type values)");
    return false;
  }
  float bw = width / n;
  auto valToY = [&](float v) { return pos.y + height - (v - lo) / (float)(hi - lo) * height; };
  float zeroY = valToY((float)std::clamp(0, lo, hi));
  if (lo < 0) dl->AddLine(ImVec2(pos.x, zeroY), ImVec2(pos.x + width, zeroY), IM_COL32(80, 80, 100, 255));
  for (int i = 0; i < n; i++) {
    float y = valToY((float)values[i]);
    ImU32 c = i == highlight ? IM_COL32(255, 255, 255, 255) : color;
    dl->AddRectFilled(ImVec2(pos.x + i * bw + 1, std::min(y, zeroY)), ImVec2(pos.x + (i + 1) * bw - 1, std::max(y, zeroY) + (y == zeroY ? 1 : 0)), c);
  }
  if (ImGui::IsItemHovered()) {
    ImVec2 m = ImGui::GetIO().MousePos;
    int i = std::clamp((int)((m.x - pos.x) / bw), 0, n - 1);
    ImGui::SetTooltip("%d: %d", i, values[i]);
  }

  static int lastIdx = -1, lastVal = 0;
  static ImGuiID lastId = 0;
  bool edited = false;
  if (ImGui::IsItemActive()) {
    ImVec2 m = ImGui::GetIO().MousePos;
    int idx = std::clamp((int)((m.x - pos.x) / bw), 0, n - 1);
    int val = (int)std::lround(lo + (pos.y + height - m.y) / height * (hi - lo));
    val = std::clamp(val, lo, hi);
    if (lastId != ImGui::GetItemID() || !ImGui::IsMouseDragging(ImGuiMouseButton_Left, 0) || lastIdx < 0) {
      lastIdx = idx;
      lastVal = val;
    }
    // Interpolate so fast drags don't leave gaps.
    int a = std::min(lastIdx, idx), b = std::max(lastIdx, idx);
    for (int i = a; i <= b; i++) {
      float t = b == a ? 1.0f : (float)(i - lastIdx) / (float)(idx - lastIdx);
      int v = (int)std::lround(lastVal + (val - lastVal) * t);
      if (values[i] != v) {
        values[i] = v;
        edited = true;
      }
    }
    lastIdx = idx;
    lastVal = val;
    lastId = ImGui::GetItemID();
  } else if (lastId == ImGui::GetItemID()) {
    lastIdx = -1;
  }
  return edited;
}

}  // namespace

void App::instrumentsWindow() {
  if (!ImGui::Begin("Instruments", &showInstruments_)) {
    ImGui::End();
    return;
  }
  int n = (int)song_.instruments.size();
  if (ImGui::Button("Add") && n < MAX_INSTRUMENTS) {
    Instrument ins;
    ins.name = "Instrument " + std::to_string(n);
    song_.instruments.push_back(ins);
    curIns_ = n;
    dirty_ = true;
    metaTouched_ = true;
  }
  ImGui::SameLine();
  if (ImGui::Button("Duplicate") && n < MAX_INSTRUMENTS && curIns_ < n) {
    Instrument copy = song_.instruments[curIns_];
    copy.name += " copy";
    song_.instruments.push_back(copy);
    curIns_ = n;
    dirty_ = true;
    metaTouched_ = true;
  }
  ImGui::SameLine();
  if (ImGui::Button("Delete") && n > 1 && curIns_ < n) {
    pushFullUndo();  // instrument numbers in the patterns change too
    int del = curIns_;
    song_.instruments.erase(song_.instruments.begin() + del);
    // Fix instrument numbers in the patterns.
    for (auto& ch : song_.channels)
      for (auto& p : ch.patterns)
        for (auto& c : p.rows) {
          if (c.ins == del) c.ins = -1;
          else if (c.ins > del) c.ins--;
        }
    curIns_ = std::min(del, (int)song_.instruments.size() - 1);
    dirty_ = true;
  }
  ImGui::BeginChild("list");
  for (int i = 0; i < (int)song_.instruments.size(); i++) {
    char buf[128];
    const Instrument& in = song_.instruments[i];
    const char* kind = in.type == INS_FM ? "FM" : in.type == INS_SAMPLE ? "Sample" : WAVE_NAMES[std::clamp(in.wave, 0, WAVE_COUNT - 1)];
    std::snprintf(buf, sizeof(buf), "%02X  %-9s %s", i, kind, in.name.c_str());
    if (ImGui::Selectable(buf, i == curIns_, ImGuiSelectableFlags_AllowDoubleClick)) {
      curIns_ = i;
      if (ImGui::IsMouseDoubleClicked(0)) {
        showInsEditor_ = true;
        ImGui::SetWindowFocus("Instrument editor");
      }
    }
  }
  ImGui::EndChild();
  ImGui::End();
}

bool App::wavetableEditor(Instrument& ins) {
  bool changed = false;
  ImGui::TextUnformatted("Wavetable (32 steps, 0-15)");
  std::vector<int> v(ins.wavetable.begin(), ins.wavetable.end());
  if (barGraph("##wavetable", v, 0, 15, 110, IM_COL32(120, 200, 255, 255))) {
    std::copy(v.begin(), v.end(), ins.wavetable.begin());
    changed = true;
  }
  auto preset = [&](const char* label, auto fn) {
    if (ImGui::SmallButton(label)) {
      for (int i = 0; i < WAVETABLE_LEN; i++) ins.wavetable[i] = std::clamp((int)std::lround(fn(i / (double)WAVETABLE_LEN)), 0, 15);
      changed = true;
    }
    ImGui::SameLine();
  };
  preset("Sine", [](double t) { return 7.5 + 7.5 * std::sin(t * 6.283185307); });
  preset("Triangle", [](double t) { return t < 0.5 ? t * 30 : (1 - t) * 30; });
  preset("Saw", [](double t) { return t * 15.99 - 0.49; });
  preset("Square", [](double t) { return t < 0.5 ? 15.0 : 0.0; });
  preset("Organ", [](double t) {
    return 7.5 + 7.5 * (0.6 * std::sin(t * 6.2832) + 0.3 * std::sin(t * 12.566) + 0.25 * std::sin(t * 25.13));
  });
  if (ImGui::SmallButton("Random")) {
    for (int& x : ins.wavetable) x = std::rand() % 16;
    changed = true;
  }
  return changed;
}

bool App::macroEditor(Instrument& ins, int type) {
  Macro& m = ins.macros[type];
  bool changed = false;

  static char bufs[MACRO_COUNT][1024];
  static bool editing[MACRO_COUNT];
  if (!editing[type]) std::snprintf(bufs[type], sizeof(bufs[type]), "%s", macroToText(m).c_str());
  ImGui::SetNextItemWidth(-1);
  ImGui::InputTextWithHint("##seq", "values, e.g. 15 12 10 | 8 6 / 3 0", bufs[type], sizeof(bufs[type]));
  editing[type] = ImGui::IsItemActive();
  if (ImGui::IsItemDeactivatedAfterEdit()) {
    int lo, hi;
    ins.macroRange(type, lo, hi);
    textToMacro(bufs[type], m, lo, hi);
    changed = true;
  }
  if (ImGui::IsItemHovered())
    ImGui::SetTooltip("Type values separated by spaces.\n'|' marks the loop start, '/' after a value holds it until note off.");

  // Keeps the next widget on the same line when it fits.
  auto sameLineIfFits = [](float width) {
    ImGui::SameLine();
    if (ImGui::GetContentRegionAvail().x < width) ImGui::NewLine();
  };
  const float fieldW = 90, labelW = 70;
  int len = (int)m.values.size();
  ImGui::SetNextItemWidth(fieldW);
  if (ImGui::InputInt("Length", &len)) {
    len = std::clamp(len, 0, MAX_MACRO_LEN);
    int fill = m.values.empty() ? (type == MACRO_VOL ? 15 : type == MACRO_DUTY ? ins.duty : type == MACRO_WAVE ? ins.wave : 0) : m.values.back();
    m.values.resize(len, fill);
    if (m.loop >= len) m.loop = -1;
    if (m.release >= len) m.release = -1;
    changed = true;
  }
  sameLineIfFits(fieldW + labelW);
  ImGui::SetNextItemWidth(fieldW);
  if (ImGui::InputInt("Loop", &m.loop)) {
    m.loop = std::clamp(m.loop, -1, len - 1);
    changed = true;
  }
  sameLineIfFits(fieldW + labelW);
  ImGui::SetNextItemWidth(fieldW);
  if (ImGui::InputInt("Release", &m.release)) {
    m.release = std::clamp(m.release, -1, len - 1);
    changed = true;
  }
  sameLineIfFits(60);
  if (ImGui::Button("Clear")) {
    m = Macro();
    changed = true;
  }

  // Show where the macro is while the current channel plays it.
  int playing = -1;
  const ChannelState& st = engine_->channel(curCh_);
  if (st.ins == curIns_ && st.active && st.macros[type].hasValue) playing = std::max(0, st.macros[type].pos - 1);

  static const ImU32 colors[MACRO_COUNT] = {IM_COL32(120, 225, 140, 255), IM_COL32(255, 160, 80, 255), IM_COL32(200, 140, 255, 255),
                                            IM_COL32(120, 200, 255, 255), IM_COL32(255, 220, 120, 255)};
  int lo, hi;
  ins.macroRange(type, lo, hi);
  if (type == MACRO_WAVE) hi = std::max(WAVE_COUNT - 1, std::min(hi, (int)song_.wavetables.size() - 1));
  if (type == MACRO_ARP) {
    // Show the fixed-note range only when the macro uses it.
    bool fixed = false;
    for (int v : m.values) fixed |= v >= ARP_FIXED;
    if (!fixed) hi = 60;
  }
  ImGui::PushID(type);
  if (barGraph("##graph", m.values, lo, hi, 140, colors[type], playing)) changed = true;
  ImGui::PopID();

  // Loop / release markers under the graph.
  if (!m.values.empty()) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 pos = ImGui::GetCursorScreenPos();
    float width = std::max(ImGui::GetContentRegionAvail().x, 100.0f);
    float bw = width / m.values.size();
    if (m.loop >= 0)
      dl->AddRectFilled(ImVec2(pos.x + m.loop * bw, pos.y), ImVec2(pos.x + m.values.size() * bw, pos.y + 5), IM_COL32(80, 160, 255, 255));
    if (m.release >= 0)
      dl->AddRectFilled(ImVec2(pos.x + m.release * bw, pos.y + 6), ImVec2(pos.x + (m.release + 1) * bw, pos.y + 11), IM_COL32(255, 90, 90, 255));
    ImGui::Dummy(ImVec2(width, 12));
    ImGui::TextDisabled("blue = loop, red = release (hold until note off)");
  }

  switch (type) {
    case MACRO_VOL: ImGui::TextDisabled("Volume 0-%d per tick, multiplied with the channel volume.", hi); break;
    case MACRO_ARP: ImGui::TextDisabled("Semitone offset per tick. %d+n plays the fixed note n.", ARP_FIXED); break;
    case MACRO_DUTY: ImGui::TextDisabled("Pulse duty: 0=12.5%% 1=25%% 2=50%% 3=75%%. Noise: 1 = metallic, 3 = periodic."); break;
    case MACRO_WAVE:
      ImGui::TextDisabled("0 pulse, 1 triangle, 2 saw, 3 noise, 4 sine, 5 wavetable.");
      ImGui::TextDisabled("On a wavetable channel: index of a song wavetable.");
      break;
    case MACRO_PITCH: ImGui::TextDisabled("Pitch change per tick in 1/32 semitone (adds up)."); break;
  }
  return changed;
}

namespace {

const char* const ALG_DIAGRAMS[8] = {
    "1 > 2 > 3 > 4",     "(1 + 2) > 3 > 4",   "(1 + (2 > 3)) > 4", "((1 > 2) + 3) > 4",
    "(1 > 2) + (3 > 4)", "1 > (2 + 3 + 4)",   "(1 > 2) + 3 + 4",   "1 + 2 + 3 + 4",
};
const bool ALG_CARRIERS[8][4] = {
    {0, 0, 0, 1}, {0, 0, 0, 1}, {0, 0, 0, 1}, {0, 0, 0, 1}, {0, 1, 0, 1}, {0, 1, 1, 1}, {0, 1, 1, 1}, {1, 1, 1, 1},
};

struct FMPreset {
  const char* name;
  int alg, fb;
  int op[4][9];  // mult dt tl ar dr sl d2r rr ksr
};
const FMPreset FM_PRESETS[] = {
    {"E.Piano", 4, 5, {{1, 3, 35, 31, 10, 3, 2, 6, 1}, {1, 3, 0, 31, 8, 5, 3, 7, 1}, {14, 3, 45, 31, 12, 4, 4, 6, 1}, {1, 3, 0, 31, 6, 6, 2, 7, 1}}},
    {"Slap bass", 0, 6, {{0, 3, 30, 31, 14, 6, 0, 8, 0}, {1, 3, 28, 31, 12, 7, 0, 8, 0}, {1, 3, 24, 31, 8, 5, 0, 8, 0}, {1, 3, 0, 31, 6, 2, 2, 9, 0}}},
    {"Brass", 2, 4, {{1, 3, 28, 18, 4, 2, 0, 7, 0}, {1, 3, 38, 20, 4, 3, 0, 7, 0}, {1, 4, 30, 18, 4, 2, 0, 7, 0}, {1, 3, 0, 20, 3, 1, 0, 8, 0}}},
    {"Lead", 4, 6, {{2, 3, 26, 31, 4, 2, 0, 8, 0}, {1, 3, 0, 31, 2, 1, 0, 8, 0}, {1, 4, 22, 31, 5, 2, 0, 8, 0}, {1, 2, 4, 31, 2, 1, 0, 8, 0}}},
    {"Bell", 4, 0, {{7, 3, 30, 31, 8, 4, 4, 5, 2}, {1, 3, 0, 31, 6, 6, 3, 5, 2}, {3, 2, 32, 31, 8, 4, 4, 5, 2}, {1, 4, 6, 31, 6, 6, 3, 5, 2}}},
};

}  // namespace

bool App::fmEditor(Instrument& ins) {
  bool changed = false;
  FMParams& fm = ins.fm;
  float w = std::min(ImGui::GetContentRegionAvail().x * 0.35f, 160.0f);
  ImGui::SetNextItemWidth(w);
  changed |= ImGui::SliderInt("Algorithm", &fm.alg, 0, 7);
  ImGui::SameLine();
  ImGui::TextColored(ImVec4(0.6f, 0.9f, 1, 1), "%s", ALG_DIAGRAMS[std::clamp(fm.alg, 0, 7)]);
  ImGui::SetNextItemWidth(w);
  changed |= ImGui::SliderInt("Feedback", &fm.fb, 0, 7);
  ImGui::SameLine();
  ImGui::TextDisabled("Presets:");
  for (const FMPreset& p : FM_PRESETS) {
    ImGui::SameLine();
    if (ImGui::SmallButton(p.name)) {
      fm.alg = p.alg;
      fm.fb = p.fb;
      for (int o = 0; o < 4; o++) {
        FMOperator& op = fm.ops[o];
        const int* v = p.op[o];
        op = FMOperator();
        op.mult = v[0], op.dt = v[1], op.tl = v[2], op.ar = v[3], op.dr = v[4], op.sl = v[5], op.d2r = v[6], op.rr = v[7], op.ksr = v[8];
      }
      changed = true;
    }
  }

  struct Param {
    const char* name;
    int FMOperator::*field;
    int max;
    const char* help;
  };
  static const Param params[] = {
      {"MULT", &FMOperator::mult, 15, "Frequency multiplier (0 = x0.5)"},
      {"DT", &FMOperator::dt, 7, "Detune (3 = none)"},
      {"TL", &FMOperator::tl, 127, "Total level: attenuation, higher = quieter"},
      {"AR", &FMOperator::ar, 31, "Attack rate"},
      {"DR", &FMOperator::dr, 31, "Decay rate"},
      {"SL", &FMOperator::sl, 15, "Sustain level (attenuation after decay)"},
      {"D2R", &FMOperator::d2r, 31, "Sustain rate (second decay)"},
      {"RR", &FMOperator::rr, 15, "Release rate"},
      {"KSR", &FMOperator::ksr, 3, "Key scaling: faster envelopes for high notes"},
  };
  if (ImGui::BeginTable("ops", 5, ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_SizingStretchSame)) {
    ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed, 40);
    for (int o = 0; o < 4; o++) ImGui::TableSetupColumn("");
    ImGui::TableNextRow();
    ImGui::TableNextColumn();
    for (int o = 0; o < 4; o++) {
      ImGui::TableNextColumn();
      ImGui::PushID(o);
      bool carrier = ALG_CARRIERS[std::clamp(fm.alg, 0, 7)][o];
      changed |= ImGui::Checkbox("##en", &fm.ops[o].enabled);
      ImGui::SameLine();
      ImGui::TextColored(carrier ? ImVec4(1, 0.8f, 0.3f, 1) : ImVec4(0.6f, 0.8f, 1, 1), "OP%d %s", o + 1, carrier ? "out" : "mod");
      ImGui::PopID();
    }
    for (const Param& p : params) {
      ImGui::TableNextRow();
      ImGui::TableNextColumn();
      ImGui::TextUnformatted(p.name);
      if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", p.help);
      for (int o = 0; o < 4; o++) {
        ImGui::TableNextColumn();
        ImGui::PushID(o * 100 + (int)(&p - params));
        ImGui::SetNextItemWidth(-1);
        changed |= ImGui::SliderInt("##v", &(fm.ops[o].*p.field), 0, p.max);
        ImGui::PopID();
      }
    }
    ImGui::EndTable();
  }
  ImGui::TextDisabled("Orange = carriers (heard), blue = modulators. Lower TL on a modulator = brighter.");
  return changed;
}

bool App::sampleInsEditor(Instrument& ins) {
  bool changed = false;
  char label[160];
  auto smpLabel = [&](int i) {
    if (i < 0 || i >= (int)song_.samples.size())
      std::snprintf(label, sizeof(label), "(none)");
    else
      std::snprintf(label, sizeof(label), "%02X: %s", i, song_.samples[i].name.c_str());
    return label;
  };
  ImGui::SetNextItemWidth(std::min(ImGui::GetContentRegionAvail().x - 90.0f, 300.0f));
  if (ImGui::BeginCombo("Sample", smpLabel(ins.sample))) {
    if (ImGui::Selectable("(none)", ins.sample < 0)) {
      ins.sample = -1;
      changed = true;
    }
    for (int i = 0; i < (int)song_.samples.size(); i++)
      if (ImGui::Selectable(smpLabel(i), i == ins.sample)) {
        ins.sample = i;
        curSample_ = i;
        changed = true;
      }
    ImGui::EndCombo();
  }
  if (ImGui::Button("Load WAV...")) {
    loadSampleIntoIns_ = curIns_;
    openFileDialog(FileDialogMode::LoadSample);
  }
  ImGui::SameLine();
  if (ImGui::Button("Edit sample")) {
    showSamples_ = true;
    if (ins.sample >= 0) curSample_ = ins.sample;
    ImGui::SetWindowFocus("Samples");
  }
  float fade = ins.fadeout * 1000;
  ImGui::SetNextItemWidth(160);
  if (ImGui::SliderFloat("Fadeout", &fade, 0, 100, "%.1f")) {
    ins.fadeout = fade / 1000;
    changed = true;
  }
  if (ImGui::IsItemHovered()) ImGui::SetTooltip("Volume lost per tick after note off (0 = cut at note off unless the volume macro has a release)");
  if (!ins.sampleMap.empty()) {
    int used = 0;
    for (int v : ins.sampleMap) used += v >= 0;
    ImGui::TextDisabled("Multi-sample map: %d notes mapped", used);
    ImGui::SameLine();
    if (ImGui::SmallButton("Remove map")) {
      ins.sampleMap.clear();
      changed = true;
    }
  }
  ImGui::TextDisabled("The sample plays at its own rate on C-5.");
  return changed;
}

void App::instrumentEditor() {
  if (!ImGui::Begin("Instrument editor", &showInsEditor_)) {
    ImGui::End();
    return;
  }
  if (song_.instruments.empty()) {
    ImGui::TextDisabled("No instrument");
    ImGui::End();
    return;
  }
  curIns_ = std::clamp(curIns_, 0, (int)song_.instruments.size() - 1);
  Instrument& ins = song_.instruments[curIns_];
  bool changed = false;

  ImGui::Text("Instrument %02X", curIns_);
  ImGui::SameLine();
  char name[128];
  std::snprintf(name, sizeof(name), "%s", ins.name.c_str());
  ImGui::SetNextItemWidth(-1);
  if (ImGui::InputText("##name", name, sizeof(name))) {
    ins.name = name;
    changed = true;
  }

  float w = std::min(ImGui::GetContentRegionAvail().x - 90.0f, 260.0f);
  ImGui::SetNextItemWidth(w);
  changed |= ImGui::Combo("Type", &ins.type, INS_TYPE_NAMES, INS_TYPE_COUNT);

  if (ins.type == INS_STANDARD) {
    ImGui::SetNextItemWidth(w);
    changed |= ImGui::Combo("Waveform", &ins.wave, WAVE_NAMES, WAVE_COUNT);
    static const char* dutyNames[4] = {"12.5%", "25%", "50%", "75%"};
    ImGui::SetNextItemWidth(w);
    changed |= ImGui::SliderInt("Duty", &ins.duty, 0, 3, dutyNames[std::clamp(ins.duty, 0, 3)]);
    ImGui::SetNextItemWidth(w);
    changed |= ImGui::SliderInt("Volume", &ins.volume, 0, 15);
    bool usesTable = ins.wave == WAVE_TABLE;
    for (int v : ins.macros[MACRO_WAVE].values) usesTable |= v == WAVE_TABLE;
    if (usesTable) {
      if (ins.songWave >= 0) {
        ImGui::TextDisabled("Uses song wavetable %d.", ins.songWave);
        ImGui::SameLine();
        if (ImGui::SmallButton("Use own table")) {
          ins.songWave = -1;
          changed = true;
        }
      } else {
        changed |= wavetableEditor(ins);
      }
    }
  } else if (ins.type == INS_FM) {
    changed |= fmEditor(ins);
  } else {
    changed |= sampleInsEditor(ins);
  }
  ImGui::TextDisabled("Play it with the keyboard: Z S X D C... / Q 2 W 3 E...");

  ImGui::Separator();
  if (ImGui::BeginTabBar("macros")) {
    for (int t = 0; t < MACRO_COUNT; t++) {
      if (ins.type != INS_STANDARD && (t == MACRO_DUTY || t == MACRO_WAVE) && ins.macros[t].values.empty()) continue;
      std::string label = MACRO_NAMES[t];
      if (!ins.macros[t].values.empty()) label += " *";
      label += "###mac" + std::to_string(t);
      if (ImGui::BeginTabItem(label.c_str())) {
        changed |= macroEditor(ins, t);
        ImGui::EndTabItem();
      }
    }
    ImGui::EndTabBar();
  }
  if (changed) {
    dirty_ = true;
    metaTouched_ = true;
  }
  ImGui::End();
}

// ---------------------------------------------------------------------------
// Samples window
// ---------------------------------------------------------------------------

void App::samplesWindow() {
  if (!ImGui::Begin("Samples", &showSamples_)) {
    ImGui::End();
    return;
  }
  bool changed = false;
  if (ImGui::Button("Load WAV...")) {
    loadSampleIntoIns_ = -1;
    openFileDialog(FileDialogMode::LoadSample);
  }
  ImGui::SameLine();
  int n = (int)song_.samples.size();
  if (ImGui::Button("Delete") && curSample_ >= 0 && curSample_ < n) {
    pushFullUndo();
    int del = curSample_;
    song_.samples.erase(song_.samples.begin() + del);
    for (Instrument& ins : song_.instruments) {
      if (ins.sample == del) ins.sample = -1;
      else if (ins.sample > del) ins.sample--;
      for (auto& m : ins.sampleMap) {
        if (m == del) m = -1;
        else if (m > del) m--;
      }
    }
    engine_->reset();
    curSample_ = std::min(del, (int)song_.samples.size() - 1);
    changed = true;
  }
  ImGui::SameLine();
  if (ImGui::Button("Make instrument") && curSample_ >= 0 && curSample_ < n && (int)song_.instruments.size() < MAX_INSTRUMENTS) {
    Instrument ins;
    ins.type = INS_SAMPLE;
    ins.sample = curSample_;
    ins.name = song_.samples[curSample_].name;
    song_.instruments.push_back(ins);
    curIns_ = (int)song_.instruments.size() - 1;
    changed = true;
  }

  ImGui::BeginChild("smplist", ImVec2(0, 120), ImGuiChildFlags_Borders | ImGuiChildFlags_ResizeY);
  for (int i = 0; i < (int)song_.samples.size(); i++) {
    char buf[160];
    std::snprintf(buf, sizeof(buf), "%02X  %s  (%zu)", i, song_.samples[i].name.c_str(), song_.samples[i].data.size());
    if (ImGui::Selectable(buf, i == curSample_)) curSample_ = i;
  }
  if (song_.samples.empty()) ImGui::TextDisabled("No samples. Load a WAV file, or import a MOD/XM/IT/S3M.");
  ImGui::EndChild();

  if (curSample_ >= 0 && curSample_ < (int)song_.samples.size()) {
    Sample& smp = song_.samples[curSample_];
    char name[128];
    std::snprintf(name, sizeof(name), "%s", smp.name.c_str());
    if (ImGui::InputText("Name", name, sizeof(name))) {
      smp.name = name;
      changed = true;
    }
    int len = (int)smp.data.size();
    ImGui::SetNextItemWidth(120);
    changed |= ImGui::InputInt("Rate at C-5 (Hz)", &smp.rate, 100, 1000);
    smp.rate = std::clamp(smp.rate, 1, 1000000);
    ImGui::SetNextItemWidth(160);
    changed |= ImGui::SliderInt("Volume", &smp.volume, 0, 64);
    ImGui::SetNextItemWidth(160);
    changed |= ImGui::Combo("Loop", &smp.loopMode, LOOP_NAMES, 4);
    if (smp.loopMode) {
      ImGui::SetNextItemWidth(160);
      changed |= ImGui::SliderInt("Loop start", &smp.loopStart, 0, std::max(0, len - 1));
      ImGui::SetNextItemWidth(160);
      changed |= ImGui::SliderInt("Loop end", &smp.loopEnd, 0, len);
      smp.loopStart = std::clamp(smp.loopStart, 0, len);
      smp.loopEnd = std::clamp(smp.loopEnd, smp.loopStart, len);
    }
    ImGui::TextDisabled("%d frames, %.2f s", len, len / (float)std::max(smp.rate, 1));

    // Waveform view with the loop region.
    ImVec2 pos = ImGui::GetCursorScreenPos();
    ImVec2 size(std::max(ImGui::GetContentRegionAvail().x, 100.0f), std::max(ImGui::GetContentRegionAvail().y, 60.0f));
    ImGui::Dummy(size);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(pos, ImVec2(pos.x + size.x, pos.y + size.y), IM_COL32(15, 15, 22, 255));
    float mid = pos.y + size.y / 2;
    if (len > 0) {
      if (smp.loopMode && smp.loopEnd > smp.loopStart)
        dl->AddRectFilled(ImVec2(pos.x + smp.loopStart * size.x / len, pos.y), ImVec2(pos.x + smp.loopEnd * size.x / len, pos.y + size.y),
                          IM_COL32(60, 90, 160, 70));
      int cols = (int)size.x;
      for (int x = 0; x < cols; x++) {
        size_t a = (size_t)x * len / cols, b = std::max(a + 1, (size_t)(x + 1) * len / cols);
        float lo = 0, hi = 0;
        for (size_t k = a; k < b && k < (size_t)len; k++) {
          lo = std::min(lo, smp.data[k]);
          hi = std::max(hi, smp.data[k]);
        }
        dl->AddLine(ImVec2(pos.x + x, mid - hi * size.y / 2), ImVec2(pos.x + x, mid - lo * size.y / 2 + 1), IM_COL32(110, 230, 140, 255));
      }
    }
  }
  if (changed) {
    dirty_ = true;
    metaTouched_ = true;
  }
  ImGui::End();
}
