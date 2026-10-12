// Keyboard shortcuts that can be changed in the Settings window. Each
// action has one key (an SDL keycode, so it follows the keyboard layout)
// with Ctrl / Shift / Alt. Note entry and cursor movement stay fixed.
#pragma once
#include <SDL.h>

#include <string>

enum class Action {
  New, Open, Save, SaveAs,
  Undo, Redo, Cut, Copy, Paste, SelectAll, FindReplace, Interpolate,
  TransposeUp, TransposeDown, TransposeOctUp, TransposeOctDown,
  PlayStop, PlayFromCursor, PlaySong, PlayRow, Stop,
  RecordToggle, OctaveDown, OctaveUp,
  ClearSelection, Delete, InsertRow, DeleteRow,
  Settings, Manual,
  Count
};
constexpr int ACTION_COUNT = (int)Action::Count;

enum : uint8_t { MOD_CTRL = 1, MOD_SHIFT = 2, MOD_ALT = 4 };

struct KeyBinding {
  SDL_Keycode key = SDLK_UNKNOWN;  // SDLK_UNKNOWN = none
  uint8_t mods = 0;
  bool operator==(const KeyBinding& o) const { return key == o.key && mods == o.mods; }
};

struct Shortcuts {
  KeyBinding bind[ACTION_COUNT];
  Shortcuts();  // the defaults
  // The action bound to this key press, or Action::Count.
  Action match(SDL_Keycode key, uint16_t sdlMods) const;
  // Another action with the same key, or Action::Count.
  Action conflict(Action a) const;
};

const char* actionName(Action a);    // English, for the Settings window
const char* actionId(Action a);      // in the settings file: key.<id>=...
KeyBinding defaultBinding(Action a);
std::string bindingText(const KeyBinding& b);  // "Ctrl+Shift+S", "" if none
bool parseBinding(const std::string& text, KeyBinding& out);
uint8_t bindingMods(uint16_t sdlMods);
bool isModifierKey(SDL_Keycode key);
