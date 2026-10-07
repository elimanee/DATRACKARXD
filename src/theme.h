// Color themes, including the neon "Keygen" one.
#pragma once
#include <imgui.h>

struct Theme {
  const char* name;
  // Pattern editor.
  ImU32 patBg, rowHi1, rowHi2, cursorRow, editRow, playRow, selection, separator;
  ImU32 note, noteOff, ins, vol, fxCmd, fxVal, empty, rowNum, rowNumHi, cursorBox;
  ImU32 header, headerHover, headerMuted, headerText;
  // Scopes.
  ImU32 scopeBg, scope, scopeMuted, scopeMaster;
  // ImGui widgets.
  ImVec4 windowBg, titleActive, tab, tabSelected, accent, frame, text;
  float rounding;
};

constexpr int THEME_COUNT = 4;
extern const Theme THEMES[THEME_COUNT];

const Theme& theme();
int themeIndex();
void setTheme(int index);  // also restyles ImGui
