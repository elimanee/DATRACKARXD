// Small widgets shared by the windows: icon buttons and toggles.
#pragma once
#include <imgui.h>

#include <string>

// A button showing an icon (and optional text). `on` paints it with the
// accent color, for toggles. The tooltip shows on hover when given.
bool iconButton(const char* id, const char* icon, const char* text = nullptr, bool on = false, ImVec2 size = ImVec2(0, 0),
                const char* tooltip = nullptr, ImU32 onColor = 0);
// The accent color of the current theme.
ImVec4 accentColor();
// "icon  text": a label with its icon (window titles keep their ### id).
std::string withIcon(const char* icon, const std::string& text);
