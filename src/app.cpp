#include "app.h"

#include <imgui.h>
#include <imgui_internal.h>

#include <cctype>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>

#include "audiofile.h"
#include "demo.h"
#include "import.h"
#include "midifile.h"
#include "modexport.h"
#include "vgmexport.h"
#include "payload.h"
#include "theme.h"

namespace fs = std::filesystem;

static void audioCallback(void* user, Uint8* stream, int len) {
  Engine* engine = static_cast<Engine*>(user);
  std::lock_guard<std::mutex> lock(engine->mutex);
  engine->render(reinterpret_cast<float*>(stream), len / (int)(2 * sizeof(float)));
}

App::App() {
  loadDemoSong(song_);
  metaShadow_ = metaSnapshot();
  engine_ = std::make_unique<Engine>(song_, 44100);
  std::error_code ec;
  dialogDir_ = fs::current_path(ec).string();
}

App::~App() {
  if (audioDev_) SDL_CloseAudioDevice(audioDev_);
  saveSettings();
}

// Small key=value settings file next to the ImGui layout file.
static const char* SETTINGS_FILE = "datrackarxd.cfg";

void App::loadSettings() {
  std::ifstream f(SETTINGS_FILE);
  std::string line;
  int th = 0;
  while (std::getline(f, line)) {
    size_t eq = line.find('=');
    if (eq == std::string::npos) continue;
    std::string k = line.substr(0, eq), v = line.substr(eq + 1);
    try {
      if (k == "theme") th = std::stoi(v);
      if (k == "scroller") showScroller_ = std::stoi(v) != 0;
      if (k == "octave") octave_ = std::clamp(std::stoi(v), 0, 8);
      if (k == "dir" && fs::is_directory(v)) dialogDir_ = v;
      if (k == "midi") midiPort_ = v;
      if (k == "midivelocity") midiVelocity_ = std::stoi(v) != 0;
      if (k == "midibend") midiBendRange_ = std::clamp(std::stoi(v), 1, 24);
      if (k == "mpe") midiMpe_ = std::stoi(v) != 0;
      if (k == "mperange") midiMpeRange_ = std::clamp(std::stoi(v), 1, 96);
      if (k == "metronome") engine_->metronome = std::stoi(v) != 0;
      if (k == "metrovol") metronomeVol_ = std::clamp(std::stof(v), 0.0f, 1.0f);
      if (k == "countin") countInBars_ = std::clamp(std::stoi(v), 0, 4);
      if (k == "quantize") midiQuantize_ = std::clamp(std::stoi(v), 0, 3);
    } catch (...) {
    }
  }
  setTheme(th);
  // The keyboard may be unplugged: keep the name so it reconnects next time.
  if (!midiPort_.empty() && midi_.open(midiPort_)) setStatus("MIDI input: " + midiPort_);
}

void App::saveSettings() {
  std::ofstream f(SETTINGS_FILE);
  f << "theme=" << themeIndex() << "\n";
  f << "scroller=" << (showScroller_ ? 1 : 0) << "\n";
  f << "octave=" << octave_ << "\n";
  f << "dir=" << dialogDir_ << "\n";
  f << "midi=" << midiPort_ << "\n";
  f << "midivelocity=" << (midiVelocity_ ? 1 : 0) << "\n";
  f << "midibend=" << midiBendRange_ << "\n";
  f << "mpe=" << (midiMpe_ ? 1 : 0) << "\n";
  f << "mperange=" << midiMpeRange_ << "\n";
  f << "metronome=" << (engine_->metronome ? 1 : 0) << "\n";
  f << "metrovol=" << metronomeVol_ << "\n";
  f << "countin=" << countInBars_ << "\n";
  f << "quantize=" << midiQuantize_ << "\n";
}

void App::openAudio() {
  SDL_AudioSpec want{}, have{};
  want.freq = 44100;
  want.format = AUDIO_F32SYS;
  want.channels = 2;
  want.samples = 512;
  want.callback = audioCallback;
  want.userdata = engine_.get();
  // No ALLOW_* flags: SDL converts to whatever the device really uses.
  audioDev_ = SDL_OpenAudioDevice(nullptr, 0, &want, &have, 0);
  if (!audioDev_) {
    audioStatus_ = std::string("No audio: ") + SDL_GetError();
    setStatus(audioStatus_);
    return;
  }
  audioStatus_ = "Audio: " + std::to_string(have.freq) + " Hz";
  SDL_PauseAudioDevice(audioDev_, 0);
}

void App::setStatus(const std::string& s) {
  status_ = s;
  statusTime_ = ImGui::GetTime();
}

std::string App::windowTitle() const {
  std::string t = "DATRACKARXD - " + (filePath_.empty() ? song_.name : fs::path(filePath_).filename().string());
  if (dirty_) t += " *";
  return t;
}

void App::afterSongReplaced() {
  engine_->reset();
  midiHeld_.clear();
  midiChord_ = false;
  curOrder_ = curRow_ = curCh_ = curCol_ = 0;
  curIns_ = 0;
  hasSel_ = false;
  resetUndo();
  dirty_ = false;
}

bool App::loadFile(const std::string& path) {
  std::string err;
  if (!loadAnySong(path, song_, err)) {
    setStatus("Load failed: " + err);
    return false;
  }
  afterSongReplaced();
  // Imported modules get saved as a new .dtk file.
  filePath_ = fs::path(path).extension() == ".dtk" ? path : "";
  dialogDir_ = fs::path(path).parent_path().string();
  if (dialogDir_.empty()) dialogDir_ = ".";
  setStatus("Loaded " + path);
  return true;
}

void App::saveSong(bool forceDialog) {
  if (forceDialog || filePath_.empty()) {
    openFileDialog(FileDialogMode::Save);
    return;
  }
  std::string err;
  if (song_.save(filePath_, err)) {
    dirty_ = false;
    setStatus("Saved " + filePath_);
  } else {
    setStatus("Save failed: " + err);
  }
}

