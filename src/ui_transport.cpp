// The transport bar under the menus (play, stop, record, loop, position,
// tempo, master level) and the status bar at the bottom of the window.
#include <imgui.h>
#include <imgui_internal.h>

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <cstdio>
#include <filesystem>

#include "app.h"
#include "i18n.h"
#include "icons.h"
#include "theme.h"
#include "ui_fonts.h"
#include "ui_widgets.h"

namespace fs = std::filesystem;

namespace {

ImU32 withAlpha(ImVec4 c, float a) { return ImGui::ColorConvertFloat4ToU32(ImVec4(c.x, c.y, c.z, a)); }

// One "LCD" cell: a small caption over a big value.
float lcdCell(ImDrawList* dl, ImVec2 pos, float h, const char* caption, const char* value, ImU32 valueColor) {
  const UiFonts& F = uiFonts();
  float pad = S(10);
  float capSize = F.caption->FontSize;
  ImVec2 capW = F.caption->CalcTextSizeA(capSize, FLT_MAX, 0, caption);
  ImVec2 valW = F.big->CalcTextSizeA(F.big->FontSize, FLT_MAX, 0, value);
  float w = std::max(capW.x, valW.x) + pad * 2;
  float top = pos.y + (h - capSize - F.big->FontSize) * 0.5f;
  dl->AddText(F.caption, capSize, ImVec2(pos.x + pad, top), ImGui::GetColorU32(ImGuiCol_TextDisabled), caption);
  dl->AddText(F.big, F.big->FontSize, ImVec2(pos.x + pad, top + capSize), valueColor, value);
  return w;
}

}  // namespace

