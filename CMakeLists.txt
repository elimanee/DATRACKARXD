cmake_minimum_required(VERSION 3.16)
project(DATRACKARXD VERSION 0.2.1 LANGUAGES CXX C)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release)
endif()

include(FetchContent)

# --- SDL2: system package if present, otherwise downloaded and built ---
find_package(SDL2 QUIET)
if(NOT SDL2_FOUND)
  message(STATUS "SDL2 not found on the system, fetching it")
  FetchContent_Declare(SDL2
    GIT_REPOSITORY https://github.com/libsdl-org/SDL.git
    GIT_TAG release-2.30.9
    GIT_SHALLOW TRUE)
  set(SDL_SHARED OFF CACHE BOOL "" FORCE)
  set(SDL_STATIC ON CACHE BOOL "" FORCE)
  set(SDL_TEST OFF CACHE BOOL "" FORCE)
  FetchContent_MakeAvailable(SDL2)
endif()

# --- Dear ImGui (docking branch) ---
FetchContent_Declare(imgui
  GIT_REPOSITORY https://github.com/ocornut/imgui.git
  GIT_TAG v1.91.9b-docking
  GIT_SHALLOW TRUE)
FetchContent_MakeAvailable(imgui)

add_library(imgui STATIC
  ${imgui_SOURCE_DIR}/imgui.cpp
  ${imgui_SOURCE_DIR}/imgui_draw.cpp
  ${imgui_SOURCE_DIR}/imgui_tables.cpp
  ${imgui_SOURCE_DIR}/imgui_widgets.cpp
  ${imgui_SOURCE_DIR}/imgui_demo.cpp
  ${imgui_SOURCE_DIR}/backends/imgui_impl_sdl2.cpp
  ${imgui_SOURCE_DIR}/backends/imgui_impl_sdlrenderer2.cpp)
target_include_directories(imgui PUBLIC ${imgui_SOURCE_DIR} ${imgui_SOURCE_DIR}/backends)
if(TARGET SDL2::SDL2-static)
  target_link_libraries(imgui PUBLIC SDL2::SDL2-static)
else()
  target_link_libraries(imgui PUBLIC SDL2::SDL2)
endif()

# --- RtMidi: only RtMidi.cpp is built, with the platform's MIDI API ---
FetchContent_Declare(rtmidi
  GIT_REPOSITORY https://github.com/thestk/rtmidi.git
  GIT_TAG 6.0.0
  GIT_SHALLOW TRUE
  SOURCE_SUBDIR no-cmake)  # don't run RtMidi's own CMakeLists
FetchContent_MakeAvailable(rtmidi)
add_library(rtmidi STATIC ${rtmidi_SOURCE_DIR}/RtMidi.cpp)
target_include_directories(rtmidi PUBLIC ${rtmidi_SOURCE_DIR})
if(WIN32)
  target_compile_definitions(rtmidi PRIVATE __WINDOWS_MM__)
  target_link_libraries(rtmidi PUBLIC winmm)
elseif(APPLE)
  target_compile_definitions(rtmidi PRIVATE __MACOSX_CORE__)
  target_link_libraries(rtmidi PUBLIC "-framework CoreMIDI" "-framework CoreAudio" "-framework CoreFoundation")
else()
  find_package(ALSA)
  find_package(Threads REQUIRED)
  if(ALSA_FOUND)
    target_compile_definitions(rtmidi PRIVATE __LINUX_ALSA__)
    target_link_libraries(rtmidi PUBLIC ALSA::ALSA Threads::Threads)
  else()
    message(WARNING "ALSA not found: building without MIDI input")
  endif()
endif()

# --- the tracker ---
add_executable(datrackarxd
  src/main.cpp
  src/song.cpp
  src/engine.cpp
  src/effects.cpp
  src/demo.cpp
  src/import.cpp
  src/import_tracker.cpp
  src/import_fur.cpp
  src/inflate.cpp
  src/midi.cpp
  src/app.cpp
  src/ui_pattern.cpp
  src/ui_pianoroll.cpp
  src/ui_instrument.cpp
  src/ui_windows.cpp
  src/theme.cpp)
target_link_libraries(datrackarxd PRIVATE imgui rtmidi)
target_compile_definitions(datrackarxd PRIVATE DATRACKARXD_VERSION="${PROJECT_VERSION}")
if(TARGET SDL2::SDL2main)
  target_link_libraries(datrackarxd PRIVATE SDL2::SDL2main)
endif()
if(MSVC)
  target_compile_options(datrackarxd PRIVATE /W3)
else()
  target_compile_options(datrackarxd PRIVATE -Wall -Wextra)
endif()