void App::togglePlay(bool fromCursor) {
  if (engine_->playing()) {
    engine_->stop();
  } else {
    // Recording starts after a bar or two of clicks.
    int countIn = editMode_ ? countInBars_ * std::max(1, song_.highlight2) : 0;
    engine_->play(curOrder_, fromCursor ? curRow_ : 0, countIn);
  }
}

void App::requestQuit() { askAction(PendingAction::Quit); }

void App::askAction(PendingAction a) {
  if (dirty_) {
    pending_ = a;
    confirmRequest_ = true;
  } else {
    doAction(a);
  }
}

void App::doAction(PendingAction a) {
  switch (a) {
    case PendingAction::New:
      engine_->stop();
      song_.reset();
      filePath_.clear();
      afterSongReplaced();
      setStatus("New song");
      break;
    case PendingAction::Open:
      openFileDialog(FileDialogMode::Open);
      break;
    case PendingAction::Demo:
      engine_->stop();
      loadDemoSong(song_);
      filePath_.clear();
      afterSongReplaced();
      setStatus("Demo song loaded");
      break;
    case PendingAction::Quit:
      quit = true;
      break;
    case PendingAction::None:
      break;
  }
}

void App::confirmPopup() {
  if (confirmRequest_) {
    ImGui::OpenPopup("Unsaved changes");
    confirmRequest_ = false;
  }
  if (ImGui::BeginPopupModal("Unsaved changes", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
    ImGui::Text("The song has unsaved changes. Discard them?");
    if (ImGui::Button("Discard", ImVec2(120, 0))) {
      ImGui::CloseCurrentPopup();
      doAction(pending_);
      pending_ = PendingAction::None;
    }
    ImGui::SameLine();
    if (ImGui::Button("Cancel", ImVec2(120, 0)) || ImGui::IsKeyPressed(ImGuiKey_Escape)) {
      ImGui::CloseCurrentPopup();
      pending_ = PendingAction::None;
    }
    ImGui::EndPopup();
  }
}

// ---------------------------------------------------------------------------
// File dialog: a small built-in browser, no external dependency.
// ---------------------------------------------------------------------------

void App::openFileDialog(FileDialogMode mode) {
  dialogMode_ = mode;
  dialogOpenRequest_ = true;
  std::string base = filePath_.empty() ? song_.name : fs::path(filePath_).stem().string();
  if (mode == FileDialogMode::Open) base.clear();
  if (mode == FileDialogMode::Save) base += ".dtk";
  if (mode == FileDialogMode::ExportWav) base += ".wav";
  if (mode == FileDialogMode::ExportMidi) base += ".mid";
  if (mode == FileDialogMode::ExportOgg) base += ".ogg";
  if (mode == FileDialogMode::ExportVgm) base += ".vgm";
  if (mode == FileDialogMode::ExportModule) base += moduleFormat_ == 1 ? ".mod" : moduleFormat_ == 2 ? ".it" : ".xm";
#ifdef _WIN32
  if (mode == FileDialogMode::ExportPlayer) base += ".exe";
#endif
  if (mode == FileDialogMode::LoadInstrument) base.clear();
  if (mode == FileDialogMode::SaveInstrument && curIns_ < (int)song_.instruments.size()) base = song_.instruments[curIns_].name + ".dti";
  std::snprintf(dialogName_, sizeof(dialogName_), "%s", base.c_str());
}

// Texts of the keygen player, then the file dialog for where to write it.
void App::playerExportPopup() {
  if (playerPopupRequest_) {
    ImGui::OpenPopup("Export keygen player");
    playerPopupRequest_ = false;
  }
  ImGui::SetNextWindowSize(ImVec2(520, 0), ImGuiCond_Appearing);
  if (!ImGui::BeginPopupModal("Export keygen player", nullptr)) return;
  ImGui::TextWrapped("A standalone program that plays this song with the keygen intro: starfield, logo and scroller. "
                     "It runs on its own, without DATRACKARXD.");
  ImGui::TextDisabled("For colors, speeds and a live preview, use the DATRACKARXD Keygen app.");
  ImGui::Separator();
  ImGui::InputText("Logo", playerLogo_, sizeof(playerLogo_));
  ImGui::InputText("Window title", playerTitle_, sizeof(playerTitle_));
  ImGui::TextUnformatted("Scroller text");
  ImGui::InputTextMultiline("##scroll", playerScroll_, sizeof(playerScroll_), ImVec2(-1, 90));
  if (ImGui::Button("Export...", ImVec2(120, 0))) {
    ImGui::CloseCurrentPopup();
    openFileDialog(FileDialogMode::ExportPlayer);
  }
  ImGui::SameLine();
  if (ImGui::Button("Cancel", ImVec2(120, 0)) || ImGui::IsKeyPressed(ImGuiKey_Escape)) ImGui::CloseCurrentPopup();
  ImGui::EndPopup();
}

void App::fileDialog() {
  const char* title = dialogMode_ == FileDialogMode::Open        ? "Open song / import module###filedlg"
                      : dialogMode_ == FileDialogMode::Save      ? "Save song###filedlg"
                      : dialogMode_ == FileDialogMode::LoadSample ? "Load sample (WAV, FLAC, MP3, OGG)###filedlg"
                      : dialogMode_ == FileDialogMode::LoadInstrument ? "Load instrument###filedlg"
                      : dialogMode_ == FileDialogMode::SaveInstrument ? "Save instrument###filedlg"
                      : dialogMode_ == FileDialogMode::ExportMidi ? "Export MIDI###filedlg"
                      : dialogMode_ == FileDialogMode::ExportOgg ? "Export OGG###filedlg"
                      : dialogMode_ == FileDialogMode::ExportModule ? "Export module (XM, MOD, IT)###filedlg"
                      : dialogMode_ == FileDialogMode::ExportVgm ? "Export VGM###filedlg"
                      : dialogMode_ == FileDialogMode::ExportPlayer ? "Export keygen player###filedlg"
                                                                 : "Export WAV###filedlg";
  if (dialogOpenRequest_) {
    ImGui::OpenPopup("###filedlg");
    dialogOpenRequest_ = false;
  }
  ImGui::SetNextWindowSize(ImVec2(560, 420), ImGuiCond_Appearing);
  if (!ImGui::BeginPopupModal(title, nullptr)) return;

  std::vector<std::string> exts;
  if (dialogMode_ == FileDialogMode::Open) exts = supportedSongExtensions();
  else if (dialogMode_ == FileDialogMode::Save) exts = {".dtk"};
  else if (dialogMode_ == FileDialogMode::LoadInstrument) exts = instrumentFileExtensions();
  else if (dialogMode_ == FileDialogMode::SaveInstrument) exts = {".dti"};
  else if (dialogMode_ == FileDialogMode::ExportMidi) exts = {".mid"};
  else if (dialogMode_ == FileDialogMode::ExportOgg) exts = {".ogg"};
  else if (dialogMode_ == FileDialogMode::ExportVgm) exts = {".vgm"};
  else if (dialogMode_ == FileDialogMode::ExportModule)
    exts = moduleFormat_ == 1 ? std::vector<std::string>{".mod", ".xm", ".it"}
           : moduleFormat_ == 2 ? std::vector<std::string>{".it", ".xm", ".mod"}
                                : std::vector<std::string>{".xm", ".mod", ".it"};
  else if (dialogMode_ == FileDialogMode::LoadSample) exts = {".wav", ".flac", ".mp3", ".ogg", ".oga"};
#ifdef _WIN32
  else if (dialogMode_ == FileDialogMode::ExportPlayer) exts = {".exe"};
#else
  else if (dialogMode_ == FileDialogMode::ExportPlayer) exts = {""};
#endif
  else exts = {".wav"};
  const std::string& ext = exts[0];
  auto matches = [&](const fs::path& p) {
    std::string e = p.extension().string();
    std::transform(e.begin(), e.end(), e.begin(), ::tolower);
    // Amiga-style names like "mod.songname" count too.
    std::string stem = p.filename().string().substr(0, 4);
    std::transform(stem.begin(), stem.end(), stem.begin(), ::tolower);
    for (const auto& x : exts)
      if (e == x || stem == x.substr(1) + ".") return true;
    return false;
  };
  ImGui::TextUnformatted(dialogDir_.c_str());
  if (ImGui::Button("Up")) {
    fs::path p = fs::path(dialogDir_).parent_path();
    if (!p.empty()) dialogDir_ = p.string();
  }
  ImGui::SameLine();
  {
    std::string list;
    for (const auto& x : exts) list += " *" + x;
    ImGui::TextDisabled("(folders and%s)", list.c_str());
  }

  bool accept = false;
  ImGui::BeginChild("files", ImVec2(0, -ImGui::GetFrameHeightWithSpacing() * 2.2f), ImGuiChildFlags_Borders);
  std::vector<fs::directory_entry> dirs, files;
  std::error_code ec;
  for (fs::directory_iterator it(dialogDir_, ec), end; !ec && it != end; it.increment(ec)) {
    std::error_code ec2;
    if (it->is_directory(ec2))
      dirs.push_back(*it);
    else if (matches(it->path()))
      files.push_back(*it);
  }
  auto byName = [](const fs::directory_entry& a, const fs::directory_entry& b) { return a.path().filename() < b.path().filename(); };
  std::sort(dirs.begin(), dirs.end(), byName);
  std::sort(files.begin(), files.end(), byName);
  std::string newDir;
  for (auto& d : dirs) {
    std::string label = "[" + d.path().filename().string() + "]";
    if (ImGui::Selectable(label.c_str(), false, ImGuiSelectableFlags_AllowDoubleClick) && ImGui::IsMouseDoubleClicked(0))
      newDir = d.path().string();
  }
  for (auto& f : files) {
    std::string name = f.path().filename().string();
    if (ImGui::Selectable(name.c_str(), name == dialogName_, ImGuiSelectableFlags_AllowDoubleClick)) {
      std::snprintf(dialogName_, sizeof(dialogName_), "%s", name.c_str());
      if (ImGui::IsMouseDoubleClicked(0)) accept = true;
    }
  }
  ImGui::EndChild();
  if (!newDir.empty()) dialogDir_ = newDir;

  ImGui::SetNextItemWidth(-200);
  if (ImGui::IsWindowAppearing()) ImGui::SetKeyboardFocusHere();
  if (ImGui::InputText("##name", dialogName_, sizeof(dialogName_), ImGuiInputTextFlags_EnterReturnsTrue)) accept = true;
  if (dialogMode_ == FileDialogMode::ExportWav || dialogMode_ == FileDialogMode::ExportMidi ||
      dialogMode_ == FileDialogMode::ExportOgg) {
    ImGui::SameLine();
    ImGui::SetNextItemWidth(80);
    ImGui::InputInt("loops", &exportLoops_);
    exportLoops_ = std::clamp(exportLoops_, 1, 16);
  }
  if (dialogMode_ == FileDialogMode::ExportModule) {
    ImGui::SetNextItemWidth(120);
    if (ImGui::Combo("Format", &moduleFormat_, "XM (FastTracker 2)\0MOD (ProTracker)\0IT (Impulse Tracker)\0")) {
      // Follow the format in the file name.
      fs::path p(dialogName_);
      p.replace_extension(moduleFormat_ == 1 ? ".mod" : moduleFormat_ == 2 ? ".it" : ".xm");
      std::snprintf(dialogName_, sizeof(dialogName_), "%s", p.string().c_str());
    }
    ImGui::SameLine();
    ImGui::SetNextItemWidth(110);
    ImGui::BeginDisabled(moduleFormat_ == 1);
    ImGui::Combo("Rendered instruments", &moduleRate_, "11025 Hz\0" "22050 Hz\0" "44100 Hz\0");
    ImGui::EndDisabled();
    ImGui::SetNextItemWidth(120);
    ImGui::SliderFloat("Note held", &moduleHold_, 0.2f, 4.0f, "%.1f s");
    if (ImGui::IsItemHovered())
      ImGui::SetTooltip("Instruments that aren't samples are rendered: one note held this long, then\n"
                        "looped if it still sounds. MOD resamples everything to 8363 Hz, 8-bit.");
  }
  if (dialogMode_ == FileDialogMode::ExportOgg) {
    ImGui::SetNextItemWidth(200);
    ImGui::SliderInt("Quality", &oggQuality_, 0, 10);
    ImGui::SameLine();
    static const int kbps[11] = {64, 80, 96, 112, 128, 160, 192, 224, 256, 320, 500};
    ImGui::TextDisabled("about %d kbit/s", kbps[std::clamp(oggQuality_, 0, 10)]);
  }
  bool opening = dialogMode_ == FileDialogMode::Open || dialogMode_ == FileDialogMode::LoadSample ||
                 dialogMode_ == FileDialogMode::LoadInstrument;
  if (ImGui::Button(opening ? "Open" : "Save", ImVec2(100, 0))) accept = true;
  ImGui::SameLine();
  bool cancel = ImGui::Button("Cancel", ImVec2(100, 0)) || ImGui::IsKeyPressed(ImGuiKey_Escape);

  if (accept && dialogName_[0]) {
    fs::path p = fs::path(dialogName_);
    if (p.is_relative()) p = fs::path(dialogDir_) / p;
    std::string pe = p.extension().string();
    std::transform(pe.begin(), pe.end(), pe.begin(), ::tolower);
    if (!opening && std::find(exts.begin(), exts.end(), pe) == exts.end()) p += ext;
    std::string path = p.string();
    std::string err;
    if (dialogMode_ == FileDialogMode::Open) {
      engine_->stop();
      if (loadFile(path)) cancel = true;
    } else if (dialogMode_ == FileDialogMode::LoadSample) {
      Sample smp;
      if ((int)song_.samples.size() >= MAX_SAMPLES) {
        setStatus("Too many samples");
      } else if (loadAudioSample(path, smp, err)) {
        pushFullUndo();
        song_.samples.push_back(std::move(smp));
        curSample_ = (int)song_.samples.size() - 1;
        if (loadSampleIntoIns_ >= 0 && loadSampleIntoIns_ < (int)song_.instruments.size())
          song_.instruments[loadSampleIntoIns_].sample = curSample_;
        dirty_ = true;
        setStatus("Loaded sample " + path);
        cancel = true;
      } else {
        setStatus("Sample load failed: " + err);
      }
    } else if (dialogMode_ == FileDialogMode::LoadInstrument) {
      Song next = song_;
      int idx = loadInstrumentFile(path, next, err);
      if (idx >= 0) {
        pushFullUndo();  // samples and wavetables may come along
        song_ = std::move(next);
        curIns_ = idx;
        dirty_ = true;
        setStatus("Loaded instrument " + song_.instruments[idx].name);
        cancel = true;
      } else {
        setStatus("Instrument load failed: " + err);
      }
    } else if (dialogMode_ == FileDialogMode::SaveInstrument) {
      if (saveInstrumentFile(song_, curIns_, path, err)) {
        setStatus("Saved instrument " + path);
        cancel = true;
      } else {
        setStatus("Instrument save failed: " + err);
      }
    } else if (dialogMode_ == FileDialogMode::ExportPlayer) {
      PlayerPayload p;
      p.song = song_.currentSubsongOnly().toText();  // the subsong on screen
      p.logo = playerLogo_;
      p.title = playerTitle_;
      p.scroll = playerScroll_;
      // The DATRACKARXD Keygen app makes lighter players than the tracker.
      if (writePlayer(path, p, err, findKeygenApp()))
        setStatus("Exported player " + path);
      else
        setStatus("Player export failed: " + err);
      cancel = true;
    } else if (dialogMode_ == FileDialogMode::ExportVgm) {
      std::string report;
      if (exportVGM(song_, path, err, report)) {
        setStatus("Exported " + path);
        exportReport_ = report;
        showExportReport_ = !report.empty();
      } else {
        setStatus("VGM export failed: " + err);
      }
      cancel = true;
    } else if (dialogMode_ == FileDialogMode::ExportModule) {
      ModuleExportOptions opt;
      opt.format = moduleFormatFromPath(path);
      opt.bakeRate = moduleRate_ == 0 ? 11025 : moduleRate_ == 2 ? 44100 : 22050;
      opt.holdSeconds = moduleHold_;
      std::string report;
      if (exportModule(song_, path, opt, err, report)) {
        setStatus("Exported " + path);
        exportReport_ = report;
        showExportReport_ = !report.empty();
      } else {
        setStatus("Module export failed: " + err);
      }
      cancel = true;
    } else if (dialogMode_ == FileDialogMode::ExportOgg) {
      if (exportOgg(song_, path, 44100, exportLoops_, oggQuality_, err))
        setStatus("Exported " + path);
      else
        setStatus("OGG export failed: " + err);
      cancel = true;
    } else if (dialogMode_ == FileDialogMode::ExportMidi) {
      if (exportMIDI(song_, path, exportLoops_, err))
        setStatus("Exported " + path);
      else
        setStatus("MIDI export failed: " + err);
      cancel = true;
    } else if (dialogMode_ == FileDialogMode::Save) {
      filePath_ = path;
      saveSong(false);
      cancel = !dirty_;
    } else {
      if (exportWav(song_, path, 44100, exportLoops_, err))
        setStatus("Exported " + path);
      else
        setStatus("Export failed: " + err);
      cancel = true;
    }
  }
  if (cancel) {
    dialogMode_ = FileDialogMode::None;
    ImGui::CloseCurrentPopup();
  }
  ImGui::EndPopup();
}

// ---------------------------------------------------------------------------

void App::menuBar() {
  if (!ImGui::BeginMainMenuBar()) return;
  if (ImGui::BeginMenu("File")) {
    if (ImGui::MenuItem("New", "Ctrl+N")) askAction(PendingAction::New);
    if (ImGui::MenuItem("Open / import...", "Ctrl+O")) askAction(PendingAction::Open);
    if (ImGui::MenuItem("Save", "Ctrl+S")) saveSong(false);
    if (ImGui::MenuItem("Save as...", "Ctrl+Shift+S")) saveSong(true);
    ImGui::Separator();
    if (ImGui::MenuItem("Export WAV...")) openFileDialog(FileDialogMode::ExportWav);
    if (ImGui::MenuItem("Export OGG...")) openFileDialog(FileDialogMode::ExportOgg);
    if (ImGui::MenuItem("Export MIDI...")) openFileDialog(FileDialogMode::ExportMidi);
    if (ImGui::MenuItem("Export module (XM, MOD, IT)...")) openFileDialog(FileDialogMode::ExportModule);
    if (ImGui::MenuItem("Export VGM...")) openFileDialog(FileDialogMode::ExportVgm);
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Register log of the emulated chips (AY, SN76489, Game Boy, OPL3)");
    if (ImGui::MenuItem("Export keygen player...")) {
      std::snprintf(playerTitle_, sizeof(playerTitle_), "%s", song_.name.c_str());
      if (!playerScroll_[0]) {
        std::string t = "Now playing: " + song_.name + (song_.author.empty() ? "" : "  by " + song_.author) +
                        "   ***   made with DATRACKARXD   ***   greetings to all trackers";
        std::snprintf(playerScroll_, sizeof(playerScroll_), "%s", t.c_str());
      }
      playerPopupRequest_ = true;
    }
    if (ImGui::MenuItem("Load demo song")) askAction(PendingAction::Demo);
    ImGui::Separator();
    if (ImGui::MenuItem("Quit")) requestQuit();
    ImGui::EndMenu();
  }
  if (ImGui::BeginMenu("Edit")) {
    if (ImGui::MenuItem("Undo", "Ctrl+Z", false, !undo_.empty())) applyUndo(undo_, redo_);
    if (ImGui::MenuItem("Redo", "Ctrl+Y", false, !redo_.empty())) applyUndo(redo_, undo_);
    ImGui::Separator();
    if (ImGui::MenuItem("Cut", "Ctrl+X")) copySelection(true);
    if (ImGui::MenuItem("Copy", "Ctrl+C")) copySelection(false);
    if (ImGui::MenuItem("Paste", "Ctrl+V")) paste();
    ImGui::Separator();
    if (ImGui::MenuItem("Transpose +1", "Ctrl+Up")) transpose(1);
    if (ImGui::MenuItem("Transpose -1", "Ctrl+Down")) transpose(-1);
    if (ImGui::MenuItem("Transpose +12", "Ctrl+Shift+Up")) transpose(12);
    if (ImGui::MenuItem("Transpose -12", "Ctrl+Shift+Down")) transpose(-12);
    ImGui::Separator();
    ImGui::TextDisabled("Selection (or the channel)");
    selectionToolsMenu();
    ImGui::EndMenu();
  }
  if (ImGui::BeginMenu("View")) {
    ImGui::MenuItem("Pattern", nullptr, &showPattern_);
    ImGui::MenuItem("Piano roll", nullptr, &showPianoRoll_);
    ImGui::MenuItem("Orders", nullptr, &showOrders_);
    ImGui::MenuItem("Instruments", nullptr, &showInstruments_);
    ImGui::MenuItem("Instrument editor", nullptr, &showInsEditor_);
    ImGui::MenuItem("Song", nullptr, &showSong_);
    ImGui::MenuItem("Samples", nullptr, &showSamples_);
    ImGui::MenuItem("Mixer", nullptr, &showMixer_);
    ImGui::MenuItem("Channel FX", nullptr, &showChanFx_);
    ImGui::MenuItem("Arrangement", nullptr, &showArrangement_);
    ImGui::MenuItem("Oscilloscope", nullptr, &showScope_);
    ImGui::MenuItem("Keygen scroller", nullptr, &showScroller_);
    ImGui::Separator();
    if (ImGui::BeginMenu("Theme")) {
      for (int i = 0; i < THEME_COUNT; i++)
        if (ImGui::MenuItem(THEMES[i].name, nullptr, themeIndex() == i)) {
          setTheme(i);
          if (i == 1) showScroller_ = true;  // the keygen look comes with its scroller
        }
      ImGui::EndMenu();
    }
    if (ImGui::MenuItem("Reset layout")) layoutDone_ = false;
    ImGui::EndMenu();
  }
  midiMenu();
  if (ImGui::BeginMenu("Help")) {
    ImGui::MenuItem("Effects", nullptr, &showEffects_);
    ImGui::MenuItem("Keyboard", nullptr, &showKeys_);
    ImGui::MenuItem("About", nullptr, &showAbout_);
    ImGui::EndMenu();
  }

  // Transport controls.
  ImGui::Separator();
  bool playing = engine_->playing();
  if (ImGui::MenuItem(playing ? "[Stop]" : "[Play]")) togglePlay(false);
  if (ImGui::MenuItem("[Play row]")) {
    engine_->stop();
    togglePlay(true);
  }
  ImGui::MenuItem("Loop pattern", nullptr, &engine_->loopPattern);
  ImGui::MenuItem("Follow", nullptr, &follow_);
  ImGui::PushStyleColor(ImGuiCol_Text, editMode_ ? ImVec4(1, 0.4f, 0.4f, 1) : ImGui::GetStyleColorVec4(ImGuiCol_Text));
  ImGui::MenuItem("Record", "Space", &editMode_);
  ImGui::PopStyleColor();
  ImGui::MenuItem("Metronome", nullptr, &engine_->metronome);
  if (ImGui::BeginMenu("Rec. options")) {
    ImGui::SetNextItemWidth(140);
    ImGui::Combo("Count-in", &countInBars_, "Off\0" "1 bar\0" "2 bars\0" "3 bars\0" "4 bars\0");
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Clicks before playback starts in Record mode (a bar = Highlight 2 rows)");
    ImGui::SetNextItemWidth(140);
    ImGui::Combo("MIDI quantize", &midiQuantize_, "Off\0" "Nearest row\0" "2 rows\0" "Beat\0");
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Live MIDI notes go to the nearest row of this grid instead of the row playing");
    ImGui::SetNextItemWidth(140);
    ImGui::SliderFloat("Metronome volume", &metronomeVol_, 0, 1, "%.2f");
    ImGui::EndMenu();
  }
  engine_->metronomeVolume = metronomeVol_;
  if (engine_->countingIn()) {
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1, 0.8f, 0.3f, 1));
    ImGui::TextUnformatted("count-in");
    ImGui::PopStyleColor();
  }
  ImGui::Separator();
  float bpm = song_.tickRate * 60.0f / (std::max(engine_->speed(), 1) * std::max(song_.highlight1, 1));
  ImGui::Text("Ord %02X  Row %02X  %.1f BPM", playing ? engine_->order() : curOrder_, playing ? engine_->row() : curRow_, bpm);
  if (!status_.empty() && ImGui::GetTime() - statusTime_ < 6.0) {
    ImGui::Separator();
    ImGui::TextUnformatted(status_.c_str());
  }
  ImGui::EndMainMenuBar();
}

