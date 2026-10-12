// Instrument list and instrument editor (waveform, macros, wavetable).
#include <imgui.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <sstream>

#include "app.h"
#include "i18n.h"
#include "theme.h"
#include "chips.h"
#include "sampleops.h"

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
  if (!ImGui::Begin(TW("Instruments").c_str(), &showInstruments_)) {
    ImGui::End();
    return;
  }
  int n = (int)song_.instruments.size();
  if (ImGui::Button(T("Add")) && n < MAX_INSTRUMENTS) {
    Instrument ins;
    ins.name = "Instrument " + std::to_string(n);
    song_.instruments.push_back(ins);
    curIns_ = n;
    dirty_ = true;
    metaTouched_ = true;
  }
  ImGui::SameLine();
  if (ImGui::Button(T("Duplicate")) && n < MAX_INSTRUMENTS && curIns_ < n) {
    Instrument copy = song_.instruments[curIns_];
    copy.name += " copy";
    song_.instruments.push_back(copy);
    curIns_ = n;
    dirty_ = true;
    metaTouched_ = true;
  }
  ImGui::SameLine();
  if (ImGui::Button(T("Delete")) && n > 1 && curIns_ < n) {
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
  if (ImGui::Button(T("Load..."))) openFileDialog(FileDialogMode::LoadInstrument);
  if (ImGui::IsItemHovered()) ImGui::SetTooltip(T("Add an instrument from a .dti preset, a Furnace .fui or a .wav"));
  ImGui::SameLine();
  if (ImGui::Button(T("Save...")) && curIns_ < n) openFileDialog(FileDialogMode::SaveInstrument);
  if (ImGui::IsItemHovered()) ImGui::SetTooltip(T("Save the selected instrument as a .dti preset"));
  ImGui::BeginChild("list");
  for (int i = 0; i < (int)song_.instruments.size(); i++) {
    char buf[128];
    const Instrument& in = song_.instruments[i];
    static const char* const KIND[INS_TYPE_COUNT] = {nullptr, "FM", "Sample", "AY", "SN", "GB", "OPL"};
    const char* kind = in.type > 0 && in.type < INS_TYPE_COUNT ? KIND[in.type] : WAVE_NAMES[std::clamp(in.wave, 0, WAVE_COUNT - 1)];
    std::snprintf(buf, sizeof(buf), "%02X  %-9s %s", i, kind, in.name.c_str());
    if (ImGui::Selectable(buf, i == curIns_, ImGuiSelectableFlags_AllowDoubleClick)) {
      curIns_ = i;
      if (ImGui::IsMouseDoubleClicked(0)) {
        showInsEditor_ = true;
        ImGui::SetWindowFocus("###Instrument editor");
      }
    }
  }
  ImGui::EndChild();
  ImGui::End();
}

