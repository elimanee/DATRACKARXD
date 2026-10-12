// Where the tracker keeps its files: settings, window layout, autosave.
// A datrackarxd.cfg in the current folder keeps everything there (portable
// use, and older versions); otherwise the user's settings folder is used.
#pragma once
#include <string>

std::string configDir();                         // ends with a separator
std::string configPath(const std::string& name);  // configDir() + name