// ---------------------------------------------------------------------------
// MIDI keyboard
// ---------------------------------------------------------------------------

void App::midiMenu() {
  if (!ImGui::BeginMenu("MIDI")) return;
  if (ImGui::IsWindowAppearing()) midiPorts_ = midi_.ports();
  if (ImGui::MenuItem("None", nullptr, !midi_.isOpen())) {
    midi_.close();
    midiPort_.clear();
  }
  for (const std::string& p : midiPorts_) {
    if (ImGui::MenuItem(p.c_str(), nullptr, midi_.portName() == p) && midi_.portName() != p) {
      if (midi_.open(p)) {
        midiPort_ = p;
        setStatus("MIDI input: " + p);
      } else {
        setStatus(midi_.error());
      }
    }
  }
  if (midiPorts_.empty()) ImGui::TextDisabled("%s", midi_.error().empty() ? "No MIDI input found" : midi_.error().c_str());
  ImGui::Separator();
  if (ImGui::MenuItem("Rescan")) midiPorts_ = midi_.ports();
  ImGui::MenuItem("Velocity -> volume", nullptr, &midiVelocity_);
  ImGui::SetNextItemWidth(120);
  ImGui::SliderInt("Pitch wheel range", &midiBendRange_, 1, 24, "%d semitones");
  if (ImGui::IsItemHovered()) ImGui::SetTooltip("In Record mode, while playing, the wheel is written as E8xx bends.");
  ImGui::Separator();
  ImGui::MenuItem("MPE controller", nullptr, &midiMpe_);
  if (ImGui::IsItemHovered())
    ImGui::SetTooltip("For MPE keyboards (Seaboard, Linnstrument, Osmose...): every note gets its own channel,\n"
                      "pitch bend, slide (CC74 -> filter cutoff) and pressure (-> volume).\n"
                      "Notes use the piano roll's Voices channels (8 if Voices is 1).\n"
                      "In Record mode while playing, all of it is written to the pattern.");
  ImGui::BeginDisabled(!midiMpe_);
  ImGui::SetNextItemWidth(120);
  ImGui::SliderInt("MPE pitch range", &midiMpeRange_, 1, 96, "%d semitones");
  ImGui::EndDisabled();
  ImGui::EndMenu();
}

