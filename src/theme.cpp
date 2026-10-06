#include "theme.h"

#include <algorithm>

namespace {

ImVec4 rgb(int r, int g, int b, float a = 1.0f) { return ImVec4(r / 255.0f, g / 255.0f, b / 255.0f, a); }

int current = 0;

}  // namespace

const Theme THEMES[THEME_COUNT] = {
    {
        "Furnace",
        IM_COL32(18, 18, 26, 255), IM_COL32(32, 32, 48, 255), IM_COL32(45, 45, 72, 255), IM_COL32(40, 70, 130, 120),
        IM_COL32(120, 40, 50, 120), IM_COL32(40, 110, 50, 110), IM_COL32(90, 120, 210, 100), IM_COL32(60, 60, 80, 255),
        IM_COL32(225, 228, 240, 255), IM_COL32(160, 160, 175, 255), IM_COL32(120, 225, 140, 255), IM_COL32(120, 180, 255, 255),
        IM_COL32(255, 160, 80, 255), IM_COL32(255, 215, 140, 255), IM_COL32(85, 85, 100, 255), IM_COL32(140, 140, 160, 255),
        IM_COL32(230, 230, 120, 255), IM_COL32(230, 230, 255, 230),
        IM_COL32(40, 40, 60, 255), IM_COL32(60, 60, 90, 255), IM_COL32(70, 30, 30, 255), IM_COL32(230, 230, 240, 255),
        IM_COL32(15, 15, 22, 255), IM_COL32(110, 230, 140, 255), IM_COL32(110, 60, 60, 255), IM_COL32(120, 190, 255, 255),
        rgb(23, 23, 33), rgb(56, 51, 115), rgb(41, 38, 77), rgb(82, 71, 158), rgb(77, 69, 140, 0.6f), rgb(38, 38, 56), rgb(235, 235, 245),
        4.0f,
    },
    {
        "Keygen",
        IM_COL32(4, 2, 10, 255), IM_COL32(18, 6, 30, 255), IM_COL32(40, 8, 60, 255), IM_COL32(0, 120, 140, 110),
        IM_COL32(170, 0, 120, 120), IM_COL32(0, 160, 60, 110), IM_COL32(255, 0, 200, 80), IM_COL32(70, 0, 110, 255),
        IM_COL32(0, 255, 240, 255), IM_COL32(255, 60, 160, 255), IM_COL32(120, 255, 60, 255), IM_COL32(255, 255, 0, 255),
        IM_COL32(255, 70, 255, 255), IM_COL32(255, 160, 255, 255), IM_COL32(70, 40, 100, 255), IM_COL32(150, 90, 220, 255),
        IM_COL32(0, 255, 240, 255), IM_COL32(0, 255, 240, 255),
        IM_COL32(30, 0, 50, 255), IM_COL32(90, 0, 130, 255), IM_COL32(110, 0, 40, 255), IM_COL32(0, 255, 240, 255),
        IM_COL32(4, 0, 12, 255), IM_COL32(0, 255, 200, 255), IM_COL32(120, 0, 70, 255), IM_COL32(255, 0, 220, 255),
        rgb(8, 4, 18), rgb(140, 0, 170), rgb(50, 0, 80), rgb(190, 0, 210), rgb(0, 200, 220, 0.5f), rgb(30, 8, 50), rgb(230, 255, 255),
        0.0f,
    },
    {
        "FastTracker",
        IM_COL32(0, 0, 0, 255), IM_COL32(20, 20, 30, 255), IM_COL32(35, 35, 55, 255), IM_COL32(73, 117, 130, 150),
        IM_COL32(150, 60, 60, 140), IM_COL32(70, 110, 70, 120), IM_COL32(100, 140, 180, 100), IM_COL32(73, 117, 130, 255),
        IM_COL32(255, 255, 130, 255), IM_COL32(200, 200, 200, 255), IM_COL32(255, 255, 130, 255), IM_COL32(255, 255, 130, 255),
        IM_COL32(255, 255, 130, 255), IM_COL32(255, 255, 130, 255), IM_COL32(90, 90, 90, 255), IM_COL32(255, 255, 255, 255),
        IM_COL32(255, 255, 255, 255), IM_COL32(255, 255, 255, 220),
        IM_COL32(73, 117, 130, 255), IM_COL32(100, 150, 165, 255), IM_COL32(130, 60, 60, 255), IM_COL32(0, 0, 0, 255),
        IM_COL32(0, 0, 0, 255), IM_COL32(255, 255, 130, 255), IM_COL32(100, 100, 80, 255), IM_COL32(255, 255, 255, 255),
        rgb(73, 117, 130), rgb(43, 69, 77), rgb(56, 92, 102), rgb(102, 161, 179), rgb(140, 190, 205, 0.6f), rgb(40, 66, 74), rgb(255, 255, 255),
        0.0f,
    },
    {
        "Amber CRT",
        IM_COL32(12, 7, 0, 255), IM_COL32(24, 14, 0, 255), IM_COL32(40, 24, 0, 255), IM_COL32(110, 60, 0, 120),
        IM_COL32(150, 40, 0, 130), IM_COL32(90, 70, 0, 110), IM_COL32(200, 120, 0, 90), IM_COL32(60, 35, 0, 255),
        IM_COL32(255, 176, 0, 255), IM_COL32(200, 120, 0, 255), IM_COL32(255, 200, 80, 255), IM_COL32(255, 150, 30, 255),
        IM_COL32(255, 140, 0, 255), IM_COL32(255, 190, 60, 255), IM_COL32(90, 55, 0, 255), IM_COL32(180, 110, 0, 255),
        IM_COL32(255, 210, 100, 255), IM_COL32(255, 200, 80, 230),
        IM_COL32(40, 22, 0, 255), IM_COL32(70, 40, 0, 255), IM_COL32(80, 20, 0, 255), IM_COL32(255, 176, 0, 255),
        IM_COL32(10, 5, 0, 255), IM_COL32(255, 176, 0, 255), IM_COL32(100, 50, 0, 255), IM_COL32(255, 210, 100, 255),
        rgb(16, 9, 0), rgb(100, 55, 0), rgb(60, 33, 0), rgb(150, 85, 0), rgb(200, 120, 0, 0.5f), rgb(40, 22, 0), rgb(255, 190, 60),
        2.0f,
    },
};

