#include "filebrowser.h"

#include <imgui.h>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <filesystem>

namespace fs = std::filesystem;

void FileBrowser::open(const std::string& title, bool saving, const std::vector<std::string>& exts, const std::string& name) {
  title_ = title;
  saving_ = saving;
  exts_ = exts;
  request_ = true;
  std::snprintf(name_, sizeof(name_), "%s", name.c_str());
  std::error_code ec;
  if (dir.empty() || !fs::is_directory(dir, ec)) dir = fs::current_path(ec).string();
}

bool FileBrowser::draw(std::string& path) {
  std::string id = title_ + "###filebrowser";
  if (request_) {
    ImGui::OpenPopup(id.c_str());
    request_ = false;
  }
  ImGui::SetNextWindowSize(ImVec2(560, 420), ImGuiCond_Appearing);
  if (!ImGui::BeginPopupModal(id.c_str(), nullptr)) return false;

  auto matches = [&](const fs::path& p) {
    if (exts_.empty()) return true;
    std::string e = p.extension().string();
    std::transform(e.begin(), e.end(), e.begin(), ::tolower);
    for (const auto& x : exts_)
      if (e == x) return true;
    return false;
  };
  ImGui::TextUnformatted(dir.c_str());
  if (ImGui::Button("Up")) {
    fs::path p = fs::path(dir).parent_path();
    if (!p.empty()) dir = p.string();
  }
  if (!exts_.empty()) {
    std::string list;
    for (const auto& x : exts_) list += x.empty() ? " (no extension)" : " *" + x;
    ImGui::SameLine();
    ImGui::TextDisabled("(folders and%s)", list.c_str());
  }

  bool accept = false;
  ImGui::BeginChild("files", ImVec2(0, -ImGui::GetFrameHeightWithSpacing() * 2.2f), ImGuiChildFlags_Borders);
  std::vector<fs::directory_entry> dirs, files;
  std::error_code ec;
  for (fs::directory_iterator it(dir, ec), end; !ec && it != end; it.increment(ec)) {
    std::error_code ec2;
    if (it->is_directory(ec2)) dirs.push_back(*it);
    else if (matches(it->path())) files.push_back(*it);
  }
  auto byName = [](const fs::directory_entry& a, const fs::directory_entry& b) { return a.path().filename() < b.path().filename(); };
  std::sort(dirs.begin(), dirs.end(), byName);
  std::sort(files.begin(), files.end(), byName);
  std::string newDir;
  for (auto& d : dirs) {
    std::string label = "[" + d.path().filename().string() + "]";
    if (ImGui::Selectable(label.c_str(), false, ImGuiSelectableFlags_AllowDoubleClick) && ImGui::IsMouseDoubleClicked(0))
      newDir = d.path().string();
  }
  for (auto& f : files) {
    std::string n = f.path().filename().string();
    if (ImGui::Selectable(n.c_str(), n == name_, ImGuiSelectableFlags_AllowDoubleClick)) {
      std::snprintf(name_, sizeof(name_), "%s", n.c_str());
      if (ImGui::IsMouseDoubleClicked(0)) accept = true;
    }
  }
  ImGui::EndChild();
  if (!newDir.empty()) dir = newDir;

  ImGui::SetNextItemWidth(-230);
  if (ImGui::IsWindowAppearing()) ImGui::SetKeyboardFocusHere();
  if (ImGui::InputText("##name", name_, sizeof(name_), ImGuiInputTextFlags_EnterReturnsTrue)) accept = true;
  ImGui::SameLine();
  if (ImGui::Button(saving_ ? "Save" : "Open", ImVec2(100, 0))) accept = true;
  ImGui::SameLine();
  bool cancel = ImGui::Button("Cancel", ImVec2(100, 0)) || ImGui::IsKeyPressed(ImGuiKey_Escape);

  bool done = false;
  if (accept && name_[0]) {
    fs::path p = fs::path(name_);
    if (p.is_relative()) p = fs::path(dir) / p;
    if (saving_ && !exts_.empty() && !exts_[0].empty() && p.extension() != exts_[0]) p += exts_[0];
    path = p.string();
    done = true;
    cancel = true;
  }
  if (cancel) ImGui::CloseCurrentPopup();
  ImGui::EndPopup();
  return done;
}