void App::midiEvents() {
  for (const MidiMessage& m : midi_.drain()) {
    int type = m.status & 0xf0, mch = m.status & 15;
    int id = midiMpe_ ? m.data1 + 256 * mch : m.data1;
    if (type == 0x90 && m.data2 > 0) {
      int ch = midiNoteOn(m.data1, m.data2, id);
      if (midiMpe_ && ch >= 0) {
        MpeChannel& mc = mpe_[mch];
        mc.voice = ch;
        mc.velVol = midiVelocity_ ? std::clamp((int)m.data2, 1, 127) : 127;
        engine_->setBend(ch, mc.bend);  // the controller may have set it before the note
        if (mc.bend != 0) recordExpression(ch, mc.bend, -1, 0, -1);
      }
    } else if (type == 0x80 || type == 0x90) {
      midiNoteOff(id);
      if (midiMpe_) mpe_[mch].voice = -1;
    } else if (type == 0xc0 && m.data1 < song_.instruments.size()) {
      curIns_ = m.data1;  // program change picks the instrument
    } else if (type == 0xe0 && midiMpe_) {
      // MPE: this note's own pitch bend.
      MpeChannel& mc = mpe_[mch];
      mc.bend = ((m.data1 | (m.data2 << 7)) - 8192) / 8192.0 * midiMpeRange_;
      if (mc.voice >= 0) {
        engine_->setBend(mc.voice, mc.bend);
        recordExpression(mc.voice, mc.bend, -1, 0, -1);
      }
    } else if (type == 0xe0) {
      midiPitchWheel(m.data1 | (m.data2 << 7));
    } else if (type == 0xb0 && m.data1 == 74 && midiMpe_ && mpe_[mch].voice >= 0) {
      // MPE slide (timbre): the instrument's filter cutoff, like 13xx.
      int v = m.data2 * 2;
      engine_->setCutoff(mpe_[mch].voice, v);
      recordExpression(mpe_[mch].voice, std::nan(""), 0x13, v, -1);
    } else if (type == 0xd0 && midiMpe_ && mpe_[mch].voice >= 0) {
      // MPE pressure: shapes the note's volume, from 30% to its velocity.
      int vol = std::clamp((int)std::lround(mpe_[mch].velVol * (0.3 + 0.7 * m.data1 / 127.0)), 1, 127);
      engine_->setVolume(mpe_[mch].voice, vol);
      recordExpression(mpe_[mch].voice, std::nan(""), -1, 0, vol);
    }
  }
}

