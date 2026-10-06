#include "app.h"

#include <imgui.h>
#include <imgui_internal.h>

#include <algorithm>
#include <cstring>
#include <filesystem>

#include "demo.h"

namespace fs = std::filesystem;

static void audioCallback(void* user, Uint8* stream, int len) {
  Engine* engine = static_cast<Engine*>(user);
  std::lock_guard<std::mutex> lock(engine->mutex);
  engine->render(reinterpret_cast<float*>(stream), len / (int)(2 * sizeof(float)));
}

App::App() {
  loadDemoSong(song_);
  engine_ = std::make_unique<Engine>(song_, 44100);
  std::error_code ec;
  dialogDir_ = fs::current_path(ec).string();
}

App::~App() {
  if (audioDev_) SDL_CloseAudioDevice(audioDev_);
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
  curOrder_ = curRow_ = curCh_ = curCol_ = 0;
  curIns_ = 0;
  hasSel_ = false;
  undo_.clear();
  redo_.clear();
  dirty_ = false;
}

bool App::loadFile(const std::string& path) {
  std::string err;
  if (!song_.load(path, err)) {
    setStatus("Load failed: " + err);
    return false;
  }
  afterSongReplaced();
  filePath_ = path;
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
    engine_->play(curOrder_, fromCursor ? curRow_ : 0);
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
  std::snprintf(dialogName_, sizeof(dialogName_), "%s", base.c_str());
}

void App::fileDialog() {
  const char* title = dialogMode_ == FileDialogMode::Open   ? "Open song###filedlg"
                      : dialogMode_ == FileDialogMode::Save ? "Save song###filedlg"
                                                            : "Export WAV###filedlg";
  if (dialogOpenRequest_) {
    ImGui::OpenPopup("###filedlg");
    dialogOpenRequest_ = false;
  }
  ImGui::SetNextWindowSize(ImVec2(560, 420), ImGuiCond_Appearing);
  if (!ImGui::BeginPopupModal(title, nullptr)) return;

  std::string ext = dialogMode_ == FileDialogMode::ExportWav ? ".wav" : ".dtk";
  ImGui::TextUnformatted(dialogDir_.c_str());
  if (ImGui::Button("Up")) {
    fs::path p = fs::path(dialogDir_).parent_path();
    if (!p.empty()) dialogDir_ = p.string();
  }
  ImGui::SameLine();
  ImGui::TextDisabled("(showing folders and *%s files)", ext.c_str());

  bool accept = false;
  ImGui::BeginChild("files", ImVec2(0, -ImGui::GetFrameHeightWithSpacing() * 2.2f), ImGuiChildFlags_Borders);
  std::vector<fs::directory_entry> dirs, files;
  std::error_code ec;
  for (fs::directory_iterator it(dialogDir_, ec), end; !ec && it != end; it.increment(ec)) {
    std::error_code ec2;
    if (it->is_directory(ec2))
      dirs.push_back(*it);
    else if (it->path().extension() == ext)
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
  if (dialogMode_ == FileDialogMode::ExportWav) {
    ImGui::SameLine();
    ImGui::SetNextItemWidth(80);
    ImGui::InputInt("loops", &exportLoops_);
    exportLoops_ = std::clamp(exportLoops_, 1, 16);
  }
  if (ImGui::Button(dialogMode_ == FileDialogMode::Open ? "Open" : "Save", ImVec2(100, 0))) accept = true;
  ImGui::SameLine();
  bool cancel = ImGui::Button("Cancel", ImVec2(100, 0)) || ImGui::IsKeyPressed(ImGuiKey_Escape);

  if (accept && dialogName_[0]) {
    fs::path p = fs::path(dialogName_);
    if (p.is_relative()) p = fs::path(dialogDir_) / p;
    if (dialogMode_ != FileDialogMode::Open && p.extension() != ext) p += ext;
    std::string path = p.string();
    std::string err;
    if (dialogMode_ == FileDialogMode::Open) {
      engine_->stop();
      if (loadFile(path)) cancel = true;
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
    if (ImGui::MenuItem("Open...", "Ctrl+O")) askAction(PendingAction::Open);
    if (ImGui::MenuItem("Save", "Ctrl+S")) saveSong(false);
    if (ImGui::MenuItem("Save as...", "Ctrl+Shift+S")) saveSong(true);
    ImGui::Separator();
    if (ImGui::MenuItem("Export WAV...")) openFileDialog(FileDialogMode::ExportWav);
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
    ImGui::EndMenu();
  }
  if (ImGui::BeginMenu("View")) {
    ImGui::MenuItem("Pattern", nullptr, &showPattern_);
    ImGui::MenuItem("Orders", nullptr, &showOrders_);
    ImGui::MenuItem("Instruments", nullptr, &showInstruments_);
    ImGui::MenuItem("Instrument editor", nullptr, &showInsEditor_);
    ImGui::MenuItem("Song", nullptr, &showSong_);
    ImGui::MenuItem("Oscilloscope", nullptr, &showScope_);
    ImGui::Separator();
    if (ImGui::MenuItem("Reset layout")) layoutDone_ = false;
    ImGui::EndMenu();
  }
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
  ImGui::MenuItem("Edit", "Space", &editMode_);
  ImGui::PopStyleColor();
  ImGui::Separator();
  float bpm = song_.tickRate * 60.0f / (std::max(engine_->speed(), 1) * std::max(song_.highlight1, 1));
  ImGui::Text("Ord %02X  Row %02X  %.1f BPM", playing ? engine_->order() : curOrder_, playing ? engine_->row() : curRow_, bpm);
  if (!status_.empty() && ImGui::GetTime() - statusTime_ < 6.0) {
    ImGui::Separator();
    ImGui::TextUnformatted(status_.c_str());
  }
  ImGui::EndMainMenuBar();
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
  ImGui::DockBuilderDockWindow("Orders", left);
  ImGui::DockBuilderDockWindow("Song", leftBottom);
  ImGui::DockBuilderDockWindow("Instruments", right);
  ImGui::DockBuilderDockWindow("Instrument editor", rightBottom);
  ImGui::DockBuilderDockWindow("Oscilloscope", bottom);
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
  if (engine_->playing() && follow_) setCursorFromPlayback();

  menuBar();
  ImGuiID dock = ImGui::DockSpaceOverViewport(0, ImGui::GetMainViewport());
  if (!layoutDone_) {
    ImGuiDockNode* node = ImGui::DockBuilderGetNode(dock);
    bool hasSavedLayout = node && node->IsSplitNode();
    if (!hasSavedLayout || ImGui::GetFrameCount() > 1) defaultLayout(dock);
    layoutDone_ = true;
  }

  if (showPattern_) patternWindow();
  if (showOrders_) ordersWindow();
  if (showInstruments_) instrumentsWindow();
  if (showInsEditor_) instrumentEditor();
  if (showSong_) songWindow();
  if (showScope_) scopeWindow();
  if (showEffects_) effectsHelp();
  if (showKeys_) keysHelp();
  if (showAbout_) aboutWindow();

  fileDialog();
  confirmPopup();
  handleKeys();
}
