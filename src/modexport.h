// Export to classic tracker modules (XM, MOD, IT). Sample instruments keep
// their samples; the others (chip, FM, emulated chips) are rendered into
// samples, with a loop when the note sustains. Each order becomes a module
// pattern (identical ones are shared), and the effects are translated where
// the format has an equivalent; the effects that keep running until stopped
// here are repeated row by row, as the classic trackers need.
#pragma once
#include <string>

#include "song.h"

enum class ModuleFormat { XM, MOD, IT };

struct ModuleExportOptions {
  ModuleFormat format = ModuleFormat::XM;
  int bakeRate = 22050;      // sample rate of rendered instruments (MOD resamples everything to 8363 Hz)
  float holdSeconds = 1.0f;  // how long a rendered note is held; longer notes loop its end
};

// report: what could not be converted, one line each (empty if everything made it).
bool exportModule(const Song& song, const std::string& path, const ModuleExportOptions& opt, std::string& err,
                  std::string& report);

// XM, MOD or IT from the file's extension (XM if unknown).
ModuleFormat moduleFormatFromPath(const std::string& path);
