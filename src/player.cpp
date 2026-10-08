#include "player.h"

#include <SDL.h>
#include <imgui.h>
#include <imgui_impl_sdl2.h>
#include <imgui_impl_sdlrenderer2.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <mutex>

#include "engine.h"
#include "keygen_fx.h"
#include "theme.h"

namespace {

void audioCallback(void* user, Uint8* stream, int len) {
  Engine* engine = static_cast<Engine*>(user);
  std::lock_guard<std::mutex> lock(engine->mutex);
  engine->render(reinterpret_cast<float*>(stream), len / (int)(2 * sizeof(float)));
}

std::string upper(std::string s) {
  for (char& c : s) c = (char)std::toupper((unsigned char)c);
  return s;
}

}  // namespace

int runPlayer(const PlayerPayload& payload, int argc, char** argv) {
  Song song;
  std::string err;
  if (!song.fromText(payload.song, err)) {
    std::fprintf(stderr, "Player: bad song: %s\n", err.c_str());
    return 1;
  }
  std::string title = payload.title.empty() ? song.name : payload.title;

  if (argc >= 2 && std::strcmp(argv[1], "--selftest") == 0) {
    Engine engine(song, 44100);
    engine.play(0, 0);
    std::vector<float> buf(44100 * 2);
    float peak = 0;
    for (int i = 0; i < 3; i++) {
      engine.render(buf.data(), 44100);
      for (float v : buf) peak = std::max(peak, std::abs(v));
    }
    std::printf("player ok: \"%s\", %d channels, peak %.2f\n", title.c_str(), song.channelCount(), peak);
    return peak > 0.01f ? 0 : 2;
  }

  if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_TIMER) != 0) {
    std::fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
    return 1;
  }
  SDL_Window* window = SDL_CreateWindow(title.c_str(), SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, 900, 560,
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

  Engine engine(song, 44100);
  engine.play(0, 0);
  SDL_AudioSpec want{}, have{};
  want.freq = 44100;
  want.format = AUDIO_F32SYS;
  want.channels = 2;
  want.samples = 512;
  want.callback = audioCallback;
  want.userdata = &engine;
  SDL_AudioDeviceID dev = SDL_OpenAudioDevice(nullptr, 0, &want, &have, 0);
  if (dev) SDL_PauseAudioDevice(dev, 0);

  KeygenScene scene;
  scene.logo = payload.logo.empty() ? "DATRACKARXD" : payload.logo;
  std::string scroll = payload.scroll;
  if (scroll.empty()) scroll = "NOW PLAYING: " + song.name + (song.author.empty() ? "" : "  BY " + song.author);
  scene.scroll = "      " + upper(scroll) + "      ***      ";

  bool quit = false, paused = false, fullscreen = false;
  Uint64 start = SDL_GetPerformanceCounter();
  double pausedTime = 0, pauseStart = 0;
  while (!quit) {
    SDL_Event ev;
    while (SDL_PollEvent(&ev)) {
      ImGui_ImplSDL2_ProcessEvent(&ev);
      if (ev.type == SDL_QUIT) quit = true;
      if (ev.type == SDL_KEYDOWN && !ev.key.repeat) {
        SDL_Keycode k = ev.key.keysym.sym;
        if (k == SDLK_ESCAPE) quit = true;
        if (k == SDLK_SPACE && dev) {
          paused = !paused;
          SDL_PauseAudioDevice(dev, paused ? 1 : 0);
          double now = (double)(SDL_GetPerformanceCounter() - start) / SDL_GetPerformanceFrequency();
          if (paused) pauseStart = now;
          else pausedTime += now - pauseStart;
        }
        if (k == SDLK_f || k == SDLK_F11 || (k == SDLK_RETURN && (ev.key.keysym.mod & KMOD_ALT))) {
          fullscreen = !fullscreen;
          SDL_SetWindowFullscreen(window, fullscreen ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0);
        }
      }
    }
    if (SDL_GetWindowFlags(window) & SDL_WINDOW_MINIMIZED) {
      SDL_Delay(20);
      continue;
    }
    ImGui_ImplSDLRenderer2_NewFrame();
    ImGui_ImplSDL2_NewFrame();
    ImGui::NewFrame();

    float peak = 0;
    std::array<float, SCOPE_LEN> scope;
    int scopePos;
    {
      std::lock_guard<std::mutex> lock(engine.mutex);
      scope = engine.masterScope();
      scopePos = engine.scopePos();
    }
    if (paused) scope.fill(0);
    for (float v : scope) peak = std::max(peak, std::abs(v));

    const ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(vp->Pos);
    ImGui::SetNextWindowSize(vp->Size);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::Begin("player", nullptr,
                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
                     ImGuiWindowFlags_NoBringToFrontOnFocus);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 p0 = vp->Pos, size = vp->Size;
    double now = (double)(SDL_GetPerformanceCounter() - start) / SDL_GetPerformanceFrequency();
    float t = (float)(paused ? pauseStart - pausedTime : now - pausedTime);
    scene.draw(dl, p0, size, t, paused ? 0.0f : ImGui::GetIO().DeltaTime, peak);

    // Oscilloscope strip and the info line at the bottom.
    float barH = std::clamp(size.y * 0.08f, 24.0f, 60.0f);
    ImVec2 b0(p0.x, p0.y + size.y - barH - 22), b1(p0.x + size.x, p0.y + size.y - 22);
    dl->AddRectFilled(b0, ImVec2(b1.x, p0.y + size.y), IM_COL32(0, 0, 0, 170));
    const int n = 512;
    ImVec2 prev;
    for (int i = 0; i < n; i++) {
      float v = scope[(scopePos - n + i + SCOPE_LEN * 2) % SCOPE_LEN];
      ImVec2 pt(b0.x + size.x * i / (n - 1), (b0.y + b1.y) / 2 - v * barH * 0.45f);
      if (i) dl->AddLine(prev, pt, IM_COL32(0, 255, 200, 230), 1.5f);
      prev = pt;
    }
    int secs = (int)t;
    char info[512];
    std::snprintf(info, sizeof(info), "%s%s%s   %02d:%02d   [SPACE] %s   [F] fullscreen   [ESC] exit", song.name.c_str(),
                  song.author.empty() ? "" : " - ", song.author.c_str(), secs / 60, secs % 60, paused ? "play" : "pause");
    dl->AddText(ImVec2(p0.x + 10, p0.y + size.y - 19), IM_COL32(200, 255, 240, 255), info);
    ImGui::End();
    ImGui::PopStyleVar(2);

    ImGui::Render();
    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
    SDL_RenderClear(renderer);
    ImGui_ImplSDLRenderer2_RenderDrawData(ImGui::GetDrawData(), renderer);
    SDL_RenderPresent(renderer);
  }

  if (dev) SDL_CloseAudioDevice(dev);
  ImGui_ImplSDLRenderer2_Shutdown();
  ImGui_ImplSDL2_Shutdown();
  ImGui::DestroyContext();
  SDL_DestroyRenderer(renderer);
  SDL_DestroyWindow(window);
  SDL_Quit();
  return 0;
}