const Theme& theme() { return THEMES[current]; }
int themeIndex() { return current; }

void setTheme(int index) {
  current = std::clamp(index, 0, THEME_COUNT - 1);
  const Theme& t = THEMES[current];
  ImGui::StyleColorsDark();
  ImGuiStyle& style = ImGui::GetStyle();
  style.WindowRounding = t.rounding;
  style.FrameRounding = t.rounding * 0.75f;
  style.TabRounding = t.rounding;
  style.GrabRounding = t.rounding * 0.75f;
  ImVec4* c = style.Colors;
  auto dim = [](ImVec4 v, float k) { return ImVec4(v.x * k, v.y * k, v.z * k, v.w); };
  c[ImGuiCol_WindowBg] = t.windowBg;
  c[ImGuiCol_ChildBg] = ImVec4(0, 0, 0, 0);
  c[ImGuiCol_PopupBg] = dim(t.windowBg, 1.2f);
  c[ImGuiCol_MenuBarBg] = dim(t.windowBg, 0.8f);
  c[ImGuiCol_TitleBg] = dim(t.titleActive, 0.6f);
  c[ImGuiCol_TitleBgActive] = t.titleActive;
  c[ImGuiCol_TitleBgCollapsed] = dim(t.titleActive, 0.5f);
  c[ImGuiCol_Tab] = t.tab;
  c[ImGuiCol_TabHovered] = t.tabSelected;
  c[ImGuiCol_TabSelected] = t.tabSelected;
  c[ImGuiCol_TabDimmed] = dim(t.tab, 0.8f);
  c[ImGuiCol_TabDimmedSelected] = dim(t.tabSelected, 0.8f);
  c[ImGuiCol_Header] = t.accent;
  c[ImGuiCol_HeaderHovered] = ImVec4(t.accent.x, t.accent.y, t.accent.z, 0.8f);
  c[ImGuiCol_HeaderActive] = ImVec4(t.accent.x, t.accent.y, t.accent.z, 1.0f);
  c[ImGuiCol_FrameBg] = t.frame;
  c[ImGuiCol_FrameBgHovered] = dim(t.frame, 1.4f);
  c[ImGuiCol_FrameBgActive] = dim(t.frame, 1.8f);
  c[ImGuiCol_Button] = dim(t.tabSelected, 0.7f);
  c[ImGuiCol_ButtonHovered] = t.tabSelected;
  c[ImGuiCol_ButtonActive] = dim(t.tabSelected, 1.3f);
  c[ImGuiCol_SliderGrab] = ImVec4(t.accent.x, t.accent.y, t.accent.z, 1.0f);
  c[ImGuiCol_SliderGrabActive] = dim(ImVec4(t.accent.x, t.accent.y, t.accent.z, 1.0f), 1.3f);
  c[ImGuiCol_CheckMark] = ImVec4(t.accent.x, t.accent.y, t.accent.z, 1.0f);
  c[ImGuiCol_Text] = t.text;
  c[ImGuiCol_Border] = dim(t.tabSelected, 0.8f);
  c[ImGuiCol_Separator] = dim(t.tabSelected, 0.8f);
  c[ImGuiCol_DockingPreview] = ImVec4(t.accent.x, t.accent.y, t.accent.z, 0.7f);
}
