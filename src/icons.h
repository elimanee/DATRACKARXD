// Icons drawn by the program into the font atlas (Unicode private use area,
// U+E000 and up), so they can sit in any label: ICON_PLAY " Play".
#pragma once
#include <imgui.h>

// The UTF-8 text of each icon, to paste into labels.
#define ICON_PLAY "\xee\x80\x80"
#define ICON_PAUSE "\xee\x80\x81"
#define ICON_STOP "\xee\x80\x82"
#define ICON_RECORD "\xee\x80\x83"
#define ICON_LOOP "\xee\x80\x84"
#define ICON_FOLLOW "\xee\x80\x85"
#define ICON_METRONOME "\xee\x80\x86"
#define ICON_PLAY_ROW "\xee\x80\x87"
#define ICON_TO_START "\xee\x80\x88"
#define ICON_FOLDER "\xee\x80\x89"
#define ICON_FILE "\xee\x80\x8a"
#define ICON_NOTE "\xee\x80\x8b"
#define ICON_WAVE "\xee\x80\x8c"
#define ICON_PIANO "\xee\x80\x8d"
#define ICON_PLUS "\xee\x80\x8e"
#define ICON_MINUS "\xee\x80\x8f"
#define ICON_TRASH "\xee\x80\x90"
#define ICON_COPY "\xee\x80\x91"
#define ICON_SAVE "\xee\x80\x92"
#define ICON_OPEN "\xee\x80\x93"
#define ICON_UNDO "\xee\x80\x94"
#define ICON_REDO "\xee\x80\x95"
#define ICON_SEARCH "\xee\x80\x96"
#define ICON_GEAR "\xee\x80\x97"
#define ICON_UP "\xee\x80\x98"
#define ICON_DOWN "\xee\x80\x99"
#define ICON_HOME "\xee\x80\x9a"
#define ICON_STAR "\xee\x80\x9b"
#define ICON_SPEAKER "\xee\x80\x9c"
#define ICON_SLIDERS "\xee\x80\x9d"
#define ICON_SCOPE "\xee\x80\x9e"
#define ICON_CHIP "\xee\x80\x9f"
#define ICON_EXPORT "\xee\x80\xa0"
#define ICON_GRID "\xee\x80\xa1"
#define ICON_LIST "\xee\x80\xa2"
#define ICON_BOOK "\xee\x80\xa3"
#define ICON_REFRESH "\xee\x80\xa4"
#define ICON_CLOSE "\xee\x80\xa5"
#define ICON_RIGHT "\xee\x80\xa6"
#define ICON_DOWN_SMALL "\xee\x80\xa7"
#define ICON_CLONE "\xee\x80\xa8"
#define ICON_SPARK "\xee\x80\xa9"
#define ICON_TIMELINE "\xee\x80\xaa"
#define ICON_DRUM "\xee\x80\xab"
#define ICON_SONG "\xee\x80\xac"
#define ICON_FX "\xee\x80\xad"
#define ICON_KEYBOARD "\xee\x80\xae"
#define ICON_INFO "\xee\x80\xaf"

constexpr ImWchar ICON_FIRST = 0xE000;
constexpr int ICON_COUNT = 48;

// Reserves the icon glyphs in a font (before the atlas is built).
void addIconGlyphs(ImFontAtlas* atlas, ImFont* font, float size);
// Draws the reserved glyphs into the atlas (after it is built).
void drawIconGlyphs(ImFontAtlas* atlas);
