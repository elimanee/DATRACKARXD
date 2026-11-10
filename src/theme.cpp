#include "theme.h"

#include <algorithm>
#include <cstring>

namespace {

ImVec4 rgb(int r, int g, int b, float a = 1.0f) { return ImVec4(r / 255.0f, g / 255.0f, b / 255.0f, a); }

int current = 0;
float uiScale = 1.0f;

}  // namespace

const Theme THEMES[THEME_COUNT] = {
    {
        "Studio",
        IM_COL32(22, 23, 28, 255), IM_COL32(29, 30, 37, 255), IM_COL32(38, 39, 50, 255), IM_COL32(124, 108, 255, 60),
        IM_COL32(230, 80, 90, 55), IM_COL32(70, 200, 120, 50), IM_COL32(124, 108, 255, 70), IM_COL32(46, 48, 58, 255),
        IM_COL32(232, 234, 242, 255), IM_COL32(150, 152, 168, 255), IM_COL32(110, 215, 160, 255), IM_COL32(120, 180, 255, 255),
        IM_COL32(255, 170, 90, 255), IM_COL32(255, 212, 150, 255), IM_COL32(68, 70, 82, 255), IM_COL32(118, 120, 136, 255),
        IM_COL32(220, 200, 120, 255), IM_COL32(160, 150, 255, 230),
        IM_COL32(36, 38, 46, 255), IM_COL32(52, 55, 68, 255), IM_COL32(78, 34, 42, 255), IM_COL32(226, 228, 236, 255),
        IM_COL32(18, 19, 23, 255), IM_COL32(110, 215, 160, 255), IM_COL32(110, 60, 66, 255), IM_COL32(160, 150, 255, 255),
        rgb(30, 31, 37), rgb(40, 42, 51), rgb(23, 24, 29), rgb(30, 31, 37), rgb(124, 108, 255), rgb(40, 42, 51), rgb(226, 228, 236),
        6.0f, true,
    },
    {
        "Graphite",
        IM_COL32(24, 24, 26, 255), IM_COL32(31, 31, 34, 255), IM_COL32(41, 40, 42, 255), IM_COL32(255, 140, 60, 55),
        IM_COL32(230, 80, 90, 55), IM_COL32(90, 200, 120, 50), IM_COL32(255, 140, 60, 65), IM_COL32(50, 50, 54, 255),
        IM_COL32(236, 236, 238, 255), IM_COL32(155, 155, 160, 255), IM_COL32(150, 215, 110, 255), IM_COL32(110, 190, 240, 255),
        IM_COL32(255, 150, 70, 255), IM_COL32(255, 205, 140, 255), IM_COL32(72, 72, 78, 255), IM_COL32(122, 122, 130, 255),
        IM_COL32(255, 190, 110, 255), IM_COL32(255, 160, 90, 230),
        IM_COL32(40, 40, 44, 255), IM_COL32(58, 58, 64, 255), IM_COL32(80, 36, 36, 255), IM_COL32(232, 232, 236, 255),
        IM_COL32(19, 19, 21, 255), IM_COL32(255, 170, 90, 255), IM_COL32(110, 64, 60, 255), IM_COL32(255, 200, 130, 255),
        rgb(32, 32, 35), rgb(43, 43, 47), rgb(25, 25, 27), rgb(32, 32, 35), rgb(255, 140, 60), rgb(43, 43, 47), rgb(232, 232, 236),
        6.0f, true,
    },
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

int themeByName(const char* name) {
  for (int i = 0; i < THEME_COUNT; i++)
    if (std::strcmp(THEMES[i].name, name) == 0) return i;
  return -1;
}

namespace {

// The modern look: flat widgets, soft corners, more air, and colors made
// from a few base tones (background, frame, accent, text).
void modernStyle(const Theme& t, ImGuiStyle& style) {
  style.WindowPadding = ImVec2(10, 8);
  style.FramePadding = ImVec2(8, 4);
  style.CellPadding = ImVec2(6, 3);
  style.ItemSpacing = ImVec2(8, 6);
  style.ItemInnerSpacing = ImVec2(6, 4);
  style.IndentSpacing = 18;
  style.ScrollbarSize = 12;
  style.GrabMinSize = 10;
  style.WindowBorderSize = 1;
  style.ChildBorderSize = 1;
  style.PopupBorderSize = 1;
  style.FrameBorderSize = 0;
  style.TabBorderSize = 0;
  style.TabBarBorderSize = 1;
  style.TabBarOverlineSize = 2;
  style.WindowRounding = 8;
  style.ChildRounding = 6;
  style.FrameRounding = 5;
  style.PopupRounding = 8;
  style.ScrollbarRounding = 8;
  style.GrabRounding = 4;
  style.TabRounding = 6;
  style.WindowTitleAlign = ImVec2(0.0f, 0.5f);
  style.WindowMenuButtonPosition = ImGuiDir_None;
  style.SeparatorTextBorderSize = 1;
  style.DockingSeparatorSize = 2;

  auto mix = [](ImVec4 a, ImVec4 b, float k) {
    return ImVec4(a.x + (b.x - a.x) * k, a.y + (b.y - a.y) * k, a.z + (b.z - a.z) * k, a.w + (b.w - a.w) * k);
  };
  auto alpha = [](ImVec4 v, float a) { return ImVec4(v.x, v.y, v.z, a); };
  const ImVec4 white(1, 1, 1, 1), black(0, 0, 0, 1);
  ImVec4 bg = t.windowBg, fr = t.frame, ac = t.accent, tx = t.text;
  ImVec4* c = style.Colors;
  c[ImGuiCol_Text] = tx;
  c[ImGuiCol_TextDisabled] = mix(tx, bg, 0.48f);
  c[ImGuiCol_WindowBg] = bg;
  c[ImGuiCol_ChildBg] = ImVec4(0, 0, 0, 0);
  c[ImGuiCol_PopupBg] = alpha(mix(bg, white, 0.05f), 0.98f);
  c[ImGuiCol_Border] = alpha(white, 0.08f);
  c[ImGuiCol_BorderShadow] = ImVec4(0, 0, 0, 0);
  c[ImGuiCol_FrameBg] = fr;
  c[ImGuiCol_FrameBgHovered] = mix(fr, white, 0.07f);
  c[ImGuiCol_FrameBgActive] = mix(fr, ac, 0.25f);
  c[ImGuiCol_TitleBg] = t.tab;
  c[ImGuiCol_TitleBgActive] = t.tab;
  c[ImGuiCol_TitleBgCollapsed] = t.tab;
  c[ImGuiCol_MenuBarBg] = t.tab;
  c[ImGuiCol_ScrollbarBg] = ImVec4(0, 0, 0, 0);
  c[ImGuiCol_ScrollbarGrab] = alpha(white, 0.12f);
  c[ImGuiCol_ScrollbarGrabHovered] = alpha(white, 0.2f);
  c[ImGuiCol_ScrollbarGrabActive] = alpha(ac, 0.8f);
  c[ImGuiCol_CheckMark] = mix(ac, white, 0.2f);
  c[ImGuiCol_SliderGrab] = ac;
  c[ImGuiCol_SliderGrabActive] = mix(ac, white, 0.25f);
  c[ImGuiCol_Button] = mix(fr, white, 0.05f);
  c[ImGuiCol_ButtonHovered] = mix(fr, ac, 0.45f);
  c[ImGuiCol_ButtonActive] = mix(fr, ac, 0.75f);
  c[ImGuiCol_Header] = alpha(ac, 0.3f);
  c[ImGuiCol_HeaderHovered] = alpha(ac, 0.45f);
  c[ImGuiCol_HeaderActive] = alpha(ac, 0.65f);
  c[ImGuiCol_Separator] = alpha(white, 0.08f);
  c[ImGuiCol_SeparatorHovered] = alpha(ac, 0.6f);
  c[ImGuiCol_SeparatorActive] = ac;
  c[ImGuiCol_ResizeGrip] = alpha(white, 0.05f);
  c[ImGuiCol_ResizeGripHovered] = alpha(ac, 0.5f);
  c[ImGuiCol_ResizeGripActive] = ac;
  c[ImGuiCol_Tab] = t.tab;
  c[ImGuiCol_TabHovered] = mix(t.tabSelected, ac, 0.35f);
  c[ImGuiCol_TabSelected] = mix(t.tabSelected, white, 0.12f);
  c[ImGuiCol_TabSelectedOverline] = ac;
  c[ImGuiCol_TabDimmed] = t.tab;
  c[ImGuiCol_TabDimmedSelected] = mix(t.tabSelected, white, 0.03f);
  c[ImGuiCol_TabDimmedSelectedOverline] = alpha(ac, 0.35f);
  c[ImGuiCol_DockingPreview] = alpha(ac, 0.6f);
  c[ImGuiCol_DockingEmptyBg] = mix(bg, black, 0.3f);
  c[ImGuiCol_PlotLines] = ac;
  c[ImGuiCol_PlotLinesHovered] = mix(ac, white, 0.3f);
  c[ImGuiCol_PlotHistogram] = ac;
  c[ImGuiCol_PlotHistogramHovered] = mix(ac, white, 0.3f);
  c[ImGuiCol_TableHeaderBg] = mix(bg, white, 0.04f);
  c[ImGuiCol_TableBorderStrong] = alpha(white, 0.1f);
  c[ImGuiCol_TableBorderLight] = alpha(white, 0.05f);
  c[ImGuiCol_TableRowBg] = ImVec4(0, 0, 0, 0);
  c[ImGuiCol_TableRowBgAlt] = alpha(white, 0.025f);
  c[ImGuiCol_TextLink] = mix(ac, white, 0.3f);
  c[ImGuiCol_TextSelectedBg] = alpha(ac, 0.35f);
  c[ImGuiCol_DragDropTarget] = mix(ac, white, 0.3f);
  c[ImGuiCol_NavCursor] = ac;
  c[ImGuiCol_ModalWindowDimBg] = ImVec4(0, 0, 0, 0.5f);
}

}  // namespace

void setTheme(int index) {
  current = std::clamp(index, 0, THEME_COUNT - 1);
  const Theme& t = THEMES[current];
  ImGuiStyle& style = ImGui::GetStyle();
  style = ImGuiStyle();  // sizes from scratch, so the scale doesn't add up
  ImGui::StyleColorsDark();
  if (t.modern) {
    modernStyle(t, style);
    if (uiScale != 1.0f) style.ScaleAllSizes(uiScale);
    return;
  }
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
  if (uiScale != 1.0f) style.ScaleAllSizes(uiScale);
}

float themeScale() { return uiScale; }

void setThemeScale(float scale) {
  uiScale = scale;
  setTheme(current);
}
