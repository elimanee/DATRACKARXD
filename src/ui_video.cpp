// Video export: the options popup, then the progress while the frames are
// drawn and written (a few per UI frame, so the window stays alive).
#include <imgui.h>

#include <algorithm>
#include <cstdio>

#include "app.h"
#include "ui_fonts.h"
#include "i18n.h"
#include "theme.h"

void App::videoExportPopup() {
  if (videoPopupRequest_) {
    ImGui::OpenPopup("###videoexp");
    videoPopupRequest_ = false;
    if (!playerScroll_[0]) {
      std::string t = "Now playing: " + song_.name + (song_.author.empty() ? "" : "  by " + song_.author) +
                      "   ***   made with DATRACKARXD   ***   greetings to all trackers";
      std::snprintf(playerScroll_, sizeof(playerScroll_), "%s", t.c_str());
    }
  }
  ImGui::SetNextWindowSize(ImVec2(S(520), 0), ImGuiCond_Appearing);
  std::string title = std::string(T("Export video")) + "###videoexp";
  if (!ImGui::BeginPopupModal(title.c_str(), nullptr)) return;
  VideoOptions& o = videoOpt_;
  const float w = S(260);
  ImGui::SetNextItemWidth(w);
  ImGui::Combo(T("Picture"), &o.scene, TC("Keygen scene\0Oscilloscopes (one per channel)\0"));
  ImGui::SetNextItemWidth(w);
  ImGui::Combo(T("Size"), &videoRes_, "640 x 360\0" "1280 x 720 (HD)\0" "1920 x 1080 (Full HD)\0");
  static const int RES[3][2] = {{640, 360}, {1280, 720}, {1920, 1080}};
  o.width = RES[std::clamp(videoRes_, 0, 2)][0];
  o.height = RES[std::clamp(videoRes_, 0, 2)][1];
  int fpsIdx = o.fps >= 60 ? 1 : 0;
  ImGui::SetNextItemWidth(w);
  if (ImGui::Combo(T("Frames per second"), &fpsIdx, "30\0" "60\0")) o.fps = fpsIdx ? 60 : 30;
  ImGui::SetNextItemWidth(w);
  ImGui::SliderInt(T("Loops"), &o.loops, 1, 4);
  if (o.scene == 0) {
    ImGui::SeparatorText(T("Keygen scene"));
    ImGui::SetNextItemWidth(w);
    ImGui::InputText(T("Logo"), playerLogo_, sizeof(playerLogo_));
    ImGui::TextUnformatted(T("Scroller text"));
    ImGui::InputTextMultiline("##vscroll", playerScroll_, sizeof(playerScroll_), ImVec2(-1, S(60)));
    KeygenStyle& st = o.style;
    int bg = st.background == 0 && !st.stars ? 4 : st.background;
    ImGui::SetNextItemWidth(w);
    if (ImGui::Combo(T("Background"), &bg, TC("Starfield\0Plasma\0Tunnel\0Rotozoom\0Black\0"))) {
      st.background = bg;
      st.stars = bg == 0;
    }
    ImGui::SetNextItemWidth(w);
    ImGui::Combo(T("3D object"), &st.object, TC("None\0Cube\0Pyramid\0Octahedron\0"));
    ImGui::Checkbox(T("Cycle backgrounds"), &st.cycle);
    ImGui::SameLine();
    ImGui::Checkbox(T("VU meters"), &st.vu);
    ImGui::SameLine();
    ImGui::Checkbox(T("Flash on hits"), &st.flash);
  }
  ImGui::Separator();
  if (findFfmpeg().empty())
    ImGui::TextWrapped("%s", T("ffmpeg was not found: the video will be an .avi written by DATRACKARXD (MJPEG pictures, "
                               "uncompressed sound, bigger files). With ffmpeg installed, it can be an .mp4."));
  else
    ImGui::TextWrapped("%s", T("ffmpeg found: .mp4 (H.264 + AAC), or .avi without it."));
  if (ImGui::Button(T("Export..."), ImVec2(S(120), 0))) {
    o.logo = playerLogo_;
    o.scroll = playerScroll_;
    ImGui::CloseCurrentPopup();
    openFileDialog(FileDialogMode::ExportVideo);
  }
  ImGui::SameLine();
  if (ImGui::Button(T("Cancel"), ImVec2(S(120), 0)) || ImGui::IsKeyPressed(ImGuiKey_Escape)) ImGui::CloseCurrentPopup();
  ImGui::EndPopup();
}

void App::videoProgress() {
  if (!video_) return;
  if (!ImGui::IsPopupOpen("###videoprog")) ImGui::OpenPopup("###videoprog");
  std::string title = std::string(T("Exporting video")) + "###videoprog";
  if (!ImGui::BeginPopupModal(title.c_str(), nullptr, ImGuiWindowFlags_AlwaysAutoResize)) return;
  // As many frames as fit in about 30 ms, then let the window redraw.
  Uint64 start = SDL_GetTicks64();
  ImGui::PushFont(uiFonts().pixel);  // the same picture as from the command line
  while (video_->step(1) && SDL_GetTicks64() - start < 30) {
  }
  ImGui::PopFont();
  ImGui::TextUnformatted(videoPath_.c_str());
  char pct[32];
  std::snprintf(pct, sizeof(pct), "%d%%", (int)(video_->progress() * 100));
  ImGui::ProgressBar(video_->progress(), ImVec2(S(380), 0), pct);
  bool close = false;
  if (ImGui::Button(T("Cancel"), ImVec2(S(120), 0))) {
    video_->cancel();
    setStatus(T("Video export cancelled"));
    close = true;
  } else if (video_->done()) {
    std::string err;
    if (video_->finish(err)) setStatus(std::string(T("Exported ")) + videoPath_);
    else setStatus(std::string(T("Video export failed: ")) + err);
    close = true;
  }
  if (close) {
    video_.reset();
    ImGui::CloseCurrentPopup();
  }
  ImGui::EndPopup();
}