void App::transportBar() {
  const UiFonts& F = uiFonts();
  ImGuiIO& io = ImGui::GetIO();
  ImGuiViewport* vp = ImGui::GetMainViewport();
  float h = std::round(S(10) + std::max(F.big->FontSize + S(14), F.caption->FontSize + F.big->FontSize + S(6)));
  ImGuiWindowFlags flags = ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoScrollWithMouse;
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(S(8), S(5)));
  ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
  ImGui::PushStyleColor(ImGuiCol_WindowBg, ImGui::GetStyleColorVec4(ImGuiCol_MenuBarBg));
  bool open = ImGui::BeginViewportSideBar("##transport", vp, ImGuiDir_Up, h, flags);
  ImGui::PopStyleColor();
  ImGui::PopStyleVar(2);
  if (!open) {
    ImGui::End();
    return;
  }

  bool playing = engine_->playing();
  if (playing && !wasPlaying_) playTime_ = 0;
  if (playing && !engine_->countingIn()) playTime_ += io.DeltaTime * engine_->tempoScale;
  wasPlaying_ = playing;

  float bs = h - S(10);  // square buttons filling the bar
  ImVec2 B(bs, bs);
  const ImU32 green = IM_COL32(70, 200, 120, 255), red = IM_COL32(230, 70, 80, 255);
  ImGui::PushFont(F.big);
  ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, S(7));
  ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(S(4), 0));
  if (iconButton("start", ICON_TO_START, nullptr, false, B, T("Play the song from the start"))) {
    engine_->stop();
    engine_->play(0, 0);
  }
  ImGui::SameLine();
  if (iconButton("play", playing ? ICON_PAUSE : ICON_PLAY, nullptr, playing, B,
                 playing ? T("Stop (keeps the cursor here)") : T("Play from this order"), green))
    togglePlay(false);
  ImGui::SameLine();
  if (iconButton("stop", ICON_STOP, nullptr, false, B, T("Stop"))) engine_->stop();
  ImGui::SameLine();
  if (iconButton("playrow", ICON_PLAY_ROW, nullptr, false, B, T("Play from the cursor row"))) {
    engine_->stop();
    togglePlay(true);
  }
  ImGui::SameLine(0, S(12));
  if (iconButton("rec", ICON_RECORD, nullptr, editMode_, B, T("Record: notes typed or played go into the pattern"), red))
    editMode_ = !editMode_;
  ImGui::SameLine();
  ImGui::PopFont();
  ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(S(2), 0));
  if (iconButton("recopt", ICON_DOWN_SMALL, nullptr, false, ImVec2(S(18), bs), T("Recording options")))
    ImGui::OpenPopup("recoptions");
  ImGui::PopStyleVar();
  ImGui::PushFont(F.big);
  ImGui::SameLine(0, S(12));
  if (iconButton("loop", ICON_LOOP, nullptr, engine_->loopPattern, B, T("Loop the pattern"))) engine_->loopPattern = !engine_->loopPattern;
  ImGui::SameLine();
  if (iconButton("follow", ICON_FOLLOW, nullptr, follow_, B, T("Follow the playback"))) follow_ = !follow_;
  ImGui::SameLine();
  if (iconButton("metro", ICON_METRONOME, nullptr, engine_->metronome, B, T("Metronome"))) engine_->metronome = !engine_->metronome;
  ImGui::PopStyleVar(2);
  ImGui::PopFont();

  if (ImGui::BeginPopup("recoptions")) {
    ImGui::SetNextItemWidth(S(140));
    ImGui::Combo(T("Count-in"), &countInBars_, TC("Off\0" "1 bar\0" "2 bars\0" "3 bars\0" "4 bars\0"));
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", T("Clicks before playback starts in Record mode (a bar = Highlight 2 rows)"));
    ImGui::SetNextItemWidth(S(140));
    ImGui::Combo(T("MIDI quantize"), &midiQuantize_, TC("Off\0" "Nearest row\0" "2 rows\0" "Beat\0"));
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", T("Live MIDI notes go to the nearest row of this grid instead of the row playing"));
    ImGui::SetNextItemWidth(S(140));
    ImGui::SliderFloat(T("Metronome volume"), &metronomeVol_, 0, 1, "%.2f");
    ImGui::EndPopup();
  }
  engine_->metronomeVolume = metronomeVol_;

  // The display: order, row, time, tempo.
  ImGui::SameLine(0, S(14));
  ImDrawList* dl = ImGui::GetWindowDrawList();
  ImVec2 p = ImGui::GetCursorScreenPos();
  ImVec4 ac = accentColor();
  ImU32 valueColor = withAlpha(ImVec4(ac.x * 0.4f + 0.6f, ac.y * 0.4f + 0.6f, ac.z * 0.4f + 0.6f, 1), 1);
  char ord[16], row[16], tim[24], bpmText[16];
  int o = playing ? engine_->order() : curOrder_, r = playing ? engine_->row() : curRow_;
  std::snprintf(ord, sizeof(ord), "%02X/%02X", o, std::max(0, (int)song_.orders.size() - 1));
  std::snprintf(row, sizeof(row), "%02X", r);
  int tenths = (int)(playTime_ * 10);
  std::snprintf(tim, sizeof(tim), "%d:%02d.%d", tenths / 600, tenths / 10 % 60, tenths % 10);
  float bpm = song_.tickRate * 60.0f / (std::max(engine_->speed(), 1) * std::max(song_.highlight1, 1));
  std::snprintf(bpmText, sizeof(bpmText), "%.1f", bpm);
  // Measure first, then draw the panel behind the cells.
  float wOrd = 0, wRow = 0, wTim = 0, wBpm = 0, lcdEnd = p.x;
  {
    ImDrawListSplitter split;
    split.Split(dl, 2);
    split.SetCurrentChannel(dl, 1);
    float x = p.x;
    x += wOrd = lcdCell(dl, ImVec2(x, p.y), bs, T("ORDER"), ord, valueColor);
    x += wRow = lcdCell(dl, ImVec2(x, p.y), bs, T("ROW"), row, valueColor);
    x += wTim = lcdCell(dl, ImVec2(x, p.y), bs, T("TIME"), tim, valueColor);
    x += wBpm = lcdCell(dl, ImVec2(x, p.y), bs, T("BPM"), bpmText, valueColor);
    if (engine_->countingIn()) x += lcdCell(dl, ImVec2(x, p.y), bs, "", T("COUNT-IN"), IM_COL32(255, 200, 80, 255));
    split.SetCurrentChannel(dl, 0);
    ImVec4 bg = ImGui::GetStyleColorVec4(ImGuiCol_FrameBg);
    dl->AddRectFilled(p, ImVec2(x, p.y + bs), withAlpha(ImVec4(bg.x * 0.6f, bg.y * 0.6f, bg.z * 0.6f, 1), 1), S(7));
    dl->AddRect(p, ImVec2(x, p.y + bs), withAlpha(ac, 0.25f), S(7));
    float sx = p.x;
    for (float w : {wOrd, wRow, wTim}) {
      sx += w;
      dl->AddLine(ImVec2(sx, p.y + S(8)), ImVec2(sx, p.y + bs - S(8)), withAlpha(ImVec4(1, 1, 1, 1), 0.07f));
    }
    split.Merge(dl);
    ImGui::Dummy(ImVec2(x - p.x, bs));
    lcdEnd = x;
  }
  if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
    ImGui::SetTooltip("%s", T("Order / row playing (or at the cursor), time since Play, and tempo.\n"
                              "The tempo comes from the Song window: Tick rate, Speed and Highlight 1."));

  // Right side: the master scope and level meter.
  float vuW = S(150), scopeW = S(120);
  float right = ImGui::GetWindowPos().x + ImGui::GetWindowWidth() - S(8);
  float x0 = right - vuW - scopeW - S(8);
  // Between them: the song's name and author.
  {
    float tx = lcdEnd + S(18);
    std::string name = song_.name.empty() ? std::string(T("Untitled")) : song_.name;
    ImVec2 nw = ImGui::CalcTextSize(name.c_str());
    float limit = x0 - S(18);
    if (tx + S(60) < limit) {
      ImGui::PushClipRect(ImVec2(tx, p.y), ImVec2(limit, p.y + bs), true);
      float ty = p.y + (bs - nw.y - (song_.author.empty() ? 0 : F.caption->FontSize)) * 0.5f;
      dl->AddText(ImVec2(tx, ty), ImGui::GetColorU32(ImGuiCol_Text), name.c_str());
      if (!song_.author.empty())
        dl->AddText(F.caption, F.caption->FontSize, ImVec2(tx, ty + nw.y), ImGui::GetColorU32(ImGuiCol_TextDisabled), song_.author.c_str());
      ImGui::PopClipRect();
    }
  }
  if (x0 > lcdEnd + S(20)) {
    const auto& sc = engine_->masterScope();
    ImVec2 a(x0, p.y), b(x0 + scopeW, p.y + bs);
    dl->AddRectFilled(a, b, ImGui::GetColorU32(ImGuiCol_FrameBg), S(6));
    int start = engine_->scopePos();
    ImVec2 prev;
    for (int i = 0; i < (int)scopeW; i++) {
      int k = (start + i * SCOPE_LEN / (int)scopeW) % SCOPE_LEN;
      ImVec2 q(a.x + i, a.y + bs * 0.5f - std::clamp(sc[k], -1.0f, 1.0f) * bs * 0.42f);
      if (i) dl->AddLine(prev, q, withAlpha(ac, 0.9f), 1.2f);
      prev = q;
    }
    float peak = 0;
    for (float v : sc) peak = std::max(peak, std::abs(v));
    vuLevel_ = std::max(peak, vuLevel_ - io.DeltaTime * 1.2f);
    if (peak >= vuPeak_ || ImGui::GetTime() - vuPeakTime_ > 1.2) {
      vuPeak_ = peak;
      vuPeakTime_ = ImGui::GetTime();
    }
    auto frac = [](float lin) { return std::clamp((20.0f * std::log10(std::max(lin, 1e-5f)) + 48.0f) / 48.0f, 0.0f, 1.0f); };
    ImVec2 va(b.x + S(8), p.y + bs * 0.3f), vb(right, p.y + bs * 0.7f);
    dl->AddRectFilled(va, vb, ImGui::GetColorU32(ImGuiCol_FrameBg), S(4));
    float fw = (vb.x - va.x) * frac(vuLevel_);
    ImU32 cl = vuLevel_ > 0.99f ? red : vuLevel_ > 0.5f ? IM_COL32(240, 200, 70, 255) : green;
    dl->AddRectFilled(va, ImVec2(va.x + fw, vb.y), cl, S(4));
    float px = va.x + (vb.x - va.x) * frac(vuPeak_);
    dl->AddLine(ImVec2(px, va.y), ImVec2(px, vb.y), IM_COL32(255, 255, 255, 200), 2.0f);
    char db[24];
    float dbv = 20.0f * std::log10(std::max(vuPeak_, 1e-5f));
    if (dbv < -60) std::snprintf(db, sizeof(db), "-inf dB");
    else std::snprintf(db, sizeof(db), "%+.1f dB", dbv);
    float capSize = F.caption->FontSize;
    dl->AddText(F.caption, capSize, ImVec2(va.x, va.y - capSize - S(1)), ImGui::GetColorU32(ImGuiCol_TextDisabled), T("MASTER"));
    ImVec2 dbW = F.caption->CalcTextSizeA(capSize, FLT_MAX, 0, db);
    dl->AddText(F.caption, capSize, ImVec2(vb.x - dbW.x, vb.y + S(1)), vuPeak_ > 0.99f ? red : ImGui::GetColorU32(ImGuiCol_TextDisabled), db);
  }
  ImGui::End();
}

