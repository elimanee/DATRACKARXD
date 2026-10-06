// Orders, song settings, oscilloscope and help windows.
#include <imgui.h>

#include <algorithm>
#include <cstdio>

#include "app.h"

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
  if (changed) dirty_ = true;
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
  ImGui::SetNextItemWidth(120);
  if (ImGui::InputInt("Speed (ticks/row)", &song_.speed)) {
    song_.speed = std::clamp(song_.speed, 1, 255);
    changed = true;
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
  float bpm = song_.tickRate * 60.0f / (std::max(song_.speed, 1) * std::max(song_.highlight1, 1));
  ImGui::TextDisabled("= %.1f BPM (one beat = Highlight 1 rows)", bpm);

  ImGui::Separator();
  int nch = song_.channelCount();
  ImGui::SetNextItemWidth(120);
  if (ImGui::InputInt("Channels", &nch)) {
    engine_->stop();
    song_.setChannelCount(nch);
    undo_.clear();
    redo_.clear();
    changed = true;
  }
  if (ImGui::BeginTable("chans", 3, ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_RowBg)) {
    ImGui::TableSetupColumn("#", ImGuiTableColumnFlags_WidthFixed);
    ImGui::TableSetupColumn("Name");
    ImGui::TableSetupColumn("FX cols", ImGuiTableColumnFlags_WidthFixed, 90);
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
      ImGui::PopID();
    }
    ImGui::EndTable();
  }
  if (!audioStatus_.empty()) ImGui::TextDisabled("%s", audioStatus_.c_str());
  if (changed) dirty_ = true;
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
  float w = avail.x / boxes, h = std::max(avail.y, 20.0f);
  const int samples = 512;
  int start = (engine_->scopePos() - samples + SCOPE_LEN) % SCOPE_LEN;

  auto drawScope = [&](int i, const std::array<float, SCOPE_LEN>& data, ImU32 color, const char* label, float scale) {
    ImVec2 a(pos.x + i * w + 2, pos.y), b(pos.x + (i + 1) * w - 2, pos.y + h);
    dl->AddRectFilled(a, b, IM_COL32(15, 15, 22, 255), 3);
    float mid = (a.y + b.y) / 2;
    ImVec2 pts[128];
    int np = 128;
    for (int k = 0; k < np; k++) {
      float v = data[(start + k * samples / np) % SCOPE_LEN] * scale;
      pts[k] = ImVec2(a.x + (b.x - a.x) * k / (np - 1), mid - std::clamp(v, -1.0f, 1.0f) * (h / 2 - 2));
    }
    dl->AddPolyline(pts, np, color, 0, 1.5f);
    dl->AddText(ImVec2(a.x + 3, a.y + 1), IM_COL32(150, 150, 170, 255), label);
  };
  for (int c = 0; c < nch; c++) {
    bool muted = song_.channels[c].muted;
    drawScope(c, engine_->channel(c).scope, muted ? IM_COL32(110, 60, 60, 255) : IM_COL32(110, 230, 140, 255),
              song_.channels[c].name.c_str(), 1.0f);
  }
  drawScope(nch, engine_->masterScope(), IM_COL32(120, 190, 255, 255), "Master", 1.0f);
  ImGui::Dummy(avail);
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
      {"08xy", "Panning: left volume x, right volume y (0-F)"},
      {"0Axy", "Volume slide: up x or down y per tick"},
      {"0Bxx", "Jump to order xx"},
      {"0Dxx", "Go to row xx of the next order"},
      {"0Fxx", "Set speed (ticks per row)"},
      {"10xx", "Set waveform (0 pulse..5 wavetable)"},
      {"12xx", "Set duty (0-3)"},
      {"ECxx", "Cut the note after xx ticks"},
      {"EDxx", "Delay the row by xx ticks"},
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
      {"Space", "Toggle edit mode"},
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
      {"Ctrl+N / O / S", "New / open / save"},
      {"Click channel name", "Mute (right-click: solo)"},
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
    ImGui::Text("DATRACKARXD");
    ImGui::TextWrapped("A small chiptune tracker in the spirit of Furnace. Pulse, triangle, saw, noise, sine and wavetable "
                       "channels with Furnace-style macros.");
  }
  ImGui::End();
}
