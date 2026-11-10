// The interface fonts: a modern one (Roboto) with a monospace one (Cousine)
// for the grids, or the classic pixel font everywhere. All carry the icons.
#pragma once
#include <imgui.h>

struct UiFonts {
  ImFont* ui = nullptr;     // labels and widgets (the default font)
  ImFont* mono = nullptr;   // pattern grid, hex numbers, anything in columns
  ImFont* big = nullptr;    // the transport bar's time and tempo display
  ImFont* caption = nullptr;  // small captions
  ImFont* pixel = nullptr;  // the keygen scenes (ProggyClean)
};

const UiFonts& uiFonts();
// Rebuilds the atlas for an interface scale (between ImGui frames).
void buildUiFonts(float scale, bool modern);
