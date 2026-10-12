// VGM export: the emulated chips' register writes, tick by tick, in the
// standard video game music log format. Channels playing AY / YM2149,
// SN76489, Game Boy or OPL3 instruments get a voice on that chip (two AY
// chips, one of each other); the other channels are left out.
#pragma once
#include <string>

#include "song.h"

// report: what was left out, one line each (empty if everything made it).
bool exportVGM(const Song& song, const std::string& path, std::string& err, std::string& report);
