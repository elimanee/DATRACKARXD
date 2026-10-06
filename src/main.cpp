// DATRACKARXD: window, event loop, and command line export.
#include <SDL.h>
#include <imgui.h>
#include <imgui_impl_sdl2.h>
#include <imgui_impl_sdlrenderer2.h>

#include <cstdio>
#include <cstring>
#include <string>

#include "app.h"
#include "demo.h"
#include "import.h"

static int usage() {
  std::printf(
      "usage:\n"
      "  datrackarxd [song.dtk]                      open the tracker\n"
      "  datrackarxd --export song out.wav [n]       render n loops to WAV (dtk/fur/mod/xm/it/s3m)\n"
      "  datrackarxd --convert module out.dtk        import a module and save it as .dtk\n"
      "  datrackarxd --export-stems song prefix      one WAV per channel (prefix_01.wav...)\n"
      "  datrackarxd --export-demo out.wav           render the demo song\n"
      "  datrackarxd --save-demo out.dtk             write the demo song file\n");
  return 1;
}

static int commandLine(int argc, char** argv) {
  std::string cmd = argv[1];
  std::string err;
  Song song;
  if (cmd == "--convert" && argc >= 4) {
    if (!loadAnySong(argv[2], song, err) || !song.save(argv[3], err)) {
      std::fprintf(stderr, "%s\n", err.c_str());
      return 1;
    }
    std::printf("wrote %s\n", argv[3]);
    return 0;
  }
  if (cmd == "--export" && argc >= 4) {
    if (!loadAnySong(argv[2], song, err)) {
      std::fprintf(stderr, "%s\n", err.c_str());
      return 1;
    }
    int loops = argc >= 5 ? std::atoi(argv[4]) : 1;
    if (!exportWav(song, argv[3], 44100, loops, err)) {
      std::fprintf(stderr, "%s\n", err.c_str());
      return 1;
    }
    std::printf("wrote %s\n", argv[3]);
    return 0;
  }
  if (cmd == "--export-stems" && argc >= 4) {
    if (!loadAnySong(argv[2], song, err)) {
      std::fprintf(stderr, "%s\n", err.c_str());
      return 1;
    }
    for (int c = 0; c < song.channelCount(); c++) {
      Song solo = song;
      for (int o = 0; o < solo.channelCount(); o++) solo.channels[o].muted = o != c;
      char name[32];
      std::snprintf(name, sizeof(name), "_%02d.wav", c + 1);
      std::string out = std::string(argv[3]) + name;
      if (!exportWav(solo, out, 44100, 1, err)) {
        std::fprintf(stderr, "%s\n", err.c_str());
        return 1;
      }
      std::printf("wrote %s (%s)\n", out.c_str(), song.channels[c].name.c_str());
    }
    return 0;
  }
  if (cmd == "--export-demo" && argc >= 3) {
    loadDemoSong(song);
    if (!exportWav(song, argv[2], 44100, 1, err)) {
      std::fprintf(stderr, "%s\n", err.c_str());
      return 1;
    }
    std::printf("wrote %s\n", argv[2]);
    return 0;
  }
  if (cmd == "--save-demo" && argc >= 3) {
    loadDemoSong(song);
    if (!song.save(argv[2], err)) {
      std::fprintf(stderr, "%s\n", err.c_str());
      return 1;
    }
    std::printf("wrote %s\n", argv[2]);
    return 0;
  }
  return usage();
}

int main(int argc, char** argv) {
  if (argc >= 2 && argv[1][0] == '-' && argv[1][1] == '-') return commandLine(argc, argv);

  if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_TIMER) != 0) {
    std::fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
    return 1;
  }
  SDL_SetHint(SDL_HINT_IME_SHOW_UI, "1");
  SDL_Window* window = SDL_CreateWindow("DATRACKARXD", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, 1400, 860,
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
  ImGuiIO& io = ImGui::GetIO();
  io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
  io.IniFilename = "datrackarxd.ini";
  ImGui_ImplSDL2_InitForSDLRenderer(window, renderer);
  ImGui_ImplSDLRenderer2_Init(renderer);

  App app;
  app.loadSettings();
  if (argc >= 2) app.loadFile(argv[1]);
  app.openAudio();

  std::string title;
  while (!app.quit) {
    SDL_Event ev;
    while (SDL_PollEvent(&ev)) {
      ImGui_ImplSDL2_ProcessEvent(&ev);
      if (ev.type == SDL_QUIT) app.requestQuit();
      if (ev.type == SDL_WINDOWEVENT && ev.window.event == SDL_WINDOWEVENT_CLOSE && ev.window.windowID == SDL_GetWindowID(window))
        app.requestQuit();
      if (ev.type == SDL_KEYDOWN || ev.type == SDL_KEYUP)
        app.queueKey({ev.key.keysym.scancode, ev.key.keysym.sym, ev.key.keysym.mod, ev.type == SDL_KEYDOWN, ev.key.repeat != 0});
    }
    if (SDL_GetWindowFlags(window) & SDL_WINDOW_MINIMIZED) {
      SDL_Delay(20);
      continue;
    }

    ImGui_ImplSDLRenderer2_NewFrame();
    ImGui_ImplSDL2_NewFrame();
    {
      // The UI reads and edits the song; the audio thread waits meanwhile.
      std::lock_guard<std::mutex> lock(app.engine().mutex);
      ImGui::NewFrame();
      app.frame();
      ImGui::Render();
    }
    std::string t = app.windowTitle();
    if (t != title) {
      SDL_SetWindowTitle(window, t.c_str());
      title = t;
    }

    SDL_SetRenderDrawColor(renderer, 10, 10, 16, 255);
    SDL_RenderClear(renderer);
    ImGui_ImplSDLRenderer2_RenderDrawData(ImGui::GetDrawData(), renderer);
    SDL_RenderPresent(renderer);
  }

  ImGui_ImplSDLRenderer2_Shutdown();
  ImGui_ImplSDL2_Shutdown();
  ImGui::DestroyContext();
  SDL_DestroyRenderer(renderer);
  SDL_DestroyWindow(window);
  SDL_Quit();
  return 0;
}
