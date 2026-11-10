#include "ui_widgets.h"

#include <string>

#include "theme.h"

ImVec4 accentColor() {
  ImVec4 a = theme().accent;
  return ImVec4(a.x, a.y, a.z, 1.0f);
}

bool iconButton(const char* id, const char* icon, const char* text, bool on, ImVec2 size, const char* tooltip, ImU32 onColor) {
  std::string label = icon;
  if (text && *text) label += std::string("  ") + text;
  label += "##";
  label += id;
  int colors = 0;
  if (on) {
    ImVec4 c = onColor ? ImGui::ColorConvertU32ToFloat4(onColor) : accentColor();
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(c.x, c.y, c.z, 0.55f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(c.x, c.y, c.z, 0.75f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(c.x, c.y, c.z, 0.95f));
    colors = 3;
  }
  bool pressed = ImGui::Button(label.c_str(), size);
  ImGui::PopStyleColor(colors);
  if (tooltip && ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort)) {
    ImGui::PushFont(ImGui::GetIO().FontDefault);  // not the big icon font
    ImGui::SetTooltip("%s", tooltip);
    ImGui::PopFont();
  }
  return pressed;
}

std::string withIcon(const char* icon, const std::string& text) { return std::string(icon) + "  " + text; }
