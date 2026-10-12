#include "paths.h"

#include <SDL.h>

#include <filesystem>

namespace fs = std::filesystem;

std::string configDir() {
  static std::string dir;
  if (!dir.empty()) return dir;
  std::error_code ec;
  if (fs::exists("datrackarxd.cfg", ec)) {
    dir = "./";
  } else if (char* p = SDL_GetPrefPath("DATRACKARXD", "datrackarxd")) {
    dir = p;
    SDL_free(p);
  } else {
    dir = "./";
  }
  return dir;
}

std::string configPath(const std::string& name) { return configDir() + name; }
