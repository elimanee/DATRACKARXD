// DATRACKARXD tests: song format, importers fed with broken files, zlib,
// engine output, module export round trip, translations and shortcuts.
// Run: datrackarxd-tests <songs folder> <temp folder>
#define SDL_MAIN_HANDLED
#include <SDL.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <random>
#include <set>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#include "demo.h"
#include "engine.h"
#include "i18n.h"
#include "import.h"
#include "midifile.h"
#include "modexport.h"
#include "shortcuts.h"
#include "vgmexport.h"
#include "song.h"

namespace fs = std::filesystem;

namespace {

int failures = 0, checks = 0;
std::string songsDir, tmpDir;

#define CHECK(cond)                                                          \
  do {                                                                       \
    checks++;                                                                \
    if (!(cond)) {                                                           \
      failures++;                                                            \
      std::printf("  FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);          \
    }                                                                        \
  } while (0)

std::vector<uint8_t> readFile(const std::string& path) {
  std::ifstream f(path, std::ios::binary);
  return std::vector<uint8_t>(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
}

void writeFile(const std::string& path, const std::vector<uint8_t>& data) {
  std::ofstream f(path, std::ios::binary | std::ios::trunc);
  f.write((const char*)data.data(), (std::streamsize)data.size());
}

bool loadSong(const std::string& name, Song& s) {
  std::string err;
  bool ok = loadAnySong(songsDir + "/" + name, s, err);
  if (!ok) std::printf("  cannot load %s: %s\n", name.c_str(), err.c_str());
  return ok;
}

// --- tests ---

void songFormatRoundTrip() {
  for (const char* name : {"demo.dtk", "chips.dtk", "consoles.dtk"}) {
    Song a;
    CHECK(loadSong(name, a));
    std::string t1 = a.toText();
    Song b;
    std::string err;
    CHECK(b.fromText(t1, err));
    CHECK(b.toText() == t1);
  }
  Song demo;
  loadDemoSong(demo);
  std::string err;
  std::string path = tmpDir + "/save.dtk";
  CHECK(demo.save(path, err));
  CHECK(!fs::exists(path + ".tmp"));
  Song back;
  CHECK(loadAnySong(path, back, err));
  CHECK(back.toText() == demo.toText());
  // Broken text never crashes and is refused or loaded.
  std::mt19937 rng(1);
  std::string text = demo.toText();
  for (int i = 0; i < 300; i++) {
    std::string t = text.substr(0, rng() % text.size());
    for (int k = 0; k < 4 && !t.empty(); k++) t[rng() % t.size()] ^= (char)(1 << (rng() % 7));
    Song s;
    s.fromText(t, err);
  }
}

void engineOutput() {
  for (const char* name : {"chips.dtk", "consoles.dtk"}) {
  Song s;
  CHECK(loadSong(name, s));
  auto render = [&] {
    Engine e(s, 44100);
    e.play(0, 0);
    std::vector<float> buf(44100 * 2 * 3);
    for (size_t i = 0; i < buf.size(); i += 2048) e.render(buf.data() + i, (int)std::min<size_t>(1024, (buf.size() - i) / 2));
    return buf;
  };
  std::vector<float> a = render(), b = render();
  CHECK(a == b);  // deterministic
  bool finite = true;
  float peak = 0;
  for (float v : a) {
    finite &= std::isfinite(v);
    peak = std::max(peak, std::abs(v));
  }
  CHECK(finite);
  CHECK(peak > 0.01f && peak < 4.0f);
  }
}

// The emulated chips: every type makes sound at the note's pitch, and the
// VGM export declares the chips the song uses.
void chipsAndVgm() {
  for (int type = INS_AY; type < INS_TYPE_COUNT; type++) {
    Song s;
    s.reset(1);
    s.instruments.assign(1, Instrument());
    Instrument& ins = s.instruments[0];
    ins.type = type;
    ins.volume = 15;
    if (type == INS_GB || type == INS_SCC || type == INS_PCE)
      for (int i = 0; i < 32; i++) ins.wavetable[i] = i < 16 ? 15 : 0;
    ins.chip.gbEnvLen = 0;  // no fade
    // FM chips: one sine carrier, so the zero crossings give the pitch.
    ins.opl.alg = 1;
    ins.opl.ops[0].tl = 63;
    ins.opl.ops[1] = OPLOperator();
    ins.fm.alg = 7;
    for (int k = 0; k < 3; k++) ins.fm.ops[k].enabled = false;
    ins.fm.ops[3] = FMOperator();
    Cell* c = s.cell(0, 0, 0, true);
    c->note = 57;  // A-4, 440 Hz
    c->ins = 0;
    Engine e(s, 44100);
    e.play(0, 0);
    std::vector<float> buf(44100 * 2 / 2);
    e.render(buf.data(), (int)buf.size() / 2);
    // Zero crossings of the second half (after the attack) give the pitch.
    int cross = 0;
    size_t half = buf.size() / 4;
    double mean = 0;
    for (size_t i = half; i < buf.size() / 2; i++) mean += buf[i * 2];
    mean /= (double)(buf.size() / 2 - half);
    for (size_t i = half + 1; i < buf.size() / 2; i++) cross += (buf[(i - 1) * 2] < mean) != (buf[i * 2] < mean);
    double hz = cross / 2.0 / ((buf.size() / 2 - half) / 44100.0);
    bool ok = std::abs(hz - 440) < 440 * 0.03;
    if (!ok) std::printf("  type %d plays %.1f Hz instead of 440\n", type, hz);
    CHECK(ok);
  }
  Song s;
  CHECK(loadSong("consoles.dtk", s));
  std::string err, report, path = tmpDir + "/consoles.vgm";
  CHECK(exportVGM(s, path, err, report));
  std::vector<uint8_t> v = readFile(path);
  auto u32 = [&](size_t at) { return v.size() >= at + 4 ? (uint32_t)(v[at] | v[at + 1] << 8 | v[at + 2] << 16 | (uint32_t)v[at + 3] << 24) : 0u; };
  CHECK(v.size() > 0x100 && v[0] == 'V' && v[1] == 'g' && v[2] == 'm');
  CHECK(u32(0x2C) != 0);  // YM2612
  CHECK(u32(0x84) != 0);  // NES APU
  CHECK(u32(0x9C) != 0);  // K051649
  CHECK(u32(0xA4) != 0);  // HuC6280
}

void zlibRoundTrip() {
  std::mt19937 rng(2);
  for (int i = 0; i < 60; i++) {
    std::vector<uint8_t> in(rng() % 70000);
    int kind = i % 3;
    for (size_t k = 0; k < in.size(); k++)
      in[k] = kind == 0 ? (uint8_t)rng() : kind == 1 ? (uint8_t)("abcab"[k % 5]) : (uint8_t)(rng() % 4);
    std::vector<uint8_t> packed, out;
    zlibDeflate(in.data(), in.size(), packed);
    CHECK(zlibInflate(packed.data(), packed.size(), out));
    CHECK(out == in);
    // Damaged streams are refused or decoded, never a crash.
    for (int k = 0; k < 20 && !packed.empty(); k++) {
      std::vector<uint8_t> bad = packed;
      bad[rng() % bad.size()] ^= (uint8_t)(1 << (rng() % 8));
      bad.resize(rng() % (bad.size() + 1));
      std::vector<uint8_t> o;
      zlibInflate(bad.data(), bad.size(), o);
    }
  }
}

void moduleRoundTrip() {
  Song s;
  CHECK(loadSong("chips.dtk", s));
  for (const char* ext : {"xm", "mod", "it"}) {
    std::string path = tmpDir + "/rt." + ext, err, report;
    ModuleExportOptions opt;
    opt.format = moduleFormatFromPath(path);
    CHECK(exportModule(s, path, opt, err, report));
    Song back;
    CHECK(loadAnySong(path, back, err));
    CHECK(back.channelCount() == s.channelCount());
    CHECK(back.orders.size() == s.orders.size());
    // The notes come back (count of note cells in the first order).
    auto notes = [](const Song& x) {
      int n = 0;
      for (int c = 0; c < x.channelCount(); c++)
        for (int r = 0; r < x.patternLength; r++) {
          const Cell* cell = x.cell(c, 0, r);
          if (cell && cell->note >= 0) n++;
        }
      return n;
    };
    CHECK(notes(back) > 0);
  }
}

// Every importer gets truncated and damaged versions of real files.
void importerFuzz() {
  Song s;
  CHECK(loadSong("chips.dtk", s));
  std::string err, report;
  std::vector<std::pair<std::string, std::vector<uint8_t>>> seeds;
  for (const char* ext : {"xm", "mod", "it"}) {
    std::string path = tmpDir + "/seed." + ext;
    ModuleExportOptions opt;
    opt.format = moduleFormatFromPath(path);
    opt.bakeRate = 11025;
    opt.holdSeconds = 0.25f;
    exportModule(s, path, opt, err, report);
    seeds.push_back({ext, readFile(path)});
  }
  exportMIDI(s, tmpDir + "/seed.mid", 1, err);
  seeds.push_back({"mid", readFile(tmpDir + "/seed.mid")});
  std::string text = s.toText();
  seeds.push_back({"dtk", std::vector<uint8_t>(text.begin(), text.end())});
  // Formats without a writer: just their magic and noise, to reach the parsers.
  auto magic = [](const std::string& m, size_t n) {
    std::vector<uint8_t> v(m.begin(), m.end());
    std::mt19937 r(7);
    while (v.size() < n) v.push_back((uint8_t)(r() % 4 == 0 ? r() : 0));
    return v;
  };
  seeds.push_back({"fur", magic("-Furnace module-", 4000)});
  seeds.push_back({"dmf", magic(".DelekDefleMask.", 4000)});
  seeds.push_back({"ftm", magic("FamiTracker Module", 4000)});
  seeds.push_back({"s3m", magic(std::string(44, 'x') + "SCRM", 4000)});
  std::vector<uint8_t> z;
  std::string furText = "-Furnace module-" + std::string(200, '\0');
  zlibDeflate((const uint8_t*)furText.data(), furText.size(), z);
  seeds.push_back({"fur", z});

  std::mt19937 rng(3);
  int runs = 0;
  for (auto& [ext, data] : seeds) {
    if (data.empty()) continue;
    std::string path = tmpDir + "/fuzz." + ext;
    Uint64 t0 = SDL_GetTicks64();
    double worst = 0;
    int worstI = -1;
    for (int i = 0; i < 400; i++) {
      Uint64 t1 = SDL_GetTicks64();
      std::vector<uint8_t> d = data;
      int mode = i % 4;
      if (mode == 0) d.resize(rng() % d.size());
      if (mode >= 1)
        for (int k = 0, n = 1 + (int)(rng() % 16); k < n; k++) d[rng() % d.size()] = (uint8_t)(mode == 2 ? 0xff : rng());
      if (mode == 3) d.resize(d.size() - rng() % (d.size() / 2 + 1));
      writeFile(path, d);
      Song out;
      loadAnySong(path, out, err);
      runs++;
      double dt = (SDL_GetTicks64() - t1) / 1000.0;
      if (dt > worst) worst = dt, worstI = i;
    }
    std::printf("  %s: %.1f s (slowest %d: %.2f s)\n", ext.c_str(), (SDL_GetTicks64() - t0) / 1000.0, worstI, worst);
  }
  std::printf("  %d damaged files read\n", runs);
  CHECK(runs > 2000);
}

// The French table: same printf specifiers, Latin-1 only, no duplicates.
struct TrEntry {
  std::string_view en;
  const char* fr;
};
#define TR(en, fr) {std::string_view(en, sizeof(en) - 1), fr}
const TrEntry FRENCH[] = {
#include "lang_fr.inc"
};
#undef TR

std::vector<std::string> specifiers(std::string_view s) {
  std::vector<std::string> out;
  for (size_t i = 0; i < s.size(); i++) {
    if (s[i] != '%') continue;
    size_t j = i + 1;
    while (j < s.size() && std::strchr("-+ #0123456789.*l", s[j])) j++;
    if (j < s.size() && std::strchr("diouxXeEfgGcsp%", s[j])) {
      out.push_back(std::string(s.substr(i, j - i + 1)));
      i = j;
    }
  }
  return out;
}

void translations() {
  std::set<std::string_view> seen;
  for (const TrEntry& e : FRENCH) {
    std::string_view fr(e.fr);
    bool unique = seen.insert(e.en).second;
    if (!unique) std::printf("  duplicate key: %.*s\n", (int)e.en.size(), e.en.data());
    CHECK(unique);
    bool same = specifiers(e.en) == specifiers(fr);
    if (!same) std::printf("  format mismatch: \"%.*s\"\n", (int)e.en.size(), e.en.data());
    CHECK(same);
    // Item lists keep their item count.
    if (e.en.find('\0') != std::string_view::npos) {
      size_t n = std::count(e.en.begin(), e.en.end(), '\0');
      const char* p = e.fr;
      size_t m = 0;
      while (*p) {
        p += std::strlen(p) + 1;
        m++;
      }
      CHECK(n == m);
    }
    // UTF-8 limited to Latin-1 (the font has nothing else).
    bool latin1 = true;
    for (size_t i = 0; i < fr.size(); i++) {
      unsigned char c = (unsigned char)fr[i];
      if (c >= 0x80) {
        latin1 &= (c == 0xC2 || c == 0xC3) && i + 1 < fr.size();
        i++;
      }
    }
    if (!latin1) std::printf("  not Latin-1: \"%s\"\n", e.fr);
    CHECK(latin1);
  }
  setLanguage(1);
  CHECK(std::string(T("Settings")) == "Réglages");
  CHECK(std::string(T("no such text, stays as is")) == "no such text, stays as is");
  CHECK(TW("Settings") == "Réglages###Settings");
  setLanguage(0);
  CHECK(std::string(T("Settings")) == "Settings");
}

void shortcuts() {
  Shortcuts sc;
  for (int i = 0; i < ACTION_COUNT; i++) {
    CHECK(sc.conflict((Action)i) == Action::Count);  // the defaults never clash
    KeyBinding b;
    std::string text = bindingText(sc.bind[i]);
    CHECK(parseBinding(text, b));
    CHECK(b == sc.bind[i]);
    CHECK(std::strlen(actionId((Action)i)) > 0);
  }
  KeyBinding b;
  CHECK(parseBinding("Ctrl+Shift+Keypad +", b) && b.key == SDLK_KP_PLUS && b.mods == (MOD_CTRL | MOD_SHIFT));
  CHECK(!parseBinding("Ctrl+NoSuchKey", b));
  CHECK(sc.match(SDLK_s, KMOD_LCTRL) == Action::Save);
  CHECK(sc.match(SDLK_s, KMOD_LCTRL | KMOD_LSHIFT) == Action::SaveAs);
  CHECK(sc.match(SDLK_q, 0) == Action::Count);
}

}  // namespace

int main(int argc, char** argv) {
  songsDir = argc > 1 ? argv[1] : "songs";
  tmpDir = argc > 2 ? argv[2] : (fs::temp_directory_path() / "datrackarxd-tests").string();
  fs::create_directories(tmpDir);
  struct Test {
    const char* name;
    std::function<void()> run;
  } tests[] = {
      {"song format", songFormatRoundTrip}, {"engine output", engineOutput},     {"zlib", zlibRoundTrip},
      {"chips and VGM", chipsAndVgm},
      {"module round trip", moduleRoundTrip}, {"importers vs. broken files", importerFuzz},
      {"translations", translations},       {"shortcuts", shortcuts},
  };
  for (const Test& t : tests) {
    int before = failures;
    std::printf("%s\n", t.name);
    Uint64 t0 = SDL_GetTicks64();
    t.run();
    std::printf("  %s (%.1f s)\n", failures == before ? "ok" : "FAILED", (SDL_GetTicks64() - t0) / 1000.0);
  }
  std::printf("%d checks, %d failed\n", checks, failures);
  return failures ? 1 : 0;
}
