// Orders, song settings, oscilloscope and help windows.
#include <imgui.h>

#include <cctype>
#include <algorithm>
#include <cfloat>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <sstream>

#include "app.h"
#include "theme.h"

void App::ordersWindow() {
  if (!ImGui::Begin("Orders", &showOrders_)) {
    ImGui::End();
    return;
  }
  int nch = song_.channelCount();
  int numOrders = (int)song_.orders.size();
  ordCh_ = std::clamp(ordCh_, 0, nch - 1);
  bool changed = false;

  // Order list operations.
  if (ImGui::Button("+") && numOrders < MAX_ORDERS) {
    // New row using the first unused pattern of each channel.
    std::array<uint8_t, MAX_CHANNELS> row{};
    for (int c = 0; c < nch; c++) {
      int p = 0;
      while (p < MAX_PATTERNS - 1) {
        bool used = song_.pattern(c, p) != nullptr;
        for (auto& o : song_.orders) used |= o[c] == p;
        if (!used) break;
        p++;
      }
      row[c] = (uint8_t)p;
    }
    song_.orders.insert(song_.orders.begin() + curOrder_ + 1, row);
    curOrder_++;
    changed = true;
  }
  if (ImGui::IsItemHovered()) ImGui::SetTooltip("Add an order with new empty patterns");
  ImGui::SameLine();
  if (ImGui::Button("Dup") && numOrders < MAX_ORDERS) {
    song_.orders.insert(song_.orders.begin() + curOrder_ + 1, song_.orders[curOrder_]);
    curOrder_++;
    changed = true;
  }
  if (ImGui::IsItemHovered()) ImGui::SetTooltip("Duplicate this order (same patterns)");
  ImGui::SameLine();
  if (ImGui::Button("Clone") && numOrders < MAX_ORDERS) {
    pushFullUndo();
    // Copy the patterns into new unused slots.
    std::array<uint8_t, MAX_CHANNELS> row = song_.orders[curOrder_];
    for (int c = 0; c < nch; c++) {
      int p = 0;
      while (p < MAX_PATTERNS - 1) {
        bool used = song_.pattern(c, p) != nullptr;
        for (auto& o : song_.orders) used |= o[c] == p;
        if (!used) break;
        p++;
      }
      const Pattern* src = song_.pattern(c, row[c]);
      if (src) *song_.pattern(c, p, true) = *src;
      row[c] = (uint8_t)p;
    }
    song_.orders.insert(song_.orders.begin() + curOrder_ + 1, row);
    curOrder_++;
    changed = true;
  }
  if (ImGui::IsItemHovered()) ImGui::SetTooltip("Copy this order into new patterns");
  ImGui::SameLine();
  if (ImGui::Button("Del") && numOrders > 1) {
    song_.orders.erase(song_.orders.begin() + curOrder_);
    curOrder_ = std::min(curOrder_, (int)song_.orders.size() - 1);
    changed = true;
  }
  ImGui::SameLine();
  if (ImGui::ArrowButton("up", ImGuiDir_Up) && curOrder_ > 0) {
    std::swap(song_.orders[curOrder_], song_.orders[curOrder_ - 1]);
    curOrder_--;
    changed = true;
  }
  ImGui::SameLine();
  if (ImGui::ArrowButton("down", ImGuiDir_Down) && curOrder_ < (int)song_.orders.size() - 1) {
    std::swap(song_.orders[curOrder_], song_.orders[curOrder_ + 1]);
    curOrder_++;
    changed = true;
  }

  // Change the selected cell's pattern number.
  uint8_t& sel = song_.orders[curOrder_][ordCh_];
  ImGui::Text("Ch %d pattern:", ordCh_ + 1);
  ImGui::SameLine();
  if (ImGui::SmallButton("-##pat") && sel > 0) {
    sel--;
    changed = true;
  }
  ImGui::SameLine();
  ImGui::Text("%02X", sel);
  ImGui::SameLine();
  if (ImGui::SmallButton("+##pat") && sel < MAX_PATTERNS - 1) {
    sel++;
    changed = true;
  }
  ImGui::SameLine();
  ImGui::TextDisabled("(wheel on a cell)");

  int playingOrder = engine_->playing() ? engine_->order() : -1;
  if (ImGui::BeginTable("orders", nch + 1,
                        ImGuiTableFlags_ScrollY | ImGuiTableFlags_ScrollX | ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_SizingFixedFit)) {
    ImGui::TableSetupScrollFreeze(1, 1);
    ImGui::TableSetupColumn("#");
    for (int c = 0; c < nch; c++) {
      char buf[16];
      std::snprintf(buf, sizeof(buf), "%d", c + 1);
      ImGui::TableSetupColumn(buf);
    }
    ImGui::TableHeadersRow();
    for (int o = 0; o < (int)song_.orders.size(); o++) {
      ImGui::TableNextRow();
      if (o == playingOrder) ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg1, IM_COL32(40, 110, 50, 140));
      ImGui::TableNextColumn();
      ImGui::TextDisabled("%02X", o);
      for (int c = 0; c < nch; c++) {
        ImGui::TableNextColumn();
        char buf[32];
        std::snprintf(buf, sizeof(buf), "%02X##%d_%d", song_.orders[o][c], o, c);
        bool isSel = o == curOrder_ && c == ordCh_;
        if (ImGui::Selectable(buf, isSel || o == curOrder_)) {
          curOrder_ = o;
          ordCh_ = c;
          curCh_ = c;
          hasSel_ = false;
          if (engine_->playing() && follow_) {
            engine_->stop();
            engine_->play(o, 0);
          }
        }
        if (isSel) ImGui::GetWindowDrawList()->AddRect(ImGui::GetItemRectMin(), ImGui::GetItemRectMax(), IM_COL32(230, 230, 255, 200));
        if (ImGui::IsItemHovered() && ImGui::GetIO().MouseWheel != 0) {
          int v = song_.orders[o][c] + (ImGui::GetIO().MouseWheel > 0 ? 1 : -1);
          song_.orders[o][c] = (uint8_t)std::clamp(v, 0, MAX_PATTERNS - 1);
          changed = true;
        }
      }
    }
    ImGui::EndTable();
  }
  if (changed) {
    dirty_ = true;
    metaTouched_ = true;
  }
  ImGui::End();
}