void App::recordExpression(int ch, double bend, int cmd, int val, int vol) {
  if (!editMode_ || !engine_->playing() || ch < 0 || ch >= song_.channelCount()) return;
  int order = engine_->order(), row = engine_->row();
  auto it = midiRecRow_.find(ch);
  if (it == midiRecRow_.end() || it->second != std::make_pair(order, row)) {
    pushUndo(ch, ch, order);  // one undo step per row and channel
    midiRecRow_[ch] = {order, row};
  }
  Cell* cell = song_.cell(ch, order, row, true);
  if (!cell) return;
  if (!std::isnan(bend)) {
    int bcmd, bval;
    bendEffect(bend, bcmd, bval);
    removeCellEffect(*cell, 0xE8);
    removeCellEffect(*cell, 0xE9);
    putEffect(ch, *cell, bcmd, bval);
    if (bend != 0) midiBent_.insert(ch);
    else midiBent_.erase(ch);
  }
  if (cmd >= 0) putEffect(ch, *cell, cmd, val);
  if (vol >= 0) cell->vol = (int16_t)vol;
  dirty_ = true;
}

// The wheel bends the channels the MIDI keyboard plays on (the piano roll's
// voice channels). In Record mode while playing it is written as E8/E9.
void App::midiPitchWheel(int value) {
  midiBend_ = (value - 8192) / 8192.0 * midiBendRange_;
  int nch = song_.channelCount();
  if (curCh_ >= nch) return;
  int voices = std::clamp(prVoices_, 1, nch - curCh_);
  for (int c = curCh_; c < curCh_ + voices; c++) {
    engine_->setBend(c, midiBend_);
    recordExpression(c, midiBend_, -1, 0, -1);
  }
}

