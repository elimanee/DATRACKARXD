// The keygen-style intro scene: a background (starfield, plasma, tunnel or
// rotozoom), copper bars, a 3D vector object, the logo, two scrollers and VU
// meters, all moving with the music. Used by the Keygen window, the keygen
// maker and the standalone player.
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
  int background = 0;          // 0 starfield, 1 plasma, 2 tunnel, 3 rotozoom, 4 black
  bool cycle = false;          // go through the backgrounds, with fades
  float sceneTime = 12.0f;     // seconds per background when cycling
  int object = 0;              // 3D vector object: 0 none, 1 cube, 2 pyramid, 3 octahedron
  int logoFx = 0;              // 0 wave, 1 pulse with the music, 2 glitch
  int scrollFx = 0;            // 0 sine, 1 bounce, 2 roller
  bool vu = false;             // VU meters, one per channel
  bool flash = false;          // white flash on loud hits

  // "key=value" lines, and reading one back (false if the key is unknown).
  std::string toText() const;
  bool set(const std::string& key, const std::string& value);
};

class KeygenScene {
 public:
  std::string logo = "DATRACKARXD";
  std::string scroll;  // drawn in capitals, repeating
  std::string greets;  // second, smaller scroller at the top (empty = none)
  std::vector<float> levels;  // channel levels 0..1 for the VU meters, set by the caller
  KeygenStyle style;

  // Draws into [p0, p0 + size). level is the music's peak (0..1).
  void draw(ImDrawList* dl, ImVec2 p0, ImVec2 size, float time, float dt, float level);

 private:
  struct Star {
    float x, y, z;
  };
  std::vector<Star> stars_;
  float scrollPos_ = 0, greetsPos_ = 0;
  std::vector<float> vuShown_;
  float flash_ = 0, lastPeak_ = 0;
  void background(ImDrawList* dl, ImVec2 p0, ImVec2 p1, int kind, float t, float dt, float peak);
  void object(ImDrawList* dl, ImVec2 c, float size, float t, float peak);
};