void App::songWindow() {
  if (!ImGui::Begin("Song", &showSong_)) {
    ImGui::End();
    return;
  }
  bool changed = false;
  char buf[256];
  std::snprintf(buf, sizeof(buf), "%s", song_.name.c_str());
  if (ImGui::InputText("Name", buf, sizeof(buf))) {
    song_.name = buf;
    changed = true;
  }
  std::snprintf(buf, sizeof(buf), "%s", song_.author.c_str());
  if (ImGui::InputText("Author", buf, sizeof(buf))) {
    song_.author = buf;
    changed = true;
  }
  ImGui::SetNextItemWidth(120);
  if (ImGui::InputFloat("Tick rate (Hz)", &song_.tickRate, 1, 10, "%.2f")) {
    song_.tickRate = std::clamp(song_.tickRate, 1.0f, 1000.0f);
    changed = true;
  }
  // Speed: one value, or several cycled row by row ("6 5" = swing).
  {
    static char speedBuf[64];
    static bool editingSpeed = false;
    if (!editingSpeed) {
      std::string t;
      for (int v : song_.speeds) t += std::to_string(v) + " ";
      if (!t.empty()) t.pop_back();
      std::snprintf(speedBuf, sizeof(speedBuf), "%s", t.c_str());
    }
    ImGui::SetNextItemWidth(120);
    ImGui::InputText("Speed (ticks/row)", speedBuf, sizeof(speedBuf));
    editingSpeed = ImGui::IsItemActive();
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Ticks per row. Several values (e.g. \"6 5\") alternate row by row for swing.");
    if (ImGui::IsItemDeactivatedAfterEdit()) {
      std::vector<int> sp;
      std::istringstream ss(speedBuf);
      int v;
      while (ss >> v && (int)sp.size() < MAX_GROOVE) sp.push_back(std::clamp(v, 1, 255));
      if (!sp.empty()) {
        song_.speeds = sp;
        changed = true;
      }
    }
  }
  ImGui::SetNextItemWidth(120);
  if (ImGui::InputInt("Pattern length", &song_.patternLength)) {
    song_.patternLength = std::clamp(song_.patternLength, 1, MAX_ROWS);
    changed = true;
  }
  ImGui::SetNextItemWidth(120);
  if (ImGui::InputInt("Highlight 1", &song_.highlight1)) {
    song_.highlight1 = std::clamp(song_.highlight1, 0, MAX_ROWS);
    changed = true;
  }
  ImGui::SetNextItemWidth(120);
  if (ImGui::InputInt("Highlight 2", &song_.highlight2)) {
    song_.highlight2 = std::clamp(song_.highlight2, 0, MAX_ROWS);
    changed = true;
  }
  float avgSpeed = 0;
  for (int v : song_.speeds) avgSpeed += v;
  avgSpeed = song_.speeds.empty() ? 6 : avgSpeed / song_.speeds.size();
  float bpm = song_.tickRate * 60.0f / (std::max(avgSpeed, 1.0f) * std::max(song_.highlight1, 1));
  ImGui::TextDisabled("= %.1f BPM (one beat = Highlight 1 rows)", bpm);

  ImGui::Separator();
  int nch = song_.channelCount();
  ImGui::SetNextItemWidth(120);
  if (ImGui::InputInt("Channels", &nch) && nch != song_.channelCount()) {
    pushFullUndo();
    engine_->stop();
    song_.setChannelCount(nch);
    changed = true;
  }
  if (ImGui::BeginTable("chans", 4, ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_RowBg)) {
    ImGui::TableSetupColumn("#", ImGuiTableColumnFlags_WidthFixed);
    ImGui::TableSetupColumn("Name");
    ImGui::TableSetupColumn("FX cols", ImGuiTableColumnFlags_WidthFixed, 90);
    ImGui::TableSetupColumn("Mix", ImGuiTableColumnFlags_WidthFixed, 70);
    ImGui::TableHeadersRow();
    for (int c = 0; c < song_.channelCount(); c++) {
      ChannelInfo& info = song_.channels[c];
      ImGui::PushID(c);
      ImGui::TableNextRow();
      ImGui::TableNextColumn();
      ImGui::Text("%d", c + 1);
      ImGui::TableNextColumn();
      std::snprintf(buf, sizeof(buf), "%s", info.name.c_str());
      ImGui::SetNextItemWidth(-1);
      if (ImGui::InputText("##n", buf, sizeof(buf))) {
        info.name = buf;
        changed = true;
      }
      ImGui::TableNextColumn();
      ImGui::SetNextItemWidth(-1);
      if (ImGui::InputInt("##fx", &info.effectCols)) {
        info.effectCols = std::clamp(info.effectCols, 1, MAX_EFFECTS);
        changed = true;
      }
      ImGui::TableNextColumn();
      ImGui::SetNextItemWidth(-1);
      changed |= ImGui::SliderFloat("##mix", &info.mix, 0.0f, 2.0f, "%.2f");
      ImGui::PopID();
    }
    ImGui::EndTable();
  }
  if (!audioStatus_.empty()) ImGui::TextDisabled("%s", audioStatus_.c_str());
  if (changed) {
    dirty_ = true;
    metaTouched_ = true;
  }
  ImGui::End();
}