// MIDI note 60 (middle C) is C-4, like the computer keyboard at octave 4.
// Held notes spread over the piano roll's voice channels. In Record mode
// they are written at the cursor (stopped: the cursor moves on once every
// key is up) or at the playing row (live: key up writes a note off).
int App::midiNoteOn(int key, int velocity, int id) {
  if (id < 0) id = key;
  if (midiHeld_.count(id)) midiNoteOff(id);
  int nch = song_.channelCount();
  if (curCh_ >= nch) return -1;
  int note = std::clamp(key - 12, 0, 119);
  // MPE needs a channel per finger: 8 voices when Voices is left at 1.
  int voices = std::clamp(midiMpe_ && prVoices_ == 1 ? 8 : prVoices_, 1, nch - curCh_);
  auto used = [&](int c) {
    for (auto& h : midiHeld_)
      if (h.second.ch == c) return true;
    return false;
  };
  int ch = -1;
  for (int c = curCh_; c < curCh_ + voices && ch < 0; c++)
    if (!used(c)) ch = c;
  if (ch < 0) {  // all voices busy: take over the last one
    ch = curCh_ + voices - 1;
    for (auto it = midiHeld_.begin(); it != midiHeld_.end();)
      it = it->second.ch == ch ? midiHeld_.erase(it) : std::next(it);
  }
  int vol = midiVelocity_ ? std::clamp(velocity, 1, 127) : -1;
  engine_->noteOn(ch, note, curIns_, vol);
  MidiHeld held{ch, -1, -1};

  if (editMode_) {
    bool live = engine_->playing();
    int order = live ? engine_->order() : curOrder_;
    int row = live ? engine_->row() : curRow_;
    if (live && midiQuantize_ > 0 && !engine_->countingIn()) {
      // Round the time the key was hit to the nearest grid row.
      int grid = midiQuantize_ == 1 ? 1 : midiQuantize_ == 2 ? 2 : std::max(1, song_.highlight1);
      double pos = row + engine_->currentTick() / (double)std::max(1, engine_->speed());
      row = (int)std::lround(pos / grid) * grid;
      if (row >= song_.patternLength) {
        row = 0;
        order = order + 1 < (int)song_.orders.size() ? order + 1 : 0;
      }
    }
    bool chordStep = !live && midiChord_ && !undo_.empty() && undo_.back().kind == UndoStep::Patterns;
    if (chordStep) {
      // Later notes of a chord join the undo step of the first one.
      UndoStep& step = undo_.back();
      bool have = false;
      for (auto& e : step.entries) have |= e.ch == ch;
      if (!have) {
        int pat = song_.orders[order][ch];
        const Pattern* p = song_.pattern(ch, pat);
        step.entries.push_back({ch, pat, p ? p->rows : std::vector<Cell>()});
      }
    } else {
      pushUndo(ch, ch, order);
    }
    Cell* c = song_.cell(ch, order, row, true);
    c->note = (int16_t)note;
    c->ins = (int16_t)curIns_;
    if (vol >= 0) c->vol = (int16_t)vol;
    dirty_ = true;
    held = {ch, order, row};
    if (!live) midiChord_ = true;
  }
  midiHeld_[id] = held;
  return ch;
}

