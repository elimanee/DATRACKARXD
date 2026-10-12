// A small ImGui file browser popup (open or save), with no dependency.
#pragma once
#include <string>
#include <vector>

class FileBrowser {
 public:
  std::string dir;  // current folder (starts at the working directory)

  // Opens the popup. exts filters the files shown ("" = files without one).
  void open(const std::string& title, bool saving, const std::vector<std::string>& exts, const std::string& name = "");
  // Call every frame; returns true once with the chosen path.
  bool draw(std::string& path);

 private:
  std::string title_;
  bool saving_ = false, request_ = false;
  std::vector<std::string> exts_;
  char name_[512] = {};
};