void App::scopeWindow() {
  if (!ImGui::Begin("Oscilloscope", &showScope_)) {
    ImGui::End();
    return;
  }
  ImDrawList* dl = ImGui::GetWindowDrawList();
  ImVec2 pos = ImGui::GetCursorScreenPos();
  ImVec2 avail = ImGui::GetContentRegionAvail();
  int nch = song_.channelCount();
  int boxes = nch + 1;
  // Grid layout: as many columns as fit at 90 px or more.
  int cols = std::clamp((int)(avail.x / 90.0f), 1, boxes);
  int gridRows = (boxes + cols - 1) / cols;
  float w = avail.x / cols, h = std::max(avail.y / gridRows, 16.0f);
  const int samples = 512;
  int start = (engine_->scopePos() - samples + SCOPE_LEN) % SCOPE_LEN;

  auto drawScope = [&](int i, const std::array<float, SCOPE_LEN>& data, ImU32 color, const char* label) {
    float x0 = pos.x + (i % cols) * w, y0 = pos.y + (i / cols) * h;
    ImVec2 a(x0 + 2, y0 + 1), b(x0 + w - 2, y0 + h - 1);
    dl->AddRectFilled(a, b, theme().scopeBg, 3);
    float mid = (a.y + b.y) / 2;
    ImVec2 pts[128];
    const int np = 128;
    for (int k = 0; k < np; k++) {
      float v = data[(start + k * samples / np) % SCOPE_LEN];
      pts[k] = ImVec2(a.x + (b.x - a.x) * k / (np - 1), mid - std::clamp(v, -1.0f, 1.0f) * ((b.y - a.y) / 2 - 1));
    }
    dl->PushClipRect(a, b, true);
    dl->AddPolyline(pts, np, color, 0, 1.5f);
    dl->AddText(ImVec2(a.x + 3, a.y + 1), IM_COL32(150, 150, 170, 255), label);
    dl->PopClipRect();
  };
  for (int c = 0; c < nch; c++)
    drawScope(c, engine_->channel(c).scope, song_.channels[c].muted ? theme().scopeMuted : theme().scope, song_.channels[c].name.c_str());
  drawScope(nch, engine_->masterScope(), theme().scopeMaster, "Master");
  ImGui::Dummy(avail);
  ImGui::End();
}

