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
#include "i18n.h"
#include "player.h"
#include "theme.h"

void App::ordersWindow() {
  if (!ImGui::Begin(TW("Orders").c_str(), &showOrders_)) {
    ImGui::End();
    return;
  }
  int nch = song_.channelCount();
  int numOrders = (int)song_.orders.size();
  ordCh_ = std::clamp(ordCh_, 0, nch - 1);
  bool changed = false;

  // Order list operations.
  if (ImGui::Button("+") && numOrders < MAX_ORDERS) orderInsertNew(curOrder_ + 1);
  if (ImGui::IsItemHovered()) ImGui::SetTooltip(T("Add an order with new empty patterns"));
  ImGui::SameLine();
  if (ImGui::Button(T("Dup")) && numOrders < MAX_ORDERS) orderDuplicate(curOrder_, curOrder_);
  if (ImGui::IsItemHovered()) ImGui::SetTooltip(T("Duplicate this order (same patterns)"));
  ImGui::SameLine();
  if (ImGui::Button(T("Clone")) && numOrders < MAX_ORDERS) orderClone(curOrder_);
  if (ImGui::IsItemHovered()) ImGui::SetTooltip(T("Copy this order into new patterns"));
  ImGui::SameLine();
  if (ImGui::Button(T("Del")) && numOrders > 1) orderDelete(curOrder_, curOrder_);
  ImGui::SameLine();
  if (ImGui::ArrowButton("up", ImGuiDir_Up) && curOrder_ > 0) {
    orderSwap(curOrder_, curOrder_ - 1);
    curOrder_--;
  }
  ImGui::SameLine();
  if (ImGui::ArrowButton("down", ImGuiDir_Down) && curOrder_ < (int)song_.orders.size() - 1) {
    orderSwap(curOrder_, curOrder_ + 1);
    curOrder_++;
  }

  // Change the selected cell's pattern number.
  uint8_t& sel = song_.orders[curOrder_][ordCh_];
  ImGui::Text(T("Ch %d pattern:"), ordCh_ + 1);
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
  ImGui::TextDisabled(T("(wheel on a cell)"));

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
  if (!ImGui::Begin(TW("Song").c_str(), &showSong_)) {
    ImGui::End();
    return;
  }
  bool changed = false;
  char buf[256];
  std::snprintf(buf, sizeof(buf), "%s", song_.name.c_str());
  if (ImGui::InputText(T("Name"), buf, sizeof(buf))) {
    song_.name = buf;
    changed = true;
  }
  std::snprintf(buf, sizeof(buf), "%s", song_.author.c_str());
  if (ImGui::InputText(T("Author"), buf, sizeof(buf))) {
    song_.author = buf;
    changed = true;
  }
  ImGui::SetNextItemWidth(S(120));
  if (ImGui::InputFloat(T("Tick rate (Hz)"), &song_.tickRate, 1, 10, "%.2f")) {
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
    ImGui::SetNextItemWidth(S(120));
    ImGui::InputText(T("Speed (ticks/row)"), speedBuf, sizeof(speedBuf));
    editingSpeed = ImGui::IsItemActive();
    if (ImGui::IsItemHovered()) ImGui::SetTooltip(T("Ticks per row. Several values (e.g. \"6 5\") alternate row by row for swing."));
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
    if (ImGui::SmallButton(T("Swing..."))) ImGui::OpenPopup(T("swing"));
    if (ImGui::BeginPopup(T("swing"))) {
      // Swing: long row then short row, the same tempo on average.
      int total = 0;
      for (int v : song_.speeds) total += v;
      int base = std::max(1, (int)std::lround(total / (float)std::max<size_t>(1, song_.speeds.size())));
      int amount = song_.speeds.size() == 2 ? std::abs(song_.speeds[0] - song_.speeds[1]) / 2 : 0;
      ImGui::SetNextItemWidth(S(140));
      bool c1 = ImGui::SliderInt(T("Speed"), &base, 1, 32);
      ImGui::SetNextItemWidth(S(140));
      bool c2 = ImGui::SliderInt(T("Swing"), &amount, 0, std::max(0, base - 1));
      if (c1 || c2) {
        amount = std::min(amount, base - 1);
        song_.speeds = amount ? std::vector<int>{base + amount, base - amount} : std::vector<int>{base};
        changed = true;
      }
      ImGui::TextDisabled(T("Rows alternate %d and %d ticks."), base + amount, base - amount);
      ImGui::EndPopup();
    }
  }
  ImGui::SetNextItemWidth(S(120));
  if (ImGui::InputInt(T("Pattern length"), &song_.patternLength)) {
    song_.patternLength = std::clamp(song_.patternLength, 1, MAX_ROWS);
    changed = true;
  }
  ImGui::SetNextItemWidth(S(120));
  if (ImGui::InputInt(T("Highlight 1"), &song_.highlight1)) {
    song_.highlight1 = std::clamp(song_.highlight1, 0, MAX_ROWS);
    changed = true;
  }
  ImGui::SetNextItemWidth(S(120));
  if (ImGui::InputInt(T("Highlight 2"), &song_.highlight2)) {
    song_.highlight2 = std::clamp(song_.highlight2, 0, MAX_ROWS);
    changed = true;
  }
  float avgSpeed = 0;
  for (int v : song_.speeds) avgSpeed += v;
  avgSpeed = song_.speeds.empty() ? 6 : avgSpeed / song_.speeds.size();
  float bpm = song_.tickRate * 60.0f / (std::max(avgSpeed, 1.0f) * std::max(song_.highlight1, 1));
  ImGui::TextDisabled(T("= %.1f BPM (one beat = Highlight 1 rows)"), bpm);

  ImGui::Separator();
  int nch = song_.channelCount();
  ImGui::SetNextItemWidth(S(120));
  if (ImGui::InputInt(T("Channels"), &nch) && nch != song_.channelCount()) {
    pushFullUndo();
    engine_->stop();
    song_.setChannelCount(nch);
    changed = true;
  }
  if (ImGui::BeginTable("chans", 4, ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_RowBg)) {
    ImGui::TableSetupColumn("#", ImGuiTableColumnFlags_WidthFixed);
    ImGui::TableSetupColumn(T("Name"));
    ImGui::TableSetupColumn(T("FX cols"), ImGuiTableColumnFlags_WidthFixed, 90);
    ImGui::TableSetupColumn(T("Mix"), ImGuiTableColumnFlags_WidthFixed, 70);
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

// Master effects and the per-channel levels and sends.
void App::mixerWindow() {
  if (!ImGui::Begin(TW("Mixer").c_str(), &showMixer_)) {
    ImGui::End();
    return;
  }
  bool changed = false;
  MasterFx& fx = song_.fx;
  const float w = S(150);
  if (ImGui::BeginTable("master", 3, ImGuiTableFlags_SizingStretchSame | ImGuiTableFlags_BordersInnerV)) {
    ImGui::TableNextRow();
    ImGui::TableNextColumn();
    changed |= ImGui::Checkbox(T("Reverb"), &fx.reverb);
    ImGui::BeginDisabled(!fx.reverb);
    ImGui::SetNextItemWidth(w);
    changed |= ImGui::SliderFloat(T("Room size"), &fx.roomSize, 0, 1, "%.2f");
    ImGui::SetNextItemWidth(w);
    changed |= ImGui::SliderFloat(T("Damping"), &fx.damping, 0, 1, "%.2f");
    ImGui::SetNextItemWidth(w);
    changed |= ImGui::SliderFloat(T("Level##rv"), &fx.reverbLevel, 0, 2, "%.2f");
    ImGui::EndDisabled();

    ImGui::TableNextColumn();
    changed |= ImGui::Checkbox(T("Delay"), &fx.delay);
    ImGui::SameLine();
    changed |= ImGui::Checkbox(T("Ping-pong"), &fx.pingPong);
    ImGui::BeginDisabled(!fx.delay);
    ImGui::SetNextItemWidth(w);
    changed |= ImGui::SliderInt(T("Time (rows)"), &fx.delayRows, 1, 16);
    ImGui::SetNextItemWidth(w);
    changed |= ImGui::SliderFloat(T("Feedback"), &fx.delayFeedback, 0, 0.95f, "%.2f");
    ImGui::SetNextItemWidth(w);
    changed |= ImGui::SliderFloat(T("Level##dl"), &fx.delayLevel, 0, 2, "%.2f");
    ImGui::EndDisabled();

    ImGui::TableNextColumn();
    changed |= ImGui::Checkbox(T("Filter"), &fx.filter);
    ImGui::BeginDisabled(!fx.filter);
    ImGui::SetNextItemWidth(w);
    changed |= ImGui::Combo(T("Type"), &fx.filterType, TC("Low-pass\0Band-pass\0High-pass\0"));
    ImGui::SetNextItemWidth(w);
    changed |= ImGui::SliderFloat(T("Cutoff"), &fx.cutoff, 20, 20000, "%.0f Hz", ImGuiSliderFlags_Logarithmic);
    ImGui::SetNextItemWidth(w);
    changed |= ImGui::SliderFloat(T("Resonance"), &fx.resonance, 0, 1, "%.2f");
    ImGui::EndDisabled();
    ImGui::SetNextItemWidth(w);
    changed |= ImGui::SliderFloat(T("Master"), &fx.volume, 0, 2, "%.2f");
    ImGui::EndTable();
  }

  ImGui::Separator();
  if (ImGui::BeginTable("sends", 7, ImGuiTableFlags_SizingStretchSame | ImGuiTableFlags_RowBg)) {
    ImGui::TableSetupColumn(T("Channel"));
    ImGui::TableSetupColumn(T("Mix"));
    ImGui::TableSetupColumn(T("Pan"));
    ImGui::TableSetupColumn(T("Reverb send"));
    ImGui::TableSetupColumn(T("Delay send"));
    ImGui::TableSetupColumn(T("Insert FX"), ImGuiTableColumnFlags_WidthFixed);
    ImGui::TableSetupColumn(T("Mute"), ImGuiTableColumnFlags_WidthFixed);
    ImGui::TableHeadersRow();
    for (int c = 0; c < song_.channelCount(); c++) {
      ChannelInfo& info = song_.channels[c];
      ImGui::PushID(c);
      ImGui::TableNextRow();
      ImGui::TableNextColumn();
      ImGui::Text("%d %s", c + 1, info.name.c_str());
      ImGui::TableNextColumn();
      ImGui::SetNextItemWidth(-1);
      changed |= ImGui::SliderFloat("##mix", &info.mix, 0.0f, 2.0f, "%.2f");
      ImGui::TableNextColumn();
      ImGui::SetNextItemWidth(-1);
      if (ImGui::SliderInt("##pan", &info.pan, 0, 255, info.pan == 128 ? "C" : info.pan < 128 ? "L%d" : "R%d")) changed = true;
      if (ImGui::IsItemActive()) engine_->resetPan(c);  // hear it right away
      ImGui::TableNextColumn();
      ImGui::SetNextItemWidth(-1);
      changed |= ImGui::SliderFloat("##rv", &info.reverbSend, 0, 1, "%.2f");
      ImGui::TableNextColumn();
      ImGui::SetNextItemWidth(-1);
      changed |= ImGui::SliderFloat("##dl", &info.delaySend, 0, 1, "%.2f");
      ImGui::TableNextColumn();
      char fxLabel[32];
      int active = 0;
      for (const ChannelEffect& e : info.effects) active += e.on;
      std::snprintf(fxLabel, sizeof(fxLabel), info.effects.empty() ? "FX" : "FX %d", active);
      if (ImGui::SmallButton(fxLabel)) {
        fxCh_ = c;
        showChanFx_ = true;
        ImGui::SetWindowFocus("###Channel FX");
      }
      if (ImGui::IsItemHovered() && !info.effects.empty()) {
        std::string list;
        for (const ChannelEffect& e : info.effects)
          list += std::string(CHAN_FX_INFO[e.type].name) + (e.on ? "\n" : " (off)\n");
        ImGui::SetTooltip("%s", list.c_str());
      }
      ImGui::TableNextColumn();
      ImGui::Checkbox("##m", &info.muted);  // like the pattern header: not an edit
      ImGui::PopID();
    }
    ImGui::EndTable();
  }
  if (changed) {
    dirty_ = true;
    metaTouched_ = true;
  }
  ImGui::End();
}

// The insert effects of one channel, in processing order.
void App::channelFxWindow() {
  if (!ImGui::Begin(TW("Channel FX").c_str(), &showChanFx_)) {
    ImGui::End();
    return;
  }
  int nch = song_.channelCount();
  fxCh_ = std::clamp(fxCh_, 0, std::max(0, nch - 1));
  bool changed = false;
  char label[96];
  std::snprintf(label, sizeof(label), "%d %s", fxCh_ + 1, nch ? song_.channels[fxCh_].name.c_str() : "");
  ImGui::SetNextItemWidth(S(220));
  if (ImGui::BeginCombo(T("Channel"), label)) {
    for (int c = 0; c < nch; c++) {
      std::snprintf(label, sizeof(label), "%d %s (%zu)", c + 1, song_.channels[c].name.c_str(), song_.channels[c].effects.size());
      if (ImGui::Selectable(label, c == fxCh_)) fxCh_ = c;
    }
    ImGui::EndCombo();
  }
  ImGui::SameLine();
  if (ImGui::SmallButton(T("Current"))) fxCh_ = std::clamp(curCh_, 0, std::max(0, nch - 1));
  if (ImGui::IsItemHovered()) ImGui::SetTooltip(T("Show the channel the cursor is on"));
  if (nch == 0) {
    ImGui::End();
    return;
  }
  std::vector<ChannelEffect>& fx = song_.channels[fxCh_].effects;
  ImGui::SameLine();
  ImGui::BeginDisabled((int)fx.size() >= MAX_CHANNEL_FX);
  ImGui::SetNextItemWidth(S(150));
  if (ImGui::BeginCombo("##add", "Add effect...")) {
    for (int t = 0; t < CFX_COUNT; t++)
      if (ImGui::Selectable(CHAN_FX_INFO[t].name)) {
        fx.push_back(ChannelEffect::make(t));
        changed = true;
      }
    ImGui::EndCombo();
  }
  ImGui::EndDisabled();
  if (fx.empty()) ImGui::TextDisabled(T("No effect on this channel. The chain runs top to bottom, before the mixer."));

  int moveFrom = -1, moveTo = -1, remove = -1;
  const float w = S(170);
  if (ImGui::BeginTable("chain", std::max(1, (int)fx.size()), ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_ScrollX)) {
    for (int k = 0; k < (int)fx.size(); k++) ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed, w + 90);
    ImGui::TableNextRow();
    for (int k = 0; k < (int)fx.size(); k++) {
      ChannelEffect& e = fx[k];
      const ChanFxInfo& info = CHAN_FX_INFO[e.type];
      ImGui::TableNextColumn();
      ImGui::PushID(k);
      changed |= ImGui::Checkbox(info.name, &e.on);
      ImGui::SameLine();
      if (ImGui::ArrowButton("##l", ImGuiDir_Left) && k > 0) {
        moveFrom = k;
        moveTo = k - 1;
      }
      ImGui::SameLine();
      if (ImGui::ArrowButton("##r", ImGuiDir_Right) && k + 1 < (int)fx.size()) {
        moveFrom = k;
        moveTo = k + 1;
      }
      ImGui::SameLine();
      if (ImGui::SmallButton("x")) remove = k;
      ImGui::BeginDisabled(!e.on);
      for (int i = 0; i < info.count; i++) {
        const ChanFxParam& prm = info.params[i];
        ImGui::SetNextItemWidth(w);
        if (prm.choices) {
          int v = (int)std::lround(e.p[i]);
          if (ImGui::Combo(prm.name, &v, prm.choices)) {
            e.p[i] = (float)v;
            changed = true;
          }
        } else {
          changed |= ImGui::SliderFloat(prm.name, &e.p[i], prm.min, prm.max, prm.format,
                                        prm.log ? ImGuiSliderFlags_Logarithmic : 0);
          if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
            e.p[i] = prm.def;  // double-click: back to the default
            changed = true;
          }
        }
      }
      ImGui::EndDisabled();
      ImGui::PopID();
    }
    ImGui::EndTable();
  }
  if (moveFrom >= 0) {
    std::swap(fx[moveFrom], fx[moveTo]);
    changed = true;
  }
  if (remove >= 0) {
    fx.erase(fx.begin() + remove);
    changed = true;
  }
  if (changed) {
    dirty_ = true;
    metaTouched_ = true;
  }
  ImGui::End();
}