void App::statusBar() {
  ImGuiViewport* vp = ImGui::GetMainViewport();
  float h = ImGui::GetFrameHeight();
  ImGuiWindowFlags flags = ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoScrollWithMouse;
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(S(10), 0));
  ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
  ImGui::PushStyleColor(ImGuiCol_WindowBg, ImGui::GetStyleColorVec4(ImGuiCol_MenuBarBg));
  bool open = ImGui::BeginViewportSideBar("##status", vp, ImGuiDir_Down, h, flags);
  ImGui::PopStyleColor();
  ImGui::PopStyleVar(2);
  if (open) {
    ImGui::SetCursorPosY((h - ImGui::GetTextLineHeight()) * 0.5f);
    bool fresh = !status_.empty() && ImGui::GetTime() - statusTime_ < 8.0;
    if (fresh) {
      ImGui::TextUnformatted(ICON_INFO);
      ImGui::SameLine();
      ImGui::TextUnformatted(status_.c_str());
    } else {
      ImGui::TextDisabled("%s", T("Ready"));
    }
    // Right: file, MIDI, audio.
    std::string file = filePath_.empty() ? song_.name : fs::path(filePath_).filename().string();
    std::string right = file + (dirty_ ? std::string("  ") + T("(modified)") : std::string());
    if (midi_.isOpen()) right += std::string("   |   MIDI: ") + midi_.portName();
    right += "   |   " + audioStatus_;
    float w = ImGui::CalcTextSize(right.c_str()).x;
    float x = ImGui::GetWindowWidth() - w - S(10);
    if (x > ImGui::GetCursorPosX() + S(20)) {
      ImGui::SameLine(x);
      ImGui::TextDisabled("%s", right.c_str());
    }
  }
  ImGui::End();
}
