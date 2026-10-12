// DATRACKARXD Keygen: a separate app that turns a song into a standalone
// keygen-style player. Open a song (any format the tracker reads), set the
// logo, scroller text and look with a live preview, and export.
//
// The exported player is a copy of this program with the song appended, so
// started with a payload it simply plays (see payload.h).
#include <SDL.h>
#include <imgui.h>
#include <imgui_impl_sdl2.h>
#include <imgui_impl_sdlrenderer2.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <mutex>
#include <string>

#include "demo.h"
#include "engine.h"
#include "filebrowser.h"
#include "import.h"
#include "keygen_fx.h"
#include "payload.h"
#include "player.h"
#include "theme.h"

namespace fs = std::filesystem;

namespace {

void audioCallback(void* user, Uint8* stream, int len) {
  Engine* engine = static_cast<Engine*>(user);
  std::lock_guard<std::mutex> lock(engine->mutex);
  engine->render(reinterpret_cast<float*>(stream), len / (int)(2 * sizeof(float)));
}

std::string defaultScroll(const Song& s) {
  return "Now playing: " + s.name + (s.author.empty() ? "" : "  by " + s.author) +
         "   ***   made with DATRACKARXD   ***   greetings to all trackers";
}

std::string playerFileName(const Song& s) {
  std::string n;
  for (char c : s.name) n += std::isalnum((unsigned char)c) ? c : '_';
  if (n.empty()) n = "player";
#ifdef _WIN32
  n += ".exe";
#endif
  return n;
}

class Maker {
 public:
  Maker() : engine_(song_, 44100) {
    loadDemoSong(song_);
    songPath_ = "(demo song)";
    resetTexts();
    engine_.play(0, 0);
  }

  Engine& engine() { return engine_; }

  void openAudio() {
    SDL_AudioSpec want{}, have{};
    want.freq = 44100;
    want.format = AUDIO_F32SYS;
    want.channels = 2;
    want.samples = 512;
    want.callback = audioCallback;
    want.userdata = &engine_;
    dev_ = SDL_OpenAudioDevice(nullptr, 0, &want, &have, 0);
    if (dev_) SDL_PauseAudioDevice(dev_, 0);
    else setStatus(std::string("No audio: ") + SDL_GetError());
  }

  void closeAudio() {
    if (dev_) SDL_CloseAudioDevice(dev_);
    dev_ = 0;
  }

  // Called with engine().mutex held.
  void load(const std::string& path) {
    Song s;
    std::string err;
    if (!loadAnySong(path, s, err)) {
      setStatus("Cannot open " + path + ": " + err);
      return;
    }
    bool autoScroll = scroll_ == defaultScroll(song_);
    song_ = std::move(s);
    songPath_ = path;
    engine_.reset();
    engine_.play(0, 0);
    playing_ = true;
    std::snprintf(title_, sizeof(title_), "%s", song_.name.c_str());
    if (autoScroll) setScroll(defaultScroll(song_));
    setStatus("Loaded " + fs::path(path).filename().string());
  }

  void frame() {
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(vp->Pos);
    ImGui::SetNextWindowSize(vp->Size);
    ImGui::Begin("maker", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
                                       ImGuiWindowFlags_NoBringToFrontOnFocus);
    float panelW = std::min(380.0f, vp->Size.x * 0.45f);
    ImGui::BeginChild("panel", ImVec2(panelW, 0), ImGuiChildFlags_Borders);
    panel();
    ImGui::EndChild();
    ImGui::SameLine();
    ImGui::BeginChild("preview", ImVec2(0, 0), ImGuiChildFlags_Borders, ImGuiWindowFlags_NoScrollbar);
    preview();
    ImGui::EndChild();
    ImGui::End();

    std::string path;
    if (browser_.draw(path)) {
      if (browserMode_ == 0) load(path);
      else exportTo(path);
    }
  }

 private:
  Song song_;
  Engine engine_;
  SDL_AudioDeviceID dev_ = 0;
  KeygenScene scene_;
  std::string songPath_;
  char logo_[64] = "DATRACKARXD";
  char title_[128] = {};
  char scroll_[2048] = {};
  bool playing_ = true;
  FileBrowser browser_;
  int browserMode_ = 0;  // 0 open song, 1 export
  std::string status_;
  double statusTime_ = -100;

  void setStatus(const std::string& s) {
    status_ = s;
    statusTime_ = ImGui::GetCurrentContext() ? ImGui::GetTime() : 0;
  }
  void setScroll(const std::string& s) { std::snprintf(scroll_, sizeof(scroll_), "%s", s.c_str()); }
  void resetTexts() {
    std::snprintf(title_, sizeof(title_), "%s", song_.name.c_str());
    setScroll(defaultScroll(song_));
  }

