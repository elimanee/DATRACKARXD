// Audio file decoding for samples: WAV, FLAC, MP3 and Ogg Vorbis.
#pragma once
#include <string>

#include "song.h"

// Reads an audio file into a mono sample (stereo files are mixed down).
// Returns false and sets err on failure.
bool loadAudioSample(const std::string& path, Sample& out, std::string& err);

// True if the path has an extension loadAudioSample understands.
bool isAudioFile(const std::string& path);