void App::scopeWindow() {
  if (!ImGui::Begin(TW("Oscilloscope").c_str(), &showScope_)) {
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

// Keygen-style intro screen (the same scene as the exported players).
void App::scrollerWindow() {
  if (!ImGui::Begin(TW("Keygen").c_str(), &showScroller_)) {
    ImGui::End();
    return;
  }
  ImVec2 p0 = ImGui::GetCursorScreenPos();
  ImVec2 size = ImGui::GetContentRegionAvail();
  size.x = std::max(size.x, 50.0f);
  size.y = std::max(size.y, 50.0f);
  float peak = 0;
  for (float v : engine_->masterScope()) peak = std::max(peak, std::abs(v));
  std::string text = "      *** DATRACKARXD ***   NOW PLAYING: " + song_.name;
  if (!song_.author.empty()) text += "  BY " + song_.author;
  text += "   ***   " + std::to_string(song_.channelCount()) + " CHANNELS OF PURE CHIP POWER   ***   " +
          std::to_string(song_.instruments.size()) + " INSTRUMENTS   ***   PRESS ENTER TO PLAY   ***   "
          "GREETINGS TO ALL TRACKER MUSICIANS, DEMOSCENERS AND FURNACE USERS   ***   ";
  keygen_.scroll = text;
  channelLevels(*engine_, song_.channelCount(), keygen_.levels);
  keygen_.draw(ImGui::GetWindowDrawList(), p0, size, (float)ImGui::GetTime(), ImGui::GetIO().DeltaTime, peak);
  ImGui::Dummy(size);
  ImGui::End();
}

void App::effectsHelp() {
  if (!ImGui::Begin(TW("Effects").c_str(), &showEffects_)) {
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
      {"10xx", "Set waveform (0 pulse..5 wavetable; wavetable index on wavetable channels). AY: mixer (1 tone, 2 noise, 4 envelope). OPL: waveform 0-7"},
      {"12xx", "Set duty (0-3), or the pulse width (01-FF) with fine pulse width. Noise: 1 = metallic, 3 = periodic. AY: noise period. SN / Game Boy noise: mode"},
      {"13xx", "Set the instrument filter cutoff (00-FF)"},
      {"80xx", "Panning: 00 left, 80 center, FF right"},
      {"90xx-92xx", "Sample offset (byte 0, 1, 2 of the position)"},
      {"C0xx-C3xx", "Set tick rate in Hz (C0-C3 hold the high bits)"},
      {"E1xy / E2xy", "Slide up / down y semitones at speed x"},
      {"E6xx", "Pattern loop: E600 marks the start, E6xx repeats xx times"},
      {"E5xx", "Fine pitch: 80 = none, 00-FF = -1 to +1 semitone"},
      {"E8xx", "Pitch bend: (xx - 80) / 16 semitones (00 = -8, 80 = none, FF = +7.9), glides over the row and stays"},
      {"E9xx", "Wide pitch bend: (xx - 80) / 4 semitones (00 = -32, 80 = none, FF = +31.75)"},
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
  ImGui::TextWrapped(T("Volume column: 00-7F. Effects with value 00 stop the effect (arpeggio, slides, vibrato)."));
  ImGui::End();
}

void App::keysHelp() {
  if (!ImGui::Begin(TW("Keyboard").c_str(), &showKeys_)) {
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
      {"Ctrl+I", "Interpolate volumes and effect values in the selection"},
      {"Ctrl+H", "Find & replace"},
      {"Right-click in the pattern", "Selection tools: interpolate, scale, humanize, reverse, expand, shrink"},
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
  ImGui::SetNextWindowSize(ImVec2(S(380), 0), ImGuiCond_Appearing);
  if (ImGui::Begin(TW("About").c_str(), &showAbout_, ImGuiWindowFlags_NoDocking)) {
    ImGui::Text(T("DATRACKARXD %s"), DATRACKARXD_VERSION);
    ImGui::TextWrapped(T("A small tracker in the spirit of Furnace: chip waves (pulse, triangle, saw, noise, sine, "
                       "wavetable), 4-operator FM, SID-style filter, ring mod and sync, and samples, with Furnace-style "
                       "macros, master effects and MIDI input. Imports FUR, DMF, FTM, MOD, XM, IT, S3M and MIDI; exports "
                       "WAV, OGG, MIDI and standalone keygen players."));
  }
  ImGui::End();
}