  void exportTo(const std::string& path) {
    PlayerPayload p;
    p.song = song_.toText();
    p.logo = logo_;
    p.title = title_;
    p.scroll = scroll_;
    p.style = scene_.style;
    std::string err;
    if (writePlayer(path, p, err)) setStatus("Exported " + path);
    else setStatus("Export failed: " + err);
  }

  void panel() {
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.3f, 1.0f, 0.85f, 1));
    ImGui::SetWindowFontScale(1.6f);
    ImGui::TextUnformatted("DATRACKARXD KEYGEN");
    ImGui::SetWindowFontScale(1.0f);
    ImGui::PopStyleColor();
    ImGui::TextDisabled("Turns a song into a standalone intro player.");
    ImGui::Spacing();

    ImGui::SeparatorText("Song");
    ImGui::TextWrapped("%s%s%s", song_.name.c_str(), song_.author.empty() ? "" : " - ", song_.author.c_str());
    ImGui::TextDisabled("%d channels, %d instruments", song_.channelCount(), (int)song_.instruments.size());
    ImGui::TextDisabled("%s", songPath_.c_str());
    if (ImGui::Button("Open song...")) {
      browserMode_ = 0;
      std::vector<std::string> exts = supportedSongExtensions();
      browser_.open("Open song", false, exts);
    }
    ImGui::SameLine();
    if (ImGui::Button(playing_ ? "Stop" : "Play")) {
      playing_ = !playing_;
      if (playing_) engine_.play(0, 0);
      else engine_.stop();
    }
    ImGui::TextDisabled("Or drop a song file on the window.");

    ImGui::SeparatorText("Texts");
    ImGui::SetNextItemWidth(-90);
    ImGui::InputText("Logo", logo_, sizeof(logo_));
    ImGui::SetNextItemWidth(-90);
    ImGui::InputText("Title", title_, sizeof(title_));
    ImGui::TextUnformatted("Scroller");
    ImGui::InputTextMultiline("##scroll", scroll_, sizeof(scroll_), ImVec2(-1, 80));
    if (ImGui::SmallButton("Default texts")) resetTexts();

    ImGui::SeparatorText("Look");
    KeygenStyle& st = scene_.style;
    ImGui::Checkbox("Rainbow colors", &st.rainbow);
    if (!st.rainbow) {
      float c[3] = {((st.color >> 16) & 255) / 255.0f, ((st.color >> 8) & 255) / 255.0f, (st.color & 255) / 255.0f};
      ImGui::SameLine();
      if (ImGui::ColorEdit3("##color", c, ImGuiColorEditFlags_NoInputs))
        st.color = ((uint32_t)std::lround(c[0] * 255) << 16) | ((uint32_t)std::lround(c[1] * 255) << 8) | (uint32_t)std::lround(c[2] * 255);
    }
    ImGui::Checkbox("Copper bars", &st.copper);
    ImGui::SameLine();
    ImGui::Checkbox("Oscilloscope", &st.scope);
    ImGui::Checkbox("Starfield", &st.stars);
    ImGui::SetNextItemWidth(-110);
    ImGui::SliderFloat("Star speed", &st.starSpeed, 0.0f, 5.0f, "%.1fx");
    ImGui::SetNextItemWidth(-110);
    ImGui::SliderFloat("Scroll speed", &st.scrollSpeed, 0.0f, 5.0f, "%.1fx");
    ImGui::SetNextItemWidth(-110);
    ImGui::SliderFloat("Wave height", &st.wave, 0.0f, 3.0f, "%.1fx");
    if (ImGui::SmallButton("Default look")) st = KeygenStyle();

    ImGui::SeparatorText("Export");
    ImGui::TextWrapped("The player runs on its own: Space pauses, F goes fullscreen, Esc quits.");
    if (ImGui::Button("Export player...", ImVec2(-1, 32))) {
      browserMode_ = 1;
#ifdef _WIN32
      browser_.open("Export player", true, {".exe"}, playerFileName(song_));
#else
      browser_.open("Export player", true, {}, playerFileName(song_));
#endif
    }
    if (!status_.empty() && ImGui::GetTime() - statusTime_ < 8.0) ImGui::TextWrapped("%s", status_.c_str());
  }

  void preview() {
    ImVec2 p0 = ImGui::GetCursorScreenPos();
    ImVec2 size = ImGui::GetContentRegionAvail();
    size.x = std::max(size.x, 64.0f);
    size.y = std::max(size.y, 64.0f);
    scene_.logo = logo_[0] ? logo_ : "DATRACKARXD";
    std::string text = scroll_[0] ? scroll_ : song_.name;
    scene_.scroll = "      " + text + "      ***      ";
    const std::array<float, SCOPE_LEN>& scope = engine_.masterScope();
    float t = (float)ImGui::GetTime();
    int secs = (int)t;
    char info[512];
    std::snprintf(info, sizeof(info), "%s%s%s   %02d:%02d   [SPACE] pause   [F] fullscreen   [ESC] exit", song_.name.c_str(),
                  song_.author.empty() ? "" : " - ", song_.author.c_str(), secs / 60, secs % 60);
    drawPlayerScreen(ImGui::GetWindowDrawList(), p0, size, scene_, scope.data(), SCOPE_LEN, engine_.scopePos(), t,
                     ImGui::GetIO().DeltaTime, info);
    ImGui::Dummy(size);
  }
};