void App::midiNoteOff(int id) {
  auto it = midiHeld_.find(id);
  if (it == midiHeld_.end()) return;
  MidiHeld h = it->second;
  midiHeld_.erase(it);
  engine_->noteOff(h.ch);

  if (h.row >= 0 && editMode_ && engine_->playing() && h.ch < song_.channelCount()) {
    int order = engine_->order(), row = engine_->row();
    if (order == h.order && row <= h.row) row = h.row + 1;  // released within its own (or a quantized later) row
    if (row < song_.patternLength && order < (int)song_.orders.size()) {
      Cell* c = song_.cell(h.ch, order, row, false);
      if (!c || c->note == NOTE_EMPTY) {
        pushUndo(h.ch, h.ch, order);
        c = song_.cell(h.ch, order, row, true);
        c->note = NOTE_OFF;
        c->ins = -1;
      }
      if (midiBent_.erase(h.ch) && c && c->note == NOTE_OFF && cellEffect(*c, 0xE8) == -1 &&
          cellEffect(*c, 0xE9) == -1)
        putEffect(h.ch, *c, 0xE8, 0x80);  // the next note starts unbent
    }
  }
  if (midiChord_ && midiHeld_.empty()) {
    midiChord_ = false;
    if (!engine_->playing()) moveRows(editStep_);
  }
}