bool App::wavetableEditor(Instrument& ins) {
  bool changed = false;
  ImGui::TextUnformatted(T("Wavetable (32 steps, 0-15)"));
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
  if (ImGui::SmallButton(T("Random"))) {
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
    ImGui::SetTooltip(T("Type values separated by spaces.\n'|' marks the loop start, '/' after a value holds it until note off."));

  // Keeps the next widget on the same line when it fits.
  auto sameLineIfFits = [](float width) {
    ImGui::SameLine();
    if (ImGui::GetContentRegionAvail().x < width) ImGui::NewLine();
  };
  const float fieldW = S(90), labelW = S(70);
  int len = (int)m.values.size();
  ImGui::SetNextItemWidth(fieldW);
  if (ImGui::InputInt(T("Length"), &len)) {
    len = std::clamp(len, 0, MAX_MACRO_LEN);
    int fill = m.values.empty() ? (type == MACRO_VOL ? 15 : type == MACRO_DUTY ? ins.duty : type == MACRO_WAVE ? ins.wave : 0) : m.values.back();
    m.values.resize(len, fill);
    if (m.loop >= len) m.loop = -1;
    if (m.release >= len) m.release = -1;
    changed = true;
  }
  sameLineIfFits(fieldW + labelW);
  ImGui::SetNextItemWidth(fieldW);
  if (ImGui::InputInt(T("Loop"), &m.loop)) {
    m.loop = std::clamp(m.loop, -1, len - 1);
    changed = true;
  }
  sameLineIfFits(fieldW + labelW);
  ImGui::SetNextItemWidth(fieldW);
  if (ImGui::InputInt(T("Release"), &m.release)) {
    m.release = std::clamp(m.release, -1, len - 1);
    changed = true;
  }
  sameLineIfFits(60);
  if (ImGui::Button(T("Clear"))) {
    m = Macro();
    changed = true;
  }

  // Show where the macro is while the current channel plays it.
  int playing = -1;
  const ChannelState& st = engine_->channel(curCh_);
  if (st.ins == curIns_ && st.active && st.macros[type].hasValue) playing = std::max(0, st.macros[type].pos - 1);

  static const ImU32 colors[MACRO_COUNT] = {IM_COL32(120, 225, 140, 255), IM_COL32(255, 160, 80, 255), IM_COL32(200, 140, 255, 255),
                                            IM_COL32(120, 200, 255, 255), IM_COL32(255, 220, 120, 255), IM_COL32(255, 120, 170, 255)};
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
    ImGui::TextDisabled(T("blue = loop, red = release (hold until note off)"));
  }

  switch (type) {
    case MACRO_VOL: ImGui::TextDisabled(T("Volume 0-%d per tick, multiplied with the channel volume."), hi); break;
    case MACRO_ARP: ImGui::TextDisabled(T("Semitone offset per tick. %d+n plays the fixed note n."), ARP_FIXED); break;
    case MACRO_DUTY:
      if (ins.type == INS_AY) ImGui::TextDisabled(T("Noise period 0-31 per tick (lower = higher pitched noise)."));
      else if (ins.type == INS_SN) ImGui::TextDisabled(T("Noise mode per tick: 0 periodic, 1 white."));
      else if (ins.type == INS_GB && ins.chip.gbChannel == 2) ImGui::TextDisabled(T("Noise per tick: 0 long, 1 short (metallic)."));
      else if (ins.type == INS_GB) ImGui::TextDisabled(T("Pulse duty: 0=12.5%% 1=25%% 2=50%% 3=75%%."));
      else if (ins.pulseWidth >= 0) ImGui::TextDisabled(T("Fine pulse width 1-255 (128 = square)."));
      else ImGui::TextDisabled(T("Pulse duty: 0=12.5%% 1=25%% 2=50%% 3=75%%. Noise: 1 = metallic, 3 = periodic."));
      break;
    case MACRO_CUTOFF: ImGui::TextDisabled(T("Filter cutoff 0-255 per tick (needs the filter on).")); break;
    case MACRO_WAVE:
      if (ins.type == INS_AY) {
        ImGui::TextDisabled(T("Mixer per tick: add 1 tone, 2 noise, 4 envelope (7 = all)."));
      } else if (ins.type == INS_OPL) {
        ImGui::TextDisabled(T("OPL waveform 0-7 for every operator."));
      } else {
        ImGui::TextDisabled(T("0 pulse, 1 triangle, 2 saw, 3 noise, 4 sine, 5 wavetable."));
        ImGui::TextDisabled(T("On a wavetable channel: index of a song wavetable."));
      }
      break;
    case MACRO_PITCH: ImGui::TextDisabled(T("Pitch change per tick in 1/32 semitone (adds up).")); break;
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
  changed |= ImGui::SliderInt(T("Algorithm"), &fm.alg, 0, 7);
  ImGui::SameLine();
  ImGui::TextColored(ImVec4(0.6f, 0.9f, 1, 1), "%s", ALG_DIAGRAMS[std::clamp(fm.alg, 0, 7)]);
  ImGui::SetNextItemWidth(w);
  changed |= ImGui::SliderInt(T("Feedback"), &fm.fb, 0, 7);
  ImGui::SameLine();
  ImGui::TextDisabled(T("Presets:"));
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
      ImGui::TextColored(carrier ? ImVec4(1, 0.8f, 0.3f, 1) : ImVec4(0.6f, 0.8f, 1, 1), T("OP%d %s"), o + 1, carrier ? "out" : "mod");
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
  ImGui::TextDisabled(T("Orange = carriers (heard), blue = modulators. Lower TL on a modulator = brighter."));
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
  if (ImGui::BeginCombo(T("Sample"), smpLabel(ins.sample))) {
    if (ImGui::Selectable(T("(none)"), ins.sample < 0)) {
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
  if (ImGui::Button(T("Load sample..."))) {
    loadSampleIntoIns_ = curIns_;
    openFileDialog(FileDialogMode::LoadSample);
  }
  ImGui::SameLine();
  if (ImGui::Button(T("Edit sample"))) {
    showSamples_ = true;
    if (ins.sample >= 0) curSample_ = ins.sample;
    ImGui::SetWindowFocus("###Samples");
  }
  float fade = ins.fadeout * 1000;
  ImGui::SetNextItemWidth(S(160));
  if (ImGui::SliderFloat(T("Fadeout"), &fade, 0, 100, "%.1f")) {
    ins.fadeout = fade / 1000;
    changed = true;
  }
  if (ImGui::IsItemHovered()) ImGui::SetTooltip(T("Volume lost per tick after note off (0 = cut at note off unless the volume macro has a release)"));
  if (!ins.sampleMap.empty()) {
    int used = 0;
    for (int v : ins.sampleMap) used += v >= 0;
    ImGui::TextDisabled(T("Multi-sample map: %d notes mapped"), used);
    ImGui::SameLine();
    if (ImGui::SmallButton(T("Remove map"))) {
      ins.sampleMap.clear();
      changed = true;
    }
  }
  ImGui::TextDisabled(T("The sample plays at its own rate on C-5."));
  return changed;
}

// SID-style options: the resonant filter (any instrument), and for chip
// instruments ring modulation, hard sync and a fine pulse width with PWM.
bool App::sidEditor(Instrument& ins) {
  bool changed = false;
  if (!ImGui::CollapsingHeader(T("SID: filter, ring mod, sync, PWM"), ins.sidDefault() ? 0 : ImGuiTreeNodeFlags_DefaultOpen)) return false;
  float w = std::min(ImGui::GetContentRegionAvail().x - 110.0f, 240.0f);
  changed |= ImGui::Checkbox(T("Filter"), &ins.filter.on);
  ImGui::BeginDisabled(!ins.filter.on);
  ImGui::SameLine();
  ImGui::SetNextItemWidth(w * 0.6f);
  changed |= ImGui::Combo("##fmode", &ins.filter.mode, TC("Low-pass\0Band-pass\0High-pass\0"));
  float hz = 30.0f * std::pow(2.0f, ins.filter.cutoff / 255.0f * 9.2f);
  char fmt[32];
  std::snprintf(fmt, sizeof(fmt), "%%d (%.0f Hz)", hz);
  ImGui::SetNextItemWidth(w);
  changed |= ImGui::SliderInt(T("Cutoff"), &ins.filter.cutoff, 0, 255, fmt);
  ImGui::SetNextItemWidth(w);
  changed |= ImGui::SliderInt(T("Resonance"), &ins.filter.resonance, 0, 15);
  ImGui::EndDisabled();
  if (ins.filter.on) ImGui::TextDisabled(T("The Cutoff macro and 13xx move the cutoff."));

  if (ins.type == INS_STANDARD) {
    changed |= ImGui::Checkbox(T("Ring mod"), &ins.ringMod);
    ImGui::SameLine();
    changed |= ImGui::Checkbox(T("Sync"), &ins.sync);
    ImGui::SameLine();
    ImGui::TextDisabled("(?)");
    if (ImGui::IsItemHovered())
      ImGui::SetTooltip(T("Both use the previous channel (channel 1 uses the last one).\n"
                        "Ring mod: multiplies by its square wave (bell sounds, best on triangle).\n"
                        "Sync: restarts this waveform with it (play a higher note here)."));
    bool fine = ins.pulseWidth >= 0;
    if (ImGui::Checkbox(T("Fine pulse width"), &fine)) {
      static const int steps[4] = {32, 64, 128, 192};
      // Convert the duty macro between the 4 steps and 1..255.
      for (int& v : ins.macros[MACRO_DUTY].values) v = fine ? steps[v & 3] : std::clamp((v + 16) / 64, 0, 3);
      ins.pulseWidth = fine ? steps[std::clamp(ins.duty, 0, 3)] : -1;
      if (!fine) ins.pwmSweep = 0;
      changed = true;
    }
    if (fine) {
      ImGui::SetNextItemWidth(w);
      changed |= ImGui::SliderInt(T("Pulse width"), &ins.pulseWidth, 1, 255);
      ImGui::SetNextItemWidth(w);
      changed |= ImGui::SliderInt(T("PWM sweep"), &ins.pwmSweep, -16, 16);
      if (ImGui::IsItemHovered()) ImGui::SetTooltip(T("Pulse width change per tick, bouncing between thin and wide."));
    }
  }
  return changed;
}

// AY / YM2149, SN76489 and Game Boy settings.
bool App::chipEditor(Instrument& ins) {
  bool changed = false;
  ChipSettings& c = ins.chip;
  float w = std::min(ImGui::GetContentRegionAvail().x - 110.0f, 240.0f);
  int nClocks = chipClockCount(ins.type);
  if (nClocks > 1) {
    ImGui::SetNextItemWidth(w);
    if (ImGui::BeginCombo(T("Clock"), chipClockName(ins.type, c.clock))) {
      for (int i = 0; i < nClocks; i++)
        if (ImGui::Selectable(chipClockName(ins.type, i), i == c.clock)) {
          c.clock = i;
          changed = true;
        }
      ImGui::EndCombo();
    }
  }
  if (ins.type == INS_AY) {
    ImGui::SetNextItemWidth(w);
    changed |= ImGui::Combo(T("Model"), &c.ayModel, TC("AY-3-8910\0YM2149 (finer envelope)\0"));
    ImGui::SetNextItemWidth(w);
    changed |= ImGui::SliderInt(T("Volume"), &ins.volume, 0, 15);
    changed |= ImGui::Checkbox(T("Tone"), &c.ayTone);
    ImGui::SameLine();
    changed |= ImGui::Checkbox(T("Noise"), &c.ayNoise);
    ImGui::SameLine();
    changed |= ImGui::Checkbox(T("Envelope"), &c.ayEnv);
    if (ImGui::IsItemHovered()) ImGui::SetTooltip(T("The hardware envelope sets the volume (the Volume slider is ignored)."));
    ImGui::BeginDisabled(!c.ayNoise);
    ImGui::SetNextItemWidth(w);
    changed |= ImGui::SliderInt(T("Noise period"), &c.ayNoiseFreq, 0, 31);
    ImGui::EndDisabled();
    if (c.ayEnv) {
      static const char* shapes[16] = {"0  \\___", "1  \\___", "2  \\___", "3  \\___", "4  /___", "5  /___", "6  /___",
                                       "7  /___", "8  \\\\\\\\ saw down", "9  \\___", "10 \\/\\/ triangle",
                                       "11 \\^^^", "12 //// saw up", "13 /^^^", "14 /\\/\\ triangle", "15 /___"};
      ImGui::SetNextItemWidth(w);
      changed |= ImGui::Combo(T("Shape"), &c.ayEnvShape, shapes, 16);
      ImGui::SetNextItemWidth(w);
      changed |= ImGui::Combo(T("Env. pitch"), &c.ayEnvAuto, TC("Manual period\0Note x1/4\0Note x1/2\0Note x1\0Note x2\0Note x4\0"));
      if (ImGui::IsItemHovered())
        ImGui::SetTooltip(T("Following the note turns the envelope into a waveform: the AY \"buzzer\" bass.\n"
                          "Try Tone off, Envelope on, saw shape, Note x1."));
      if (c.ayEnvAuto == 0) {
        ImGui::SetNextItemWidth(w);
        changed |= ImGui::InputInt(T("Env. period"), &c.ayEnvPeriod, 10, 100);
        c.ayEnvPeriod = std::clamp(c.ayEnvPeriod, 1, 65535);
        ImGui::SameLine();
        ImGui::TextDisabled(T("%.2f Hz"), chipClockHz(INS_AY, c.clock) / (256.0 * c.ayEnvPeriod));
      }
    }
    ImGui::TextDisabled(T("Macros: Duty = noise period, Waveform = mixer (1 tone, 2 noise, 4 envelope)."));
  } else if (ins.type == INS_SN) {
    ImGui::SetNextItemWidth(w);
    changed |= ImGui::SliderInt(T("Volume"), &ins.volume, 0, 15);
    int kind = c.snNoise ? (c.snWhite ? 1 : 2) : 0;
    ImGui::SetNextItemWidth(w);
    if (ImGui::Combo(T("Channel"), &kind, TC("Tone (square)\0White noise\0Periodic noise (tonal, buzzy)\0"))) {
      c.snNoise = kind != 0;
      c.snWhite = kind == 1;
      changed = true;
    }
    ImGui::TextDisabled(T("Volume in 2 dB steps. Duty macro on noise: 0 periodic, 1 white."));
  } else if (ins.type == INS_GB) {
    ImGui::SetNextItemWidth(w);
    changed |= ImGui::Combo(T("Channel"), &c.gbChannel, TC("Pulse\0Wave (wavetable)\0Noise\0"));
    if (c.gbChannel == 0) {
      static const char* dutyNames[4] = {"12.5%%", "25%%", "50%%", "75%%"};
      ImGui::SetNextItemWidth(w);
      changed |= ImGui::SliderInt(T("Duty"), &ins.duty, 0, 3, dutyNames[std::clamp(ins.duty, 0, 3)]);
    } else if (c.gbChannel == 2) {
      changed |= ImGui::Checkbox(T("Short noise (7-bit, metallic)"), &c.gbShortNoise);
    }
    ImGui::SetNextItemWidth(w);
    changed |= ImGui::SliderInt(c.gbChannel == 1 ? "Volume" : "Start volume", &ins.volume, 0, 15);
    if (c.gbChannel == 1) {
      ImGui::TextDisabled(T("The wave channel has 4 volumes: off, 25%%, 50%%, 100%%."));
      changed |= wavetableEditor(ins);
      changed |= morphEditor(ins);
    } else {
      ImGui::SetNextItemWidth(w * 0.5f);
      changed |= ImGui::Combo("##envdir", &c.gbEnvDir, TC("Fade out\0Fade in\0"));
      ImGui::SameLine();
      ImGui::SetNextItemWidth(w * 0.5f - ImGui::GetStyle().ItemSpacing.x);
      char fmt[48];
      std::snprintf(fmt, sizeof(fmt), c.gbEnvLen ? "%%d (%.0f ms/step)" : "off", c.gbEnvLen * 1000.0f / 64);
      changed |= ImGui::SliderInt(T("Envelope"), &c.gbEnvLen, 0, 7, fmt);
      if (c.gbChannel == 0) {
        ImGui::SetNextItemWidth(w * 0.5f);
        std::snprintf(fmt, sizeof(fmt), c.gbSweepTime ? "%%d (%.0f ms)" : "sweep off", c.gbSweepTime * 1000.0f / 128);
        changed |= ImGui::SliderInt("##swt", &c.gbSweepTime, 0, 7, fmt);
        ImGui::SameLine();
        ImGui::SetNextItemWidth(w * 0.25f);
        changed |= ImGui::Combo("##swd", &c.gbSweepDir, TC("Up\0Down\0"));
        ImGui::SameLine();
        ImGui::SetNextItemWidth(w * 0.25f - ImGui::GetStyle().ItemSpacing.x * 2);
        changed |= ImGui::SliderInt(T("Sweep"), &c.gbSweepShift, 0, 7);
        if (ImGui::IsItemHovered()) ImGui::SetTooltip(T("Hardware pitch sweep: time, direction and shift (bigger shift = slower)."));
      }
      ImGui::TextDisabled(T("A Volume macro replaces the hardware envelope."));
    }
  }
  return changed;
}

// OPL3: 2 or 4 operators, 8 waveforms.
bool App::oplEditor(Instrument& ins) {
  bool changed = false;
  OPLParams& p = ins.opl;
  float w = std::min(ImGui::GetContentRegionAvail().x * 0.35f, 160.0f);
  if (ImGui::Checkbox(T("4 operators"), &p.fourOp)) {
    p.alg = std::clamp(p.alg, 0, p.fourOp ? 3 : 1);
    changed = true;
  }
  ImGui::SameLine();
  changed |= ImGui::Checkbox(T("Deep AM"), &p.deepAm);
  ImGui::SameLine();
  changed |= ImGui::Checkbox(T("Deep vibrato"), &p.deepVib);
  ImGui::SetNextItemWidth(w * 1.4f);
  if (p.fourOp)
    changed |= ImGui::Combo(T("Algorithm"), &p.alg, TC("1>2>3>4\0" "1 + 2>3>4\0" "1>2 + 3>4\0" "1 + 2>3 + 4\0"));
  else
    changed |= ImGui::Combo(T("Algorithm"), &p.alg, TC("FM: 1>2\0Additive: 1 + 2\0"));
  ImGui::SetNextItemWidth(w);
  changed |= ImGui::SliderInt(T("Feedback"), &p.fb, 0, 7);
  int nops = p.fourOp ? 4 : 2;
  bool carrier[4] = {p.alg == 1 && !p.fourOp, true, false, false};
  if (p.fourOp) {
    static const bool C4[4][4] = {{0, 0, 0, 1}, {1, 0, 0, 1}, {0, 1, 0, 1}, {1, 0, 1, 1}};
    for (int k = 0; k < 4; k++) carrier[k] = C4[std::clamp(p.alg, 0, 3)][k];
  }
  struct Param {
    const char* name;
    int OPLOperator::*field;
    int max;
    const char* help;
  };
  static const Param params[] = {
      {"MULT", &OPLOperator::mult, 15, "Frequency multiplier (0 = x0.5)"},
      {"TL", &OPLOperator::tl, 63, "Total level: attenuation, higher = quieter"},
      {"AR", &OPLOperator::ar, 15, "Attack rate"},
      {"DR", &OPLOperator::dr, 15, "Decay rate"},
      {"SL", &OPLOperator::sl, 15, "Sustain level (attenuation after decay)"},
      {"RR", &OPLOperator::rr, 15, "Release rate"},
      {"KSL", &OPLOperator::ksl, 3, "Key scale level: high notes get quieter"},
      {"WAVE", &OPLOperator::ws, 7, "Waveform: 0 sine, 1 half sine, 2 abs sine, 3 quarter sine,\n4 alternating sine, 5 camel sine, 6 square, 7 log saw"},
  };
  static const Param flags[] = {
      {"SUS", &OPLOperator::sus, 1, "Sustain: hold at the sustain level until note off"},
      {"AM", &OPLOperator::am, 1, "Tremolo"},
      {"VIB", &OPLOperator::vib, 1, "Vibrato"},
      {"KSR", &OPLOperator::ksr, 1, "Key scaling: faster envelopes for high notes"},
  };
  if (ImGui::BeginTable("oplops", nops + 1, ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_SizingStretchSame)) {
    ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed, 40);
    for (int o = 0; o < nops; o++) ImGui::TableSetupColumn("");
    ImGui::TableNextRow();
    ImGui::TableNextColumn();
    for (int o = 0; o < nops; o++) {
      ImGui::TableNextColumn();
      ImGui::TextColored(carrier[o] ? ImVec4(1, 0.8f, 0.3f, 1) : ImVec4(0.6f, 0.8f, 1, 1), T("OP%d %s"), o + 1, carrier[o] ? "out" : "mod");
    }
    for (const Param& prm : params) {
      ImGui::TableNextRow();
      ImGui::TableNextColumn();
      ImGui::TextUnformatted(prm.name);
      if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", prm.help);
      for (int o = 0; o < nops; o++) {
        ImGui::TableNextColumn();
        ImGui::PushID(o * 100 + (int)(&prm - params));
        ImGui::SetNextItemWidth(-1);
        changed |= ImGui::SliderInt("##v", &(p.ops[o].*prm.field), 0, prm.max);
        ImGui::PopID();
      }
    }
    for (const Param& fl : flags) {
      ImGui::TableNextRow();
      ImGui::TableNextColumn();
      ImGui::TextUnformatted(fl.name);
      if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", fl.help);
      for (int o = 0; o < nops; o++) {
        ImGui::TableNextColumn();
        ImGui::PushID(o * 100 + 50 + (int)(&fl - flags));
        bool on = p.ops[o].*fl.field != 0;
        if (ImGui::Checkbox("##f", &on)) {
          p.ops[o].*fl.field = on ? 1 : 0;
          changed = true;
        }
        ImGui::PopID();
      }
    }
    ImGui::EndTable();
  }
  ImGui::TextDisabled(T("Orange = carriers (heard). The Waveform macro and 10xx set every operator's wave."));
  return changed;
}

// ADSR envelope and the two LFOs (any instrument type).
bool App::modulationEditor(Instrument& ins) {
  bool used = ins.env.on;
  for (const InsLfo& l : ins.lfo) used |= l.on;
  if (!ImGui::CollapsingHeader(T("Envelope & LFO"), used ? ImGuiTreeNodeFlags_DefaultOpen : 0)) return false;
  bool changed = false;
  float w = std::min(ImGui::GetContentRegionAvail().x - 110.0f, 240.0f);
  const ImGuiSliderFlags logFlags = ImGuiSliderFlags_Logarithmic;

  changed |= ImGui::Checkbox(T("ADSR"), &ins.env.on);
  if (ImGui::IsItemHovered())
    ImGui::SetTooltip(T("Volume envelope on top of the instrument's volume.\nThe note rings out for the release time after note off."));
  if (ins.env.on) {
    InsEnvelope& e = ins.env;
    // Shape preview.
    ImVec2 p = ImGui::GetCursorScreenPos();
    ImVec2 size(w, 46);
    ImGui::Dummy(size);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(p, ImVec2(p.x + size.x, p.y + size.y), IM_COL32(15, 15, 22, 255));
    float total = e.attack + e.decay + 0.4f + e.release;
    float sx = size.x / std::max(total, 0.01f);
    auto pt = [&](float t, float lvl) { return ImVec2(p.x + t * sx, p.y + size.y - 2 - lvl * (size.y - 4)); };
    ImVec2 pts[5] = {pt(0, 0), pt(e.attack, 1), pt(e.attack + e.decay, e.sustain), pt(e.attack + e.decay + 0.4f, e.sustain),
                     pt(total, 0)};
    dl->AddPolyline(pts, 5, IM_COL32(110, 230, 140, 255), 0, 2.0f);
    ImGui::SetNextItemWidth(w);
    changed |= ImGui::SliderFloat(T("Attack"), &e.attack, 0, 10, "%.3f s", logFlags);
    ImGui::SetNextItemWidth(w);
    changed |= ImGui::SliderFloat(T("Decay"), &e.decay, 0, 10, "%.3f s", logFlags);
    ImGui::SetNextItemWidth(w);
    changed |= ImGui::SliderFloat(T("Sustain"), &e.sustain, 0, 1, "%.2f");
    ImGui::SetNextItemWidth(w);
    changed |= ImGui::SliderFloat(T("Release"), &e.release, 0, 10, "%.3f s", logFlags);
  }

  for (int l = 0; l < INS_LFOS; l++) {
    InsLfo& lfo = ins.lfo[l];
    ImGui::PushID(l);
    char label[16];
    std::snprintf(label, sizeof(label), "LFO %d", l + 1);
    changed |= ImGui::Checkbox(label, &lfo.on);
    if (lfo.on) {
      ImGui::SameLine();
      ImGui::SetNextItemWidth(w * 0.55f);
      if (ImGui::Combo("##target", &lfo.target, LFO_TARGET_NAMES, LFO_TARGET_COUNT)) {
        // A sensible depth for the new target.
        static const float defaults[LFO_TARGET_COUNT] = {0.3f, 0.5f, 0.3f, 40.0f, 0.5f};
        lfo.depth = defaults[std::clamp(lfo.target, 0, LFO_TARGET_COUNT - 1)];
        changed = true;
      }
      ImGui::SameLine();
      ImGui::SetNextItemWidth(w * 0.45f);
      changed |= ImGui::Combo("##shape", &lfo.shape, LFO_SHAPE_NAMES, LFO_SHAPE_COUNT);
      ImGui::SetNextItemWidth(w);
      changed |= ImGui::SliderFloat(T("Rate"), &lfo.rate, 0.05f, 30.0f, "%.2f Hz", logFlags);
      ImGui::SetNextItemWidth(w);
      switch (lfo.target) {
        case LFO_PITCH: changed |= ImGui::SliderFloat(T("Depth"), &lfo.depth, 0, 12, "%.2f semitones", logFlags); break;
        case LFO_CUTOFF: changed |= ImGui::SliderFloat(T("Depth"), &lfo.depth, 0, 255, "%.0f"); break;
        default: changed |= ImGui::SliderFloat(T("Depth"), &lfo.depth, 0, 1, "%.2f"); break;
      }
      if (lfo.target == LFO_CUTOFF && !ins.filter.on) ImGui::TextDisabled(T("Turn the filter on (SID section) to hear it."));
      if (lfo.target == LFO_PULSE && !(ins.type == INS_STANDARD && ins.wave == WAVE_PULSE))
        ImGui::TextDisabled(T("Pulse width only changes pulse waves."));
      ImGui::SetNextItemWidth(w);
      changed |= ImGui::SliderFloat(T("Delay"), &lfo.delay, 0, 5, "%.2f s");
      if (ImGui::IsItemHovered()) ImGui::SetTooltip(T("Time after the note starts before the LFO fades in."));
    }
    ImGui::PopID();
  }
  return changed;
}

// Second wavetable the wave morphs to over time.
bool App::morphEditor(Instrument& ins) {
  bool changed = ImGui::Checkbox(T("Morph"), &ins.morph.on);
  if (ImGui::IsItemHovered()) ImGui::SetTooltip(T("Each note morphs from the wavetable above to this one."));
  if (!ins.morph.on) return changed;
  ImGui::SameLine();
  ImGui::SetNextItemWidth(S(110));
  changed |= ImGui::Combo("##mmode", &ins.morph.mode, TC("Once\0Loop\0Ping-pong\0"));
  ImGui::SameLine();
  ImGui::SetNextItemWidth(std::max(80.0f, ImGui::GetContentRegionAvail().x - 60));
  changed |= ImGui::SliderFloat(T("Time"), &ins.morph.time, 0.01f, 20.0f, "%.2f s", ImGuiSliderFlags_Logarithmic);
  std::vector<int> v(ins.morph.table.begin(), ins.morph.table.end());
  if (barGraph("##morphtable", v, 0, 15, 80, IM_COL32(255, 160, 220, 255))) {
    std::copy(v.begin(), v.end(), ins.morph.table.begin());
    changed = true;
  }
  if (ImGui::SmallButton(T("Copy from wavetable"))) {
    ins.morph.table = ins.wavetable;
    changed = true;
  }
  ImGui::SameLine();
  if (ImGui::SmallButton(T("Invert"))) {
    for (int& x : ins.morph.table) x = 15 - x;
    changed = true;
  }
  ImGui::SameLine();
  if (ImGui::SmallButton(T("Random##m"))) {
    for (int& x : ins.morph.table) x = std::rand() % 16;
    changed = true;
  }
  return changed;
}

void App::instrumentEditor() {
  if (!ImGui::Begin(TW("Instrument editor").c_str(), &showInsEditor_)) {
    ImGui::End();
    return;
  }
  if (song_.instruments.empty()) {
    ImGui::TextDisabled(T("No instrument"));
    ImGui::End();
    return;
  }
  curIns_ = std::clamp(curIns_, 0, (int)song_.instruments.size() - 1);
  Instrument& ins = song_.instruments[curIns_];
  bool changed = false;

  ImGui::Text(T("Instrument %02X"), curIns_);
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
  changed |= ImGui::Combo(T("Type"), &ins.type, INS_TYPE_NAMES, INS_TYPE_COUNT);

  if (ins.type == INS_STANDARD) {
    ImGui::SetNextItemWidth(w);
    changed |= ImGui::Combo(T("Waveform"), &ins.wave, WAVE_NAMES, WAVE_COUNT);
    static const char* dutyNames[4] = {"12.5%%", "25%%", "50%%", "75%%"};
    ImGui::BeginDisabled(ins.pulseWidth >= 0 && ins.wave == WAVE_PULSE);  // the fine width wins
    ImGui::SetNextItemWidth(w);
    changed |= ImGui::SliderInt(T("Duty"), &ins.duty, 0, 3, dutyNames[std::clamp(ins.duty, 0, 3)]);
    ImGui::EndDisabled();
    ImGui::SetNextItemWidth(w);
    changed |= ImGui::SliderInt(T("Volume"), &ins.volume, 0, 15);
    bool usesTable = ins.wave == WAVE_TABLE;
    for (int v : ins.macros[MACRO_WAVE].values) usesTable |= v == WAVE_TABLE;
    if (usesTable) {
      if (ins.songWave >= 0) {
        ImGui::TextDisabled(T("Uses song wavetable %d."), ins.songWave);
        ImGui::SameLine();
        if (ImGui::SmallButton(T("Use own table"))) {
          ins.songWave = -1;
          changed = true;
        }
      } else {
        changed |= wavetableEditor(ins);
        changed |= morphEditor(ins);
      }
    }
  } else if (ins.type == INS_FM) {
    changed |= fmEditor(ins);
  } else if (ins.type == INS_OPL) {
    changed |= oplEditor(ins);
  } else if (isChipType(ins.type)) {
    changed |= chipEditor(ins);
  } else {
    changed |= sampleInsEditor(ins);
  }
  changed |= sidEditor(ins);
  changed |= modulationEditor(ins);
  ImGui::TextDisabled(T("Play it with the keyboard: Z S X D C... / Q 2 W 3 E..."));

  ImGui::Separator();
  if (ImGui::BeginTabBar("macros")) {
    for (int t = 0; t < MACRO_COUNT; t++) {
      // The Duty and Waveform macros mean something else on each chip.
      const char* name = MACRO_NAMES[t];
      bool useful = true;
      if (t == MACRO_DUTY) {
        switch (ins.type) {
          case INS_STANDARD: break;
          case INS_AY: name = "Noise"; break;
          case INS_SN: name = "Noise mode"; useful = ins.chip.snNoise; break;
          case INS_GB: name = ins.chip.gbChannel == 2 ? "Noise mode" : "Duty"; useful = ins.chip.gbChannel != 1; break;
          default: useful = false; break;
        }
      } else if (t == MACRO_WAVE) {
        switch (ins.type) {
          case INS_STANDARD: break;
          case INS_AY: name = "Mixer"; break;
          case INS_OPL: name = "Waveform"; break;
          default: useful = false; break;
        }
      }
      if (!useful && ins.macros[t].values.empty()) continue;
      if (t == MACRO_CUTOFF && !ins.filter.on && ins.macros[t].values.empty()) continue;
      std::string label = name;
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

// After SameLine(): goes to the next line if the next item (w pixels wide)
// would not fit, so toolbars wrap in narrow windows.
static void wrapFor(float w) {
  if (ImGui::GetContentRegionAvail().x < w) ImGui::NewLine();
}
static float buttonWidth(const char* label) {
  return ImGui::CalcTextSize(label, nullptr, true).x + ImGui::GetStyle().FramePadding.x * 2;
}

// Settings, edit tools and the waveform of one sample. The waveform zooms
// with the wheel (Shift: scroll), drag selects; in pencil mode drag draws.
void App::sampleEditor(Sample& smp) {
  bool changed = false;
  int len = (int)smp.data.size();
  if (smpFor_ != curSample_) {
    smpFor_ = curSample_;
    smpViewStart_ = 0;
    smpViewLen_ = 0;
    smpSelA_ = smpSelB_ = 0;
    smpCursor_ = 0;
  }
  smpSelA_ = std::clamp(smpSelA_, 0, len);
  smpSelB_ = std::clamp(smpSelB_, 0, len);
  smpCursor_ = std::clamp(smpCursor_, 0, len);
  bool hasSel = smpSelA_ != smpSelB_;
  int a = std::min(smpSelA_, smpSelB_), b = std::max(smpSelA_, smpSelB_);

  char name[128];
  std::snprintf(name, sizeof(name), "%s", smp.name.c_str());
  ImGui::SetNextItemWidth(S(220));
  if (ImGui::InputText(T("Name"), name, sizeof(name))) {
    smp.name = name;
    changed = true;
  }
  ImGui::SameLine();
  wrapFor(220);
  ImGui::SetNextItemWidth(S(120));
  changed |= ImGui::InputInt(T("Rate at C-5"), &smp.rate, 100, 1000);
  smp.rate = std::clamp(smp.rate, 1, 1000000);
  ImGui::SetNextItemWidth(S(160));
  changed |= ImGui::SliderInt(T("Volume"), &smp.volume, 0, 64);
  ImGui::SameLine();
  wrapFor(160);
  ImGui::SetNextItemWidth(S(120));
  changed |= ImGui::Combo(T("Loop"), &smp.loopMode, LOOP_NAMES, 4);
  if (smp.loopMode) {
    ImGui::SetNextItemWidth(S(160));
    changed |= ImGui::SliderInt(T("Loop start"), &smp.loopStart, 0, std::max(0, len - 1));
    ImGui::SameLine();
    wrapFor(230);
    ImGui::SetNextItemWidth(S(160));
    changed |= ImGui::SliderInt(T("Loop end"), &smp.loopEnd, 0, len);
    smp.loopStart = std::clamp(smp.loopStart, 0, len);
    smp.loopEnd = std::clamp(smp.loopEnd, smp.loopStart, len);
  }

  // Edit tools: on the selection, or on the whole sample.
  auto edit = [&](const char* label, const char* tip, auto fn, bool needSel = false) {
    wrapFor(buttonWidth(label));
    ImGui::BeginDisabled(needSel && !hasSel);
    bool clicked = ImGui::SmallButton(label);
    ImGui::EndDisabled();
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("%s", tip);
    if (clicked) {
      pushFullUndo();
      fn();
      engine_->reset();  // no voice keeps playing past a shortened sample
      len = (int)smp.data.size();  // pushFullUndo() already marked the song dirty
    }
    ImGui::SameLine();
  };
  const char* scope = hasSel ? "the selection" : "the whole sample";
  std::string t;
  edit("Cut", "Copy the selection and remove it", [&] {
    smpClipboard_ = sampleops::copy(smp, a, b);
    sampleops::erase(smp, a, b);
    smpCursor_ = a;
    smpSelA_ = smpSelB_ = a;
  }, true);
  wrapFor(buttonWidth("Copy"));
  if (ImGui::SmallButton(T("Copy")) && hasSel) smpClipboard_ = sampleops::copy(smp, a, b);
  ImGui::SameLine();
  ImGui::BeginDisabled(smpClipboard_.empty());
  edit("Paste", "Insert the clipboard at the cursor (replaces the selection)", [&] {
    int at = smpCursor_;
    if (hasSel) {
      sampleops::erase(smp, a, b);
      at = a;
    }
    sampleops::insert(smp, at, smpClipboard_);
    smpSelA_ = at;
    smpSelB_ = at + (int)smpClipboard_.size();
  });
  ImGui::EndDisabled();
  edit("Delete", "Remove the selection", [&] {
    sampleops::erase(smp, a, b);
    smpCursor_ = a;
    smpSelA_ = smpSelB_ = a;
  }, true);
  edit("Crop", "Keep only the selection", [&] {
    sampleops::crop(smp, a, b);
    smpSelA_ = smpSelB_ = 0;
    smpViewLen_ = 0;
  }, true);
  edit("Silence", (t = std::string("Set ") + scope + " to silence").c_str(), [&] { sampleops::silence(smp, a, b); });
  ImGui::NewLine();
  edit("Normalize", (t = std::string("Bring the peak of ") + scope + " to full scale").c_str(), [&] { sampleops::normalize(smp, a, b); });
  edit("Fade in", "Fade in over the selection (or the whole sample)", [&] { sampleops::fadeIn(smp, a, b); });
  edit("Fade out", "Fade out over the selection (or the whole sample)", [&] { sampleops::fadeOut(smp, a, b); });
  edit("Reverse", (t = std::string("Play ") + scope + " backwards").c_str(), [&] { sampleops::reverse(smp, a, b); });
  edit("Invert", "Flip the polarity", [&] { sampleops::invert(smp, a, b); });
  edit("Remove DC", "Center the waveform on zero", [&] { sampleops::removeDc(smp, a, b); });
  ImGui::NewLine();
  ImGui::SetNextItemWidth(S(90));
  ImGui::DragFloat("##gain", &smpGainPct_, 1, 0, 1000, "%.0f %%");
  ImGui::SameLine();
  edit("Amplify", (t = std::string("Multiply ") + scope + " by this amount").c_str(),
       [&] { sampleops::amplify(smp, a, b, smpGainPct_ / 100.0f); });
  wrapFor(90 + buttonWidth("Resample"));
  ImGui::SetNextItemWidth(S(90));
  ImGui::InputInt("##rate", &smpNewRate_, 0, 0);
  smpNewRate_ = std::clamp(smpNewRate_, 1000, 192000);
  ImGui::SameLine();
  edit("Resample", "Convert the whole sample to this rate (same pitch; lower rates sound grittier and save space)",
       [&] { sampleops::resample(smp, smpNewRate_); });
  edit("Loop selection", "Loop over the selection", [&] {
    smp.loopStart = a;
    smp.loopEnd = b;
    if (!smp.loopMode) smp.loopMode = 1;
  }, true);
  ImGui::NewLine();
  ImGui::Checkbox(T("Pencil"), &smpDraw_);
  if (ImGui::IsItemHovered()) ImGui::SetTooltip(T("Draw on the waveform (zoom in for detail)"));
  ImGui::SameLine();
  if (ImGui::SmallButton(T("Show all"))) smpViewLen_ = 0;
  ImGui::SameLine();
  wrapFor(buttonWidth("Zoom to selection"));
  if (ImGui::SmallButton(T("Zoom to selection")) && hasSel) {
    smpViewStart_ = a;
    smpViewLen_ = b - a;
  }
  ImGui::SameLine();
  wrapFor(200);
  if (hasSel)
    ImGui::TextDisabled(T("Selection %d-%d (%d frames, %.3f s)"), a, b, b - a, (b - a) / (float)std::max(smp.rate, 1));
  else
    ImGui::TextDisabled(T("%d frames, %.2f s"), len, len / (float)std::max(smp.rate, 1));

  // Waveform.
  ImVec2 pos = ImGui::GetCursorScreenPos();
  ImVec2 size(std::max(ImGui::GetContentRegionAvail().x, 100.0f), std::max(ImGui::GetContentRegionAvail().y, 80.0f));
  ImGui::InvisibleButton("##wave", size);
  bool hovered = ImGui::IsItemHovered();
  if (hovered) ImGui::SetItemKeyOwner(ImGuiKey_MouseWheelY);
  if (smpViewLen_ <= 0 || smpViewLen_ > len) {
    smpViewStart_ = 0;
    smpViewLen_ = std::max(len, 1);
  }
  smpViewLen_ = std::max(smpViewLen_, std::min(32.0, (double)std::max(len, 1)));
  smpViewStart_ = std::clamp(smpViewStart_, 0.0, std::max(0.0, len - smpViewLen_));
  double vs = smpViewStart_, vl = smpViewLen_;
  ImGuiIO& io = ImGui::GetIO();
  auto frameAt = [&](float x) { return vs + (x - pos.x) / size.x * vl; };
  auto xOf = [&](double f) { return pos.x + (float)((f - vs) / vl * size.x); };
  if (hovered && io.MouseWheel != 0 && len > 0) {
    if (io.KeyShift) {
      smpViewStart_ -= io.MouseWheel * vl * 0.1;
    } else {
      double at = frameAt(io.MousePos.x);
      double nl = std::clamp(vl * std::pow(0.8, io.MouseWheel), 32.0, (double)len);
      smpViewStart_ = at - (at - vs) * nl / vl;
      smpViewLen_ = nl;
    }
  }
  float mid = pos.y + size.y / 2;
  if (len > 0) {
    if (ImGui::IsItemActivated()) {
      int f = std::clamp((int)std::lround(frameAt(io.MousePos.x)), 0, len);
      if (smpDraw_) {
        pushFullUndo();
        smpDrag_ = 2;
        smpLastDrawX_ = -1;
      } else {
        smpDrag_ = 1;
        if (io.KeyShift && hasSel) {
          // Shift+click extends the selection.
          if (std::abs(f - smpSelA_) < std::abs(f - smpSelB_)) std::swap(smpSelA_, smpSelB_);
          smpSelB_ = f;
        } else {
          smpSelA_ = smpSelB_ = f;
          smpCursor_ = f;
        }
      }
    }
    if (ImGui::IsItemActive() && smpDrag_ == 1) {
      smpSelB_ = std::clamp((int)std::lround(frameAt(io.MousePos.x)), 0, len);
    } else if (ImGui::IsItemActive() && smpDrag_ == 2) {
      int f = std::clamp((int)frameAt(io.MousePos.x), 0, len - 1);
      float v = std::clamp((mid - io.MousePos.y) / (size.y / 2), -1.0f, 1.0f);
      int f0 = smpLastDrawX_ < 0 ? f : smpLastDrawX_;
      float v0 = smpLastDrawX_ < 0 ? v : smpLastDrawV_;
      int lo = std::min(f0, f), hi = std::max(f0, f);
      for (int k = lo; k <= hi; k++) {
        float t2 = hi == lo ? 1.0f : (float)(k - lo) / (hi - lo);
        smp.data[k] = f0 <= f ? v0 + (v - v0) * t2 : v + (v0 - v) * t2;
      }
      smpLastDrawX_ = f;
      smpLastDrawV_ = v;
      dirty_ = true;
    }
    if (!ImGui::IsItemActive()) smpDrag_ = 0;
  }

  ImDrawList* dl = ImGui::GetWindowDrawList();
  dl->PushClipRect(pos, ImVec2(pos.x + size.x, pos.y + size.y), true);
  dl->AddRectFilled(pos, ImVec2(pos.x + size.x, pos.y + size.y), IM_COL32(15, 15, 22, 255));
  dl->AddLine(ImVec2(pos.x, mid), ImVec2(pos.x + size.x, mid), IM_COL32(50, 50, 70, 255));
  if (len > 0) {
    if (smp.loopMode && smp.loopEnd > smp.loopStart) {
      dl->AddRectFilled(ImVec2(xOf(smp.loopStart), pos.y), ImVec2(xOf(smp.loopEnd), pos.y + size.y), IM_COL32(60, 90, 160, 60));
      dl->AddLine(ImVec2(xOf(smp.loopStart), pos.y), ImVec2(xOf(smp.loopStart), pos.y + size.y), IM_COL32(90, 140, 255, 200));
      dl->AddLine(ImVec2(xOf(smp.loopEnd), pos.y), ImVec2(xOf(smp.loopEnd), pos.y + size.y), IM_COL32(90, 140, 255, 200));
    }
    if (smpSelA_ != smpSelB_)
      dl->AddRectFilled(ImVec2(xOf(std::min(smpSelA_, smpSelB_)), pos.y), ImVec2(xOf(std::max(smpSelA_, smpSelB_)), pos.y + size.y),
                        IM_COL32(255, 255, 255, 40));
    int cols = (int)size.x;
    float framesPerPx = (float)(vl / size.x);
    if (framesPerPx > 1.0f) {
      // Min/max per pixel column.
      for (int x = 0; x < cols; x++) {
        size_t f0 = (size_t)std::max(0.0, vs + x * (double)framesPerPx);
        size_t f1 = std::min((size_t)len, std::max(f0 + 1, (size_t)(vs + (x + 1) * (double)framesPerPx)));
        float lo = 1, hi = -1;
        for (size_t k = f0; k < f1; k++) {
          lo = std::min(lo, smp.data[k]);
          hi = std::max(hi, smp.data[k]);
        }
        if (hi < lo) continue;
        dl->AddLine(ImVec2(pos.x + x, mid - hi * size.y / 2), ImVec2(pos.x + x, mid - lo * size.y / 2 + 1), IM_COL32(110, 230, 140, 255));
      }
    } else {
      // Zoomed in: a line through the frames, with dots when they are far apart.
      int f0 = std::max(0, (int)vs - 1), f1 = std::min(len - 1, (int)(vs + vl) + 1);
      for (int k = f0; k < f1; k++)
        dl->AddLine(ImVec2(xOf(k), mid - smp.data[k] * size.y / 2), ImVec2(xOf(k + 1), mid - smp.data[k + 1] * size.y / 2),
                    IM_COL32(110, 230, 140, 255));
      if (framesPerPx < 0.2f)
        for (int k = f0; k <= f1; k++) dl->AddCircleFilled(ImVec2(xOf(k), mid - smp.data[k] * size.y / 2), 2.5f, IM_COL32(170, 255, 190, 255));
    }
    if (!hasSel) dl->AddLine(ImVec2(xOf(smpCursor_), pos.y), ImVec2(xOf(smpCursor_), pos.y + size.y), IM_COL32(255, 220, 90, 200));
    // Where the view is, when zoomed in.
    if (vl < len) {
      float x0 = pos.x + (float)(vs / len) * size.x, x1 = pos.x + (float)((vs + vl) / len) * size.x;
      dl->AddRectFilled(ImVec2(x0, pos.y + size.y - 4), ImVec2(std::max(x1, x0 + 3), pos.y + size.y), IM_COL32(255, 255, 255, 90));
    }
  } else {
    dl->AddText(ImVec2(pos.x + 8, pos.y + 8), IM_COL32(150, 150, 170, 255), "Empty sample");
  }
  dl->PopClipRect();
  if (hovered && len > 0) {
    int f = std::clamp((int)frameAt(io.MousePos.x), 0, len - 1);
    ImGui::SetTooltip(T("%d  (%.3f s)\nWheel: zoom, Shift+wheel: scroll%s"), f, f / (float)std::max(smp.rate, 1),
                      smpDraw_ ? "\nDrag: draw" : "\nDrag: select, Shift+click: extend");
  }
  if (changed) {
    dirty_ = true;
    metaTouched_ = true;
  }
}

void App::samplesWindow() {
  if (!ImGui::Begin(TW("Samples").c_str(), &showSamples_)) {
    ImGui::End();
    return;
  }
  bool changed = false;
  if (ImGui::Button(T("Load sample..."))) {
    loadSampleIntoIns_ = -1;
    openFileDialog(FileDialogMode::LoadSample);
  }
  ImGui::SameLine();
  int n = (int)song_.samples.size();
  if (ImGui::Button(T("Delete")) && curSample_ >= 0 && curSample_ < n) {
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
  if (ImGui::Button(T("Make instrument")) && curSample_ >= 0 && curSample_ < n && (int)song_.instruments.size() < MAX_INSTRUMENTS) {
    Instrument ins;
    ins.type = INS_SAMPLE;
    ins.sample = curSample_;
    ins.name = song_.samples[curSample_].name;
    song_.instruments.push_back(ins);
    curIns_ = (int)song_.instruments.size() - 1;
    changed = true;
  }

  ImGui::SameLine();
  wrapFor(buttonWidth("Record"));
  if (!recorder_.recording()) {
    if (ImGui::Button(T("Record"))) {
      std::string err;
      if (recorder_.start(recDevice_, 44100, err)) setStatus("Recording... press Stop to keep it");
      else setStatus("Cannot record: " + err);
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip(T("Record a new sample from the microphone or line input"));
    ImGui::SameLine();
    std::vector<std::string> devs = Recorder::devices();
    ImGui::SetNextItemWidth(std::max(80.0f, ImGui::GetContentRegionAvail().x));
    if (ImGui::BeginCombo("##recdev", recDevice_.empty() ? "Default input" : recDevice_.c_str())) {
      if (ImGui::Selectable(T("Default input"), recDevice_.empty())) recDevice_.clear();
      for (const std::string& d : devs)
        if (ImGui::Selectable(d.c_str(), d == recDevice_)) recDevice_ = d;
      if (devs.empty()) ImGui::TextDisabled(T("No input device found"));
      ImGui::EndCombo();
    }
  } else {
    ImGui::PushStyleColor(ImGuiCol_Button, IM_COL32(170, 40, 50, 255));
    bool stop = ImGui::Button(T("Stop"));
    ImGui::PopStyleColor();
    recPeak_ = std::max(recorder_.takePeak(), recPeak_ * 0.9f);
    ImGui::SameLine();
    ImGui::Text("%.1f s", recorder_.seconds());
    ImGui::SameLine();
    char lvl[16];
    std::snprintf(lvl, sizeof(lvl), "%.0f dB", 20 * std::log10(std::max(recPeak_, 1e-5f)));
    ImGui::ProgressBar(recPeak_, ImVec2(std::max(60.0f, ImGui::GetContentRegionAvail().x), 0), lvl);
    if (stop) {
      int rate = recorder_.rate();
      std::vector<float> data = recorder_.stop();
      if (data.empty()) {
        setStatus("Nothing was recorded");
      } else if ((int)song_.samples.size() < MAX_SAMPLES) {
        pushFullUndo();
        Sample smp;
        smp.name = "Recording " + std::to_string(song_.samples.size() + 1);
        smp.data = std::move(data);
        smp.rate = rate;
        smp.loopEnd = (int)smp.data.size();
        song_.samples.push_back(std::move(smp));
        curSample_ = (int)song_.samples.size() - 1;
        changed = true;
        setStatus("Recorded " + song_.samples.back().name);
      }
    }
  }

  ImGui::BeginChild("smplist", ImVec2(0, S(120)), ImGuiChildFlags_Borders | ImGuiChildFlags_ResizeY);
  for (int i = 0; i < (int)song_.samples.size(); i++) {
    char buf[160];
    std::snprintf(buf, sizeof(buf), "%02X  %s  (%zu)", i, song_.samples[i].name.c_str(), song_.samples[i].data.size());
    if (ImGui::Selectable(buf, i == curSample_)) curSample_ = i;
  }
  if (song_.samples.empty()) ImGui::TextDisabled(T("No samples. Load a WAV file, or import a MOD/XM/IT/S3M."));
  ImGui::EndChild();

  if (curSample_ >= 0 && curSample_ < (int)song_.samples.size()) sampleEditor(song_.samples[curSample_]);
  if (changed) {
    dirty_ = true;
    metaTouched_ = true;
  }
  ImGui::End();
}