// Keygen-style intro screen: starfield, copper bars, logo and sine scroller.
void App::scrollerWindow() {
  if (!ImGui::Begin("Keygen", &showScroller_)) {
    ImGui::End();
    return;
  }
  ImDrawList* dl = ImGui::GetWindowDrawList();
  ImVec2 p0 = ImGui::GetCursorScreenPos();
  ImVec2 size = ImGui::GetContentRegionAvail();
  size.x = std::max(size.x, 50.0f);
  size.y = std::max(size.y, 50.0f);
  ImVec2 p1(p0.x + size.x, p0.y + size.y);
  float t = (float)ImGui::GetTime();
  float dt = std::min(ImGui::GetIO().DeltaTime, 0.1f);
  dl->PushClipRect(p0, p1, true);
  dl->AddRectFilled(p0, p1, IM_COL32(0, 0, 0, 255));

  // Music level drives the bars and the stars.
  float peak = 0;
  for (float v : engine_->masterScope()) peak = std::max(peak, std::abs(v));

  // Starfield.
  struct Star {
    float x, y, z;
  };
  static std::vector<Star> stars;
  if (stars.empty()) {
    for (int i = 0; i < 220; i++) stars.push_back({(std::rand() % 2000 - 1000) / 1000.0f, (std::rand() % 2000 - 1000) / 1000.0f, (std::rand() % 1000 + 1) / 1000.0f});
  }
  ImVec2 c((p0.x + p1.x) / 2, (p0.y + p1.y) / 2);
  for (Star& s : stars) {
    s.z -= dt * (0.25f + peak * 0.8f);
    if (s.z <= 0.02f) {
      s = {(std::rand() % 2000 - 1000) / 1000.0f, (std::rand() % 2000 - 1000) / 1000.0f, 1.0f};
    }
    float sx = c.x + s.x / s.z * size.x * 0.5f, sy = c.y + s.y / s.z * size.y * 0.5f;
    int b = (int)(255 * (1.0f - s.z));
    float r = 1.0f + (1.0f - s.z) * 1.5f;
    dl->AddRectFilled(ImVec2(sx - r / 2, sy - r / 2), ImVec2(sx + r / 2, sy + r / 2), IM_COL32(b, b, b, 255));
  }

  // Copper bars.
  static const ImU32 barCols[3][2] = {{IM_COL32(255, 0, 200, 0), IM_COL32(255, 0, 200, 200)},
                                      {IM_COL32(0, 255, 240, 0), IM_COL32(0, 255, 240, 200)},
                                      {IM_COL32(120, 255, 60, 0), IM_COL32(120, 255, 60, 200)}};
  for (int i = 0; i < 3; i++) {
    float y = c.y + std::sin(t * 1.3f + i * 2.1f) * size.y * 0.3f;
    float hh = 6 + peak * 14;
    dl->AddRectFilledMultiColor(ImVec2(p0.x, y - hh), ImVec2(p1.x, y), barCols[i][0], barCols[i][0], barCols[i][1], barCols[i][1]);
    dl->AddRectFilledMultiColor(ImVec2(p0.x, y), ImVec2(p1.x, y + hh), barCols[i][1], barCols[i][1], barCols[i][0], barCols[i][0]);
  }

  ImFont* font = ImGui::GetFont();
  auto hue = [](float h, int alpha) {
    float r, g, b;
    ImGui::ColorConvertHSVtoRGB(h - std::floor(h), 0.9f, 1.0f, r, g, b);
    return IM_COL32((int)(r * 255), (int)(g * 255), (int)(b * 255), alpha);
  };

  // Logo.
  const char* logo = "DATRACKARXD";
  float logoSize = std::clamp(size.y * 0.22f, 16.0f, 64.0f);
  float lw = font->CalcTextSizeA(logoSize, FLT_MAX, 0, logo).x;
  float lx = c.x - lw / 2, ly = p0.y + size.y * 0.12f;
  for (const char* ch = logo; *ch; ch++) {
    int i = (int)(ch - logo);
    float oy = std::sin(t * 4 + i * 0.6f) * logoSize * 0.12f;
    dl->AddText(font, logoSize, ImVec2(lx + 3, ly + oy + 3), IM_COL32(0, 0, 0, 200), ch, ch + 1);
    dl->AddText(font, logoSize, ImVec2(lx, ly + oy), hue(t * 0.2f + i * 0.07f, 255), ch, ch + 1);
    lx += font->CalcTextSizeA(logoSize, FLT_MAX, 0, ch, ch + 1).x;
  }

  // Sine scroller.
  std::string text = "      *** DATRACKARXD ***   NOW PLAYING: " + song_.name;
  if (!song_.author.empty()) text += "  BY " + song_.author;
  text += "   ***   " + std::to_string(song_.channelCount()) + " CHANNELS OF PURE CHIP POWER   ***   " +
          std::to_string(song_.instruments.size()) + " INSTRUMENTS   ***   PRESS ENTER TO PLAY   ***   "
          "GREETINGS TO ALL TRACKER MUSICIANS, DEMOSCENERS AND FURNACE USERS   ***   ";
  for (char& ch : text) ch = (char)std::toupper((unsigned char)ch);
  float fs = std::clamp(size.y * 0.16f, 14.0f, 48.0f);
  float charW = font->CalcTextSizeA(fs, FLT_MAX, 0, "W").x;
  float total = text.size() * charW;
  float offset = std::fmod(t * 140.0f, total);
  float baseY = p0.y + size.y * 0.72f;
  for (size_t i = 0; i < text.size() * 2; i++) {
    float x = p0.x + i * charW - offset;
    if (x < p0.x - charW) continue;
    if (x > p1.x) break;
    const char* ch = &text[i % text.size()];
    float y = baseY + std::sin(t * 3.0f + x * 0.015f) * size.y * 0.1f;
    dl->AddText(font, fs, ImVec2(x, y), hue(x * 0.002f + t * 0.3f, 255), ch, ch + 1);
  }
  dl->PopClipRect();
  ImGui::Dummy(size);
  ImGui::End();
}

