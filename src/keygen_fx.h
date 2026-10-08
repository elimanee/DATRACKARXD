// The keygen-style intro scene: starfield, copper bars, a wavy logo and a
// sine scroller. Used by the Keygen window and the standalone player.
#pragma once
#include <imgui.h>

#include <string>
#include <vector>

class KeygenScene {
 public:
  std::string logo = "DATRACKARXD";
  std::string scroll;  // drawn in capitals, repeating

  // Draws into [p0, p0 + size). level is the music's peak (0..1).
  void draw(ImDrawList* dl, ImVec2 p0, ImVec2 size, float time, float dt, float level);

 private:
  struct Star {
    float x, y, z;
  };
  std::vector<Star> stars_;
};
