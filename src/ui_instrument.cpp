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

void textToMacro(const std::string& text, Macro& m, int type) {
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
        m.values.push_back(std::clamp(std::stoi(tok), MACRO_MIN[type], MACRO_MAX[type]));
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
  }
  ImGui::SameLine();
  if (ImGui::Button("Duplicate") && n < MAX_INSTRUMENTS && curIns_ < n) {
    Instrument copy = song_.instruments[curIns_];
    copy.name += " copy";
    song_.instruments.push_back(copy);
    curIns_ = n;
    dirty_ = true;
  }
  ImGui::SameLine();
  if (ImGui::Button("Delete") && n > 1 && curIns_ < n) {
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
    undo_.clear();
    redo_.clear();
    dirty_ = true;
  }
  ImGui::BeginChild("list");
  for (int i = 0; i < (int)song_.instruments.size(); i++) {
    char buf[128];
    std::snprintf(buf, sizeof(buf), "%02X  %-6s %s", i, WAVE_NAMES[song_.instruments[i].wave], song_.instruments[i].name.c_str());
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
    textToMacro(bufs[type], m, type);
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
  ImGui::PushID(type);
  if (barGraph("##graph", m.values, MACRO_MIN[type], MACRO_MAX[type], 140, colors[type], playing)) changed = true;
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
    case MACRO_VOL: ImGui::TextDisabled("Volume 0-15 per tick, multiplied with the channel volume."); break;
    case MACRO_ARP: ImGui::TextDisabled("Semitone offset per tick (relative to the note)."); break;
    case MACRO_DUTY: ImGui::TextDisabled("Pulse duty: 0=12.5%% 1=25%% 2=50%% 3=75%%. Noise: odd = metallic."); break;
    case MACRO_WAVE: ImGui::TextDisabled("0 pulse, 1 triangle, 2 saw, 3 noise, 4 sine, 5 wavetable."); break;
    case MACRO_PITCH: ImGui::TextDisabled("Pitch change per tick in 1/32 semitone (adds up)."); break;
  }
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
  changed |= ImGui::Combo("Waveform", &ins.wave, WAVE_NAMES, WAVE_COUNT);
  static const char* dutyNames[4] = {"12.5%", "25%", "50%", "75%"};
  ImGui::SetNextItemWidth(w);
  changed |= ImGui::SliderInt("Duty", &ins.duty, 0, 3, dutyNames[std::clamp(ins.duty, 0, 3)]);
  ImGui::SetNextItemWidth(w);
  changed |= ImGui::SliderInt("Volume", &ins.volume, 0, 15);
  ImGui::TextDisabled("Play it with the keyboard: Z S X D C... / Q 2 W 3 E...");

  bool usesTable = ins.wave == WAVE_TABLE;
  for (int v : ins.macros[MACRO_WAVE].values) usesTable |= v == WAVE_TABLE;
  if (usesTable) changed |= wavetableEditor(ins);

  ImGui::Separator();
  if (ImGui::BeginTabBar("macros")) {
    for (int t = 0; t < MACRO_COUNT; t++) {
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
  if (changed) dirty_ = true;
  ImGui::End();
}