void App::effectsHelp() {
  if (!ImGui::Begin("Effects", &showEffects_)) {
    ImGui::End();
    return;
  }
  static const char* rows[][2] = {
      {"00xy", "Arpeggio: note, +x, +y semitones, cycling every tick"},
      {"01xx", "Pitch slide up (xx/32 semitone per tick)"},
      {"02xx", "Pitch slide down"},
      {"03xx", "Portamento to the note in this row (speed xx)"},
      {"04xy", "Vibrato: speed x, depth y"},
      {"05xy", "Volume slide (like 0A) while the vibrato continues"},
      {"06xy", "Volume slide (like 0A) while the portamento continues"},
      {"07xy", "Tremolo: speed x, depth y"},
      {"08xy", "Panning: left volume x, right volume y (0-F)"},
      {"0Axy", "Volume slide: up x or down y per tick"},
      {"0Bxx", "Jump to order xx"},
      {"0Cxx", "Retrigger the note every xx ticks"},
      {"0Dxx", "Go to row xx of the next order"},
      {"0Fxx", "Set speed (ticks per row)"},
      {"10xx", "Set waveform (0 pulse..5 wavetable; wavetable index on wavetable channels)"},
      {"12xx", "Set duty (0-3). Noise: 1 = metallic, 3 = periodic"},
      {"80xx", "Panning: 00 left, 80 center, FF right"},
      {"90xx-92xx", "Sample offset (byte 0, 1, 2 of the position)"},
      {"C0xx-C3xx", "Set tick rate in Hz (C0-C3 hold the high bits)"},
      {"E1xy / E2xy", "Slide up / down y semitones at speed x"},
      {"E6xx", "Pattern loop: E600 marks the start, E6xx repeats xx times"},
      {"ECxx", "Cut the note after xx ticks"},
      {"EDxx", "Delay the row by xx ticks"},
      {"EExx", "Pattern delay: repeat the row xx times"},
      {"F0xx", "Set tempo in BPM (tick rate = xx * 2 / 5)"},
      {"F1xx / F2xx", "Fine pitch slide up / down, once"},
      {"F3xx / F4xx", "Fine volume up / down by xx, once"},
  };
  if (ImGui::BeginTable("fx", 2, ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp)) {
    for (auto& r : rows) {
      ImGui::TableNextRow();
      ImGui::TableNextColumn();
      ImGui::TextColored(ImVec4(1, 0.65f, 0.3f, 1), "%s", r[0]);
      ImGui::TableNextColumn();
      ImGui::TextWrapped("%s", r[1]);
    }
    ImGui::EndTable();
  }
  ImGui::Separator();
  ImGui::TextWrapped("Volume column: 00-7F. Effects with value 00 stop the effect (arpeggio, slides, vibrato).");
  ImGui::End();
}

