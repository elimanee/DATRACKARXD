cmake_minimum_required(VERSION 3.16)
project(DATRACKARXD VERSION 0.7.0 LANGUAGES CXX C)

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

# --- libogg + libvorbis (OGG export and import): the sources are built directly, as
# their own CMake files want an installed Ogg ---
FetchContent_Declare(ogg
  GIT_REPOSITORY https://github.com/xiph/ogg.git
  GIT_TAG v1.3.6
  GIT_SHALLOW TRUE
  SOURCE_SUBDIR no-cmake)
FetchContent_Declare(vorbis
  GIT_REPOSITORY https://github.com/xiph/vorbis.git
  GIT_TAG v1.3.7
  GIT_SHALLOW TRUE
  SOURCE_SUBDIR no-cmake)
FetchContent_MakeAvailable(ogg vorbis)
set(OGG_CONFIG_DIR ${CMAKE_BINARY_DIR}/ogg_config)
file(WRITE ${OGG_CONFIG_DIR}/ogg/config_types.h
  "#ifndef __CONFIG_TYPES_H__\n#define __CONFIG_TYPES_H__\n#include <stdint.h>\n"
  "typedef int16_t ogg_int16_t;\ntypedef uint16_t ogg_uint16_t;\n"
  "typedef int32_t ogg_int32_t;\ntypedef uint32_t ogg_uint32_t;\n"
  "typedef int64_t ogg_int64_t;\ntypedef uint64_t ogg_uint64_t;\n#endif\n")
set(VORBIS_LIB_SOURCES mdct smallft block envelope window lsp lpc analysis synthesis psy info
  floor1 floor0 res0 mapping0 registry codebook sharedbook lookup bitrate vorbisenc vorbisfile)
list(TRANSFORM VORBIS_LIB_SOURCES PREPEND ${vorbis_SOURCE_DIR}/lib/)
list(TRANSFORM VORBIS_LIB_SOURCES APPEND .c)
add_library(vorbisenc_static STATIC
  ${ogg_SOURCE_DIR}/src/bitwise.c
  ${ogg_SOURCE_DIR}/src/framing.c
  ${VORBIS_LIB_SOURCES})
target_include_directories(vorbisenc_static
  PUBLIC ${ogg_SOURCE_DIR}/include ${OGG_CONFIG_DIR} ${vorbis_SOURCE_DIR}/include
  PRIVATE ${vorbis_SOURCE_DIR}/lib)
if(MSVC)
  target_compile_definitions(vorbisenc_static PRIVATE _CRT_SECURE_NO_WARNINGS _USE_MATH_DEFINES)
  target_compile_options(vorbisenc_static PRIVATE /W0)
else()
  target_compile_options(vorbisenc_static PRIVATE -w)
  find_library(MATH_LIBRARY m)
  if(MATH_LIBRARY)
    target_link_libraries(vorbisenc_static PUBLIC ${MATH_LIBRARY})
  endif()
endif()

# --- dr_flac and dr_mp3 (public domain / MIT-0): FLAC and MP3 samples ---
FetchContent_Declare(drlibs
  GIT_REPOSITORY https://github.com/mackron/dr_libs.git
  GIT_TAG mp3-0.7.3
  GIT_SHALLOW TRUE
  SOURCE_SUBDIR no-cmake)
FetchContent_MakeAvailable(drlibs)

# --- ymfm (BSD-3-Clause): the OPL3 emulator ---
FetchContent_Declare(ymfm
  GIT_REPOSITORY https://github.com/aaronsgiles/ymfm.git
  GIT_TAG 81aec25ccbb98f4873a255f7551ac4dadac59b4a
  SOURCE_SUBDIR no-cmake)
FetchContent_MakeAvailable(ymfm)
add_library(ymfm STATIC
  ${ymfm_SOURCE_DIR}/src/ymfm_opl.cpp
  ${ymfm_SOURCE_DIR}/src/ymfm_adpcm.cpp
  ${ymfm_SOURCE_DIR}/src/ymfm_pcm.cpp)
target_include_directories(ymfm SYSTEM PUBLIC ${ymfm_SOURCE_DIR}/src)
if(MSVC)
  target_compile_options(ymfm PRIVATE /W0)
else()
  target_compile_options(ymfm PRIVATE -w)
endif()

# --- shared code: song model, engine, importers, keygen player ---
add_library(dtcore STATIC
  src/song.cpp
  src/engine.cpp
  src/chips.cpp
  src/effects.cpp
  src/demo.cpp
  src/import.cpp
  src/import_tracker.cpp
  src/import_fur.cpp
  src/import_dmf.cpp
  src/import_ftm.cpp
  src/inflate.cpp
  src/midifile.cpp
  src/theme.cpp
  src/keygen_fx.cpp
  src/payload.cpp
  src/player.cpp
  src/sampleops.cpp)
target_include_directories(dtcore PUBLIC src)
target_link_libraries(dtcore PUBLIC imgui ymfm)
target_compile_definitions(dtcore PUBLIC DATRACKARXD_VERSION="${PROJECT_VERSION}")

# --- the tracker ---
add_executable(datrackarxd
  src/main.cpp
  src/export.cpp
  src/midi.cpp
  src/app.cpp
  src/ui_pattern.cpp
  src/ui_pianoroll.cpp
  src/ui_instrument.cpp
  src/ui_windows.cpp
  src/audiofile.cpp
  src/recorder.cpp)
target_include_directories(datrackarxd PRIVATE ${drlibs_SOURCE_DIR})
target_link_libraries(datrackarxd PRIVATE dtcore rtmidi vorbisenc_static)

# --- DATRACKARXD Keygen: makes standalone keygen-style players ---
add_executable(datrackarxd-keygen
  src/keygen_main.cpp
  src/filebrowser.cpp)
target_link_libraries(datrackarxd-keygen PRIVATE dtcore)

foreach(t dtcore datrackarxd datrackarxd-keygen)
  if(MSVC)
    target_compile_options(${t} PRIVATE /W3)
  else()
    target_compile_options(${t} PRIVATE -Wall -Wextra)
  endif()
endforeach()
foreach(t datrackarxd datrackarxd-keygen)
  if(TARGET SDL2::SDL2main)
    target_link_libraries(${t} PRIVATE SDL2::SDL2main)
  endif()
endforeach()
