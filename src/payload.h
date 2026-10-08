// Keygen export: a copy of the program with a song (and the player's texts)
// appended. At startup the program looks for that payload and, if present,
// runs as a standalone player.
#pragma once
#include <string>

struct PlayerPayload {
  std::string song;  // .dtk text
  std::string logo, scroll, title;
};

// The payload of the running executable, if it has one.
bool readOwnPayload(PlayerPayload& out);
// Writes a copy of the running executable (without any payload of its own)
// with this payload appended, and makes it executable.
bool writePlayer(const std::string& path, const PlayerPayload& payload, std::string& err);
