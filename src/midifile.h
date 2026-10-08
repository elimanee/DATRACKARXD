// Standard MIDI file (.mid) import and export.
#pragma once
#include <cstdint>
#include <string>
#include <vector>

#include "song.h"

// Notes are quantized to rows (16ths, or 32nds when needed); each MIDI part
// gets chip instruments and as many channels as it has simultaneous notes.
bool importMIDI(const std::vector<uint8_t>& data, Song& song, std::string& err);
// Plays the song through the engine and writes one track per channel.
bool exportMIDI(const Song& song, const std::string& path, int loops, std::string& err);