void App::keysHelp() {
  if (!ImGui::Begin("Keyboard", &showKeys_)) {
    ImGui::End();
    return;
  }
  static const char* rows[][2] = {
      {"Z S X D C V G B H N J M", "Notes (bottom row, physical keys)"},
      {"Q 2 W 3 E R 5 T 6 Y 7 U I", "Notes, octave + 1"},
      {"1 or `", "Note off"},
      {"0-9 A-F", "Hex values (instrument, volume, effects)"},
      {"Space", "Toggle record (edit) mode"},
      {"Enter / Shift+Enter", "Play/stop from the order start / from the cursor"},
      {"F5 / F6 / F8", "Play song / play from cursor / stop"},
      {"Arrows, PgUp/PgDn, Home/End", "Move"},
      {"Tab / Shift+Tab", "Next / previous channel"},
      {"Shift + move, mouse drag", "Select"},
      {"Ctrl+C / X / V", "Copy / cut / paste"},
      {"Ctrl+A", "Select channel, again for whole pattern"},
      {"Ctrl+Z / Ctrl+Y", "Undo / redo"},
      {"Ctrl+Up/Down (+Shift)", "Transpose 1 (12) semitones"},
      {"Delete", "Clear"},
      {"Insert / Backspace", "Insert row / delete row"},
      {"Numpad / *", "Octave down / up"},
      {"Ctrl+N / O / S", "New / open or import / save"},
      {"Click channel name", "Mute (right-click: solo)"},
      {"Piano roll: click / drag", "Add a note / move it, drag its right edge to resize"},
      {"Piano roll: right-click", "Delete notes (drag to erase several)"},
      {"Piano roll: Ctrl+drag", "Select a box (Shift adds), Ctrl+A selects all"},
      {"Piano roll: arrows", "Move selection (Up/Down: semitone, Shift: octave)"},
      {"Piano roll: wheel", "Scroll; Shift: horizontal, Ctrl: zoom time, Alt: zoom pitch"},
  };
  if (ImGui::BeginTable("keys", 2, ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp)) {
    for (auto& r : rows) {
      ImGui::TableNextRow();
      ImGui::TableNextColumn();
      ImGui::TextColored(ImVec4(0.5f, 0.8f, 1, 1), "%s", r[0]);
      ImGui::TableNextColumn();
      ImGui::TextWrapped("%s", r[1]);
    }
    ImGui::EndTable();
  }
  ImGui::End();
}

void App::aboutWindow() {
  ImGui::SetNextWindowSize(ImVec2(380, 0), ImGuiCond_Appearing);
  if (ImGui::Begin("About", &showAbout_, ImGuiWindowFlags_NoDocking)) {
    ImGui::Text("DATRACKARXD %s", DATRACKARXD_VERSION);
    ImGui::TextWrapped("A small tracker in the spirit of Furnace: chip waves (pulse, triangle, saw, noise, sine, "
                       "wavetable), 4-operator FM and samples, with Furnace-style macros. Imports FUR, MOD, XM, IT and S3M.");
  }
  ImGui::End();
}
