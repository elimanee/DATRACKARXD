// Standalone keygen-style player for an exported song (see payload.h).
#pragma once
#include <string>

#include "payload.h"

// Opens the player window. With "--selftest" it only loads the song and
// renders a few seconds of audio (used by CI), printing the result.
int runPlayer(const PlayerPayload& payload, int argc, char** argv);

// One frame of the player: the scene, the oscilloscope strip (if the style
// wants it) and the info line. scope is a ring buffer of scopeLen values.
void drawPlayerScreen(ImDrawList* dl, ImVec2 p0, ImVec2 size, KeygenScene& scene, const float* scope, int scopeLen,
                      int scopePos, float t, float dt, const std::string& info);
