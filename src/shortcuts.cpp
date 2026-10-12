#include "shortcuts.h"

#include <cstring>

namespace {

struct ActionInfo {
  const char* id;
  const char* name;
  KeyBinding def;
};

const ActionInfo INFO[ACTION_COUNT] = {
    {"new", "New song", {SDLK_n, MOD_CTRL}},
    {"open", "Open / import", {SDLK_o, MOD_CTRL}},
    {"save", "Save", {SDLK_s, MOD_CTRL}},
    {"saveas", "Save as", {SDLK_s, MOD_CTRL | MOD_SHIFT}},
    {"undo", "Undo", {SDLK_z, MOD_CTRL}},
    {"redo", "Redo", {SDLK_y, MOD_CTRL}},
    {"cut", "Cut", {SDLK_x, MOD_CTRL}},
    {"copy", "Copy", {SDLK_c, MOD_CTRL}},
    {"paste", "Paste", {SDLK_v, MOD_CTRL}},
    {"selectall", "Select channel / all", {SDLK_a, MOD_CTRL}},
    {"find", "Find / replace", {SDLK_h, MOD_CTRL}},
    {"interpolate", "Interpolate", {SDLK_i, MOD_CTRL}},
    {"transposeup", "Transpose +1", {SDLK_UP, MOD_CTRL}},
    {"transposedown", "Transpose -1", {SDLK_DOWN, MOD_CTRL}},
    {"transposeoctup", "Transpose +12", {SDLK_UP, MOD_CTRL | MOD_SHIFT}},
    {"transposeoctdown", "Transpose -12", {SDLK_DOWN, MOD_CTRL | MOD_SHIFT}},
    {"play", "Play / stop", {SDLK_RETURN, 0}},
    {"playcursor", "Play / stop from the cursor row", {SDLK_RETURN, MOD_SHIFT}},
    {"playsong", "Play the song from the start", {SDLK_F5, 0}},
    {"playrow", "Restart from the cursor row", {SDLK_F6, 0}},
    {"stop", "Stop", {SDLK_F8, 0}},
    {"record", "Record mode on / off", {SDLK_SPACE, 0}},
    {"octavedown", "Octave down", {SDLK_KP_DIVIDE, 0}},
    {"octaveup", "Octave up", {SDLK_KP_MULTIPLY, 0}},
    {"deselect", "Clear the selection", {SDLK_ESCAPE, 0}},
    {"delete", "Delete", {SDLK_DELETE, 0}},
    {"insertrow", "Insert a row", {SDLK_INSERT, 0}},
    {"deleterow", "Delete the row above", {SDLK_BACKSPACE, 0}},
    {"settings", "Settings", {SDLK_COMMA, MOD_CTRL}},
    {"manual", "Manual", {SDLK_F1, 0}},
};

}  // namespace

Shortcuts::Shortcuts() {
  for (int i = 0; i < ACTION_COUNT; i++) bind[i] = INFO[i].def;
}

Action Shortcuts::match(SDL_Keycode key, uint16_t sdlMods) const {
  KeyBinding b{key, bindingMods(sdlMods)};
  if (key == SDLK_UNKNOWN) return Action::Count;
  for (int i = 0; i < ACTION_COUNT; i++)
    if (bind[i] == b) return (Action)i;
  return Action::Count;
}

Action Shortcuts::conflict(Action a) const {
  const KeyBinding& b = bind[(int)a];
  if (b.key == SDLK_UNKNOWN) return Action::Count;
  for (int i = 0; i < ACTION_COUNT; i++)
    if (i != (int)a && bind[i] == b) return (Action)i;
  return Action::Count;
}

const char* actionName(Action a) { return (int)a < ACTION_COUNT ? INFO[(int)a].name : ""; }
const char* actionId(Action a) { return (int)a < ACTION_COUNT ? INFO[(int)a].id : ""; }
KeyBinding defaultBinding(Action a) { return (int)a < ACTION_COUNT ? INFO[(int)a].def : KeyBinding{}; }

uint8_t bindingMods(uint16_t m) {
  uint8_t out = 0;
  // Cmd works like Ctrl on macOS.
#ifdef __APPLE__
  if (m & (KMOD_CTRL | KMOD_GUI)) out |= MOD_CTRL;
#else
  if (m & KMOD_CTRL) out |= MOD_CTRL;
#endif
  if (m & KMOD_SHIFT) out |= MOD_SHIFT;
  if (m & KMOD_ALT) out |= MOD_ALT;
  return out;
}

bool isModifierKey(SDL_Keycode k) {
  return k == SDLK_LCTRL || k == SDLK_RCTRL || k == SDLK_LSHIFT || k == SDLK_RSHIFT || k == SDLK_LALT || k == SDLK_RALT ||
         k == SDLK_LGUI || k == SDLK_RGUI || k == SDLK_MODE;
}

std::string bindingText(const KeyBinding& b) {
  if (b.key == SDLK_UNKNOWN) return "";
  std::string s;
  if (b.mods & MOD_CTRL) s += "Ctrl+";
  if (b.mods & MOD_SHIFT) s += "Shift+";
  if (b.mods & MOD_ALT) s += "Alt+";
  const char* name = SDL_GetKeyName(b.key);
  s += name && *name ? name : "?";
  return s;
}

bool parseBinding(const std::string& text, KeyBinding& out) {
  out = KeyBinding{};
  if (text.empty()) return true;  // no key
  std::string rest = text;
  uint8_t mods = 0;
  // Modifiers come first; the key name itself may contain '+' ("Keypad +").
  for (;;) {
    if (rest.rfind("Ctrl+", 0) == 0 && rest.size() > 5) mods |= MOD_CTRL, rest.erase(0, 5);
    else if (rest.rfind("Shift+", 0) == 0 && rest.size() > 6) mods |= MOD_SHIFT, rest.erase(0, 6);
    else if (rest.rfind("Alt+", 0) == 0 && rest.size() > 4) mods |= MOD_ALT, rest.erase(0, 4);
    else break;
  }
  SDL_Keycode k = SDL_GetKeyFromName(rest.c_str());
  if (k == SDLK_UNKNOWN) return false;
  out.key = k;
  out.mods = mods;
  return true;
}