void App::defaultLayout(unsigned int dockId) {
  ImGui::DockBuilderRemoveNode(dockId);
  ImGui::DockBuilderAddNode(dockId, ImGuiDockNodeFlags_DockSpace);
  ImGui::DockBuilderSetNodeSize(dockId, ImGui::GetMainViewport()->WorkSize);
  ImGuiID center = dockId;
  ImGuiID left = ImGui::DockBuilderSplitNode(center, ImGuiDir_Left, 0.20f, nullptr, &center);
  ImGuiID right = ImGui::DockBuilderSplitNode(center, ImGuiDir_Right, 0.30f, nullptr, &center);
  ImGuiID bottom = ImGui::DockBuilderSplitNode(center, ImGuiDir_Down, 0.18f, nullptr, &center);
  ImGuiID leftBottom = ImGui::DockBuilderSplitNode(left, ImGuiDir_Down, 0.45f, nullptr, &left);
  ImGuiID rightBottom = ImGui::DockBuilderSplitNode(right, ImGuiDir_Down, 0.70f, nullptr, &right);
  ImGui::DockBuilderDockWindow("Pattern", center);
  ImGui::DockBuilderDockWindow("Piano roll", center);
  ImGui::DockBuilderDockWindow("Orders", left);
  ImGui::DockBuilderDockWindow("Song", leftBottom);
  ImGui::DockBuilderDockWindow("Instruments", right);
  ImGui::DockBuilderDockWindow("Instrument editor", rightBottom);
  ImGui::DockBuilderDockWindow("Samples", center);
  ImGui::DockBuilderDockWindow("Arrangement", center);
  ImGui::DockBuilderDockWindow("Oscilloscope", bottom);
  ImGui::DockBuilderDockWindow("Keygen", bottom);
  ImGui::DockBuilderDockWindow("Mixer", bottom);
  ImGui::DockBuilderDockWindow("Channel FX", bottom);
  ImGui::DockBuilderDockWindow("Effects", rightBottom);
  ImGui::DockBuilderDockWindow("Keyboard", rightBottom);
  ImGui::DockBuilderFinish(dockId);
}

void App::globalShortcut(const KeyEvent& ev) {
  bool ctrl = ev.mod & KMOD_CTRL, shift = ev.mod & KMOD_SHIFT;
  if (ctrl) {
    switch (ev.sc) {
      case SDL_SCANCODE_N: askAction(PendingAction::New); return;
      case SDL_SCANCODE_O: askAction(PendingAction::Open); return;
      case SDL_SCANCODE_S: saveSong(shift); return;
      default: break;
    }
    // Undo/redo follow the keyboard layout (Ctrl+Z is on W in AZERTY).
    if (ev.key == SDLK_z) applyUndo(undo_, redo_);
    if (ev.key == SDLK_y) applyUndo(redo_, undo_);
    return;
  }
  switch (ev.sc) {
    case SDL_SCANCODE_RETURN: togglePlay(shift); break;
    case SDL_SCANCODE_F5: engine_->stop(); engine_->play(0, 0); break;
    case SDL_SCANCODE_F6: engine_->stop(); togglePlay(true); break;
    case SDL_SCANCODE_F8: engine_->stop(); break;
    case SDL_SCANCODE_KP_DIVIDE: octave_ = std::max(0, octave_ - 1); break;
    case SDL_SCANCODE_KP_MULTIPLY: octave_ = std::min(8, octave_ + 1); break;
    default: {
      // Play the current instrument from the keyboard.
      int n = scancodeToNote(ev.sc);
      if (n >= 0 && !ev.repeat) {
        int note = std::clamp(octave_ * 12 + n, 0, 119);
        engine_->noteOn(curCh_, note, curIns_);
        jamming_[ev.sc] = curCh_;
      }
      break;
    }
  }
}

void App::handleKeys() {
  ImGuiIO& io = ImGui::GetIO();
  std::vector<KeyEvent> keys;
  keys.swap(keys_);
  for (const KeyEvent& ev : keys) {
    if (!ev.down) {
      auto it = jamming_.find(ev.sc);
      if (it != jamming_.end()) {
        engine_->noteOff(it->second);
        jamming_.erase(it);
      }
      continue;
    }
    if (io.WantTextInput || ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId)) continue;
    if (patternFocused_)
      patternKey(ev);
    else
      globalShortcut(ev);
  }
}

void App::frame() {
  midiEvents();
  if (engine_->playing() && follow_) setCursorFromPlayback();

  menuBar();
  ImGuiID dock = ImGui::DockSpaceOverViewport(0, ImGui::GetMainViewport());
  if (!layoutDone_) {
    ImGuiDockNode* node = ImGui::DockBuilderGetNode(dock);
    bool hasSavedLayout = node && node->IsSplitNode();
    if (!hasSavedLayout || ImGui::GetFrameCount() > 1) defaultLayout(dock);
    layoutDone_ = true;
  }

  if (showPianoRoll_) pianoRollWindow();
  if (showPattern_) patternWindow();
  if (showOrders_) ordersWindow();
  if (showInstruments_) instrumentsWindow();
  if (showInsEditor_) instrumentEditor();
  if (showSong_) songWindow();
  if (showSamples_) samplesWindow();
  if (showMixer_) mixerWindow();
  if (showChanFx_) channelFxWindow();
  if (showArrangement_) arrangementWindow();
  if (showFindReplace_) findReplaceWindow();
  if (showScope_) scopeWindow();
  if (showScroller_) scrollerWindow();
  if (showEffects_) effectsHelp();
  if (showKeys_) keysHelp();
  if (showAbout_) aboutWindow();

  playerExportPopup();
  if (showExportReport_) {
    ImGui::SetNextWindowSize(ImVec2(460, 0), ImGuiCond_Appearing);
    if (ImGui::Begin("Export report", &showExportReport_)) {
      ImGui::TextWrapped("Exported. These parts had no equivalent in the format and were left out:");
      ImGui::Separator();
      ImGui::TextUnformatted(exportReport_.c_str());
      if (ImGui::Button("OK")) showExportReport_ = false;
    }
    ImGui::End();
  }
  fileDialog();
  confirmPopup();
  handleKeys();
  // A settings edit becomes one undo step once the mouse/text gesture ends.
  if (shadowStale_ || (metaTouched_ && !ImGui::IsAnyItemActive())) commitMetaEdit();
}
