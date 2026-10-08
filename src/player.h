// Standalone keygen-style player for an exported song (see payload.h).
#pragma once
#include "payload.h"

// Opens the player window. With "--selftest" it only loads the song and
// renders a few seconds of audio (used by CI), printing the result.
int runPlayer(const PlayerPayload& payload, int argc, char** argv);
