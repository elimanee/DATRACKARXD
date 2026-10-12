#include "i18n.h"

#include <string_view>
#include <unordered_map>

namespace {

struct Entry {
  std::string_view en;
  const char* fr;
};

// sizeof keeps the "\0" separators of combo item lists.
#define TR(en, fr) {std::string_view(en, sizeof(en) - 1), fr}
const Entry FRENCH[] = {
#include "lang_fr.inc"
};
#undef TR

int current = 0;

const std::unordered_map<std::string_view, const char*>& frenchMap() {
  static std::unordered_map<std::string_view, const char*> m = [] {
    std::unordered_map<std::string_view, const char*> out;
    for (const Entry& e : FRENCH) out.emplace(e.en, e.fr);
    return out;
  }();
  return m;
}

const char* lookup(std::string_view en, const char* fallback) {
  if (current != 1) return fallback;
  const auto& m = frenchMap();
  auto it = m.find(en);
  return it == m.end() ? fallback : it->second;
}

}  // namespace

const char* languageName(int lang) { return lang == 1 ? "Français" : "English"; }
void setLanguage(int lang) { current = lang == 1 ? 1 : 0; }
int language() { return current; }

const char* T(const char* en) { return en ? lookup(en, en) : en; }

const char* trItems(const char* en, size_t size) { return lookup(std::string_view(en, size - 1), en); }

std::string TW(const char* en) { return std::string(T(en)) + "###" + en; }
