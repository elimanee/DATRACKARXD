// The keygen-style intro scene: starfield, copper bars, a wavy logo and a
// sine scroller. Used by the Keygen window and the standalone player.
#pragma once
#include <imgui.h>

#include <cstdint>
#include <string>
#include <vector>

// How the scene looks; saved in exported players.
struct KeygenStyle {
  bool rainbow = true;         // rainbow logo and scroller, else one color
  uint32_t color = 0x00ffc8;   // 0xRRGGBB, used when not rainbow
  bool copper = true;          // copper bars
  bool stars = true;           // starfield
  float starSpeed = 1.0f;
  float scrollSpeed = 1.0f;
  float wave = 1.0f;           // height of the scroller's sine wave
  bool scope = true;           // oscilloscope strip (player)

  // "key=value" lines, and reading one back (false if the key is unknown).
  std::string toText() const;
  bool set(const std::string& key, const std::string& value);
};

class KeygenScene {
 public:
  std::string logo = "DATRACKARXD";
  std::string scroll;  // drawn in capitals, repeating
  KeygenStyle style;

  // Draws into [p0, p0 + size). level is the music's peak (0..1).
  void draw(ImDrawList* dl, ImVec2 p0, ImVec2 size, float time, float dt, float level);

 private:
  struct Star {
    float x, y, z;
  };
  std::vector<Star> stars_;
  float scrollPos_ = 0;
};