int usage() {
  std::printf(
      "DATRACKARXD Keygen " DATRACKARXD_VERSION "\n"
      "usage:\n"
      "  datrackarxd-keygen                                  open the keygen maker\n"
      "  datrackarxd-keygen --export song out [logo] [text]  write a standalone player\n");
  return 1;
}

}  // namespace

int main(int argc, char** argv) {
  // An exported player carries its song: play it and nothing else.
  PlayerPayload payload;
  if (readOwnPayload(payload)) return runPlayer(payload, argc, argv);

  if (argc >= 2 && std::strcmp(argv[1], "--version") == 0) {
    std::printf("DATRACKARXD Keygen %s\n", DATRACKARXD_VERSION);
    return 0;
  }
  if (argc >= 2 && std::strcmp(argv[1], "--export") == 0) {
    if (argc < 4) return usage();
    Song song;
    std::string err;
    if (!loadAnySong(argv[2], song, err)) {
      std::fprintf(stderr, "%s\n", err.c_str());
      return 1;
    }
    PlayerPayload p;
    p.song = song.toText();
    p.title = song.name;
    if (argc >= 5) p.logo = argv[4];
    if (argc >= 6) p.scroll = argv[5];
    if (!writePlayer(argv[3], p, err)) {
      std::fprintf(stderr, "%s\n", err.c_str());
      return 1;
    }
    std::printf("wrote %s\n", argv[3]);
    return 0;
  }
  if (argc >= 2 && argv[1][0] == '-') return usage();

  if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_TIMER) != 0) {
    std::fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
    return 1;
  }
  SDL_EventState(SDL_DROPFILE, SDL_ENABLE);
  SDL_Window* window = SDL_CreateWindow("DATRACKARXD Keygen", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, 1180, 700,
                                        SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI);
  if (!window) {
    std::fprintf(stderr, "SDL_CreateWindow failed: %s\n", SDL_GetError());
    return 1;
  }
  SDL_Renderer* renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_PRESENTVSYNC | SDL_RENDERER_ACCELERATED);
  if (!renderer) renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_SOFTWARE);
  if (!renderer) {
    std::fprintf(stderr, "SDL_CreateRenderer failed: %s\n", SDL_GetError());
    return 1;
  }
  IMGUI_CHECKVERSION();
  ImGui::CreateContext();
  ImGui::GetIO().IniFilename = nullptr;
  ImGui_ImplSDL2_InitForSDLRenderer(window, renderer);
  ImGui_ImplSDLRenderer2_Init(renderer);
  setTheme(1);  // keygen colors

  Maker maker;
  if (argc >= 2) {
    std::lock_guard<std::mutex> lock(maker.engine().mutex);
    maker.load(argv[1]);
  }
  maker.openAudio();

  bool quit = false;
  while (!quit) {
    SDL_Event ev;
    while (SDL_PollEvent(&ev)) {
      ImGui_ImplSDL2_ProcessEvent(&ev);
      if (ev.type == SDL_QUIT) quit = true;
      if (ev.type == SDL_DROPFILE) {
        std::lock_guard<std::mutex> lock(maker.engine().mutex);
        maker.load(ev.drop.file);
        SDL_free(ev.drop.file);
      }
    }
    if (SDL_GetWindowFlags(window) & SDL_WINDOW_MINIMIZED) {
      SDL_Delay(20);
      continue;
    }
    ImGui_ImplSDLRenderer2_NewFrame();
    ImGui_ImplSDL2_NewFrame();
    {
      std::lock_guard<std::mutex> lock(maker.engine().mutex);
      ImGui::NewFrame();
      maker.frame();
      ImGui::Render();
    }
    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
    SDL_RenderClear(renderer);
    ImGui_ImplSDLRenderer2_RenderDrawData(ImGui::GetDrawData(), renderer);
    SDL_RenderPresent(renderer);
  }

  maker.closeAudio();
  ImGui_ImplSDLRenderer2_Shutdown();
  ImGui_ImplSDL2_Shutdown();
  ImGui::DestroyContext();
  SDL_DestroyRenderer(renderer);
  SDL_DestroyWindow(window);
  SDL_Quit();
  return 0;
}
