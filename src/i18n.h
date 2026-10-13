// Interface language: English (the text in the code) or French. Strings
// are looked up by their English text; anything without a translation
// stays in English.
#pragma once
#include <string>

constexpr int LANGUAGE_COUNT = 2;
const char* languageName(int lang);  // in its own language
void setLanguage(int lang);           // 0 English, 1 French
int language();

// The translation of an English text (a string literal, kept as is).
const char* T(const char* en);
// Combo items ("A\0B\0"): the whole list is translated at once.
const char* trItems(const char* en, size_t size);
#define TC(s) trItems(s, sizeof(s))
// Window title: translated, with a fixed ID so the layout survives a
// language change ("Pattern" -> "Motif###Pattern").
std::string TW(const char* en);
