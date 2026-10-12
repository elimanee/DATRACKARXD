// Settings window, recent files, autosave and crash recovery, dropped files.
#include <imgui.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <functional>
#include <sstream>

#include "app.h"
#include "audiofile.h"
#include "i18n.h"
#include "import.h"
#include "paths.h"
#include "theme.h"

namespace fs = std::filesystem;

namespace {

const char* AUTOSAVE_FILE = "autosave.dtk";
const char* AUTOSAVE_INFO = "autosave.txt";  // the song's own path, if it had one
const int BUFFER_SIZES[] = {128, 256, 512, 1024, 2048, 4096};
const float UI_SCALES[] = {0.75f, 1.0f, 1.25f, 1.5f, 1.75f, 2.0f, 2.5f};
const int AUTOSAVE_MINUTES[] = {0, 1, 2, 5, 10};

std::string lower(std::string s) {
  for (char& c : s) c = (char)std::tolower((unsigned char)c);
  return s;
}

bool hasExtension(const std::string& path, const std::vector<std::string>& exts) {
  std::string e = lower(fs::path(path).extension().string());
  return std::find(exts.begin(), exts.end(), e) != exts.end();
}

}  // namespace

// ---------------------------------------------------------------------------
// Recent files, autosave, recovery
// ---------------------------------------------------------------------------

void App::addRecent(const std::string& path) {
  std::error_code ec;
  fs::path abs = fs::absolute(path, ec);
  std::string p = ec ? path : abs.lexically_normal().string();
  recent_.erase(std::remove(recent_.begin(), recent_.end(), p), recent_.end());
  recent_.insert(recent_.begin(), p);
  if (recent_.size() > MAX_RECENT) recent_.resize(MAX_RECENT);
}

// Every few minutes, unsaved changes go to the settings folder. A clean
// exit (or a save) removes the file; finding it at startup means the last
// session ended without saving.
void App::autosaveTick() {
  if (recoverRequest_) return;  // don't overwrite what may be recovered
  double now = SDL_GetTicks64() / 1000.0;
  if (!dirty_) {
    if (autosaved_) clearAutosave();
    lastAutosave_ = now;
    return;
  }
  if (autosaveMinutes_ <= 0 || now - lastAutosave_ < autosaveMinutes_ * 60.0) return;
  lastAutosave_ = now;
  std::string text = song_.toText();
  size_t h = std::hash<std::string>()(text);
  if (autosaved_ && h == autosaveHash_) return;
  std::string tmp = configPath("autosave.tmp");
  {
    std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
    f << text;
    if (!f) return;
  }
  std::error_code ec;
  fs::rename(tmp, configPath(AUTOSAVE_FILE), ec);
  if (ec) return;
  std::ofstream(configPath(AUTOSAVE_INFO)) << filePath_ << "\n";
  autosaved_ = true;
  autosaveHash_ = h;
}

void App::clearAutosave() {
  std::error_code ec;
  fs::remove(configPath(AUTOSAVE_FILE), ec);
  fs::remove(configPath(AUTOSAVE_INFO), ec);
  autosaved_ = false;
}

void App::checkRecovery() {
  std::error_code ec;
  recoverRequest_ = fs::exists(configPath(AUTOSAVE_FILE), ec);
  recoverFrom_.clear();
  std::ifstream f(configPath(AUTOSAVE_INFO));
  std::getline(f, recoverFrom_);
  if (!recoverFrom_.empty() && recoverFrom_.back() == '\r') recoverFrom_.pop_back();
}

void App::recoveryPopup() {
  if (recoverRequest_ && !ImGui::IsPopupOpen("###recover")) ImGui::OpenPopup("###recover");
  ImGui::SetNextWindowSize(ImVec2(S(480), 0), ImGuiCond_Appearing);
  if (!ImGui::BeginPopupModal((std::string(T("Recover unsaved work?")) + "###recover").c_str(), nullptr,
                              ImGuiWindowFlags_AlwaysAutoResize))
    return;
  ImGui::TextWrapped("%s", T("DATRACKARXD was closed without saving a song. Its last autosave can be opened."));
  std::error_code ec;
  auto when = fs::last_write_time(configPath(AUTOSAVE_FILE), ec);
  if (!ec) {
    auto sys = std::chrono::time_point_cast<std::chrono::system_clock::duration>(
        when - fs::file_time_type::clock::now() + std::chrono::system_clock::now());
    std::time_t t = std::chrono::system_clock::to_time_t(sys);
    char buf[64];
    std::strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M", std::localtime(&t));
    ImGui::TextDisabled("%s %s", T("Autosaved:"), buf);
  }
  if (!recoverFrom_.empty()) ImGui::TextDisabled("%s %s", T("File:"), recoverFrom_.c_str());
  ImGui::Spacing();
  if (ImGui::Button(T("Recover"), ImVec2(S(140), 0))) {
    std::string err;
    Song s;
    std::ifstream f(configPath(AUTOSAVE_FILE), std::ios::binary);
    std::stringstream ss;
    ss << f.rdbuf();
    if (s.fromText(ss.str(), err)) {
      engine_->stop();
      song_ = std::move(s);
      afterSongReplaced();
      filePath_ = recoverFrom_;
      dirty_ = true;  // still unsaved
      setStatus(T("Recovered the autosaved song"));
    } else {
      setStatus(std::string(T("Recovery failed: ")) + err);
    }
    recoverRequest_ = false;
    autosaved_ = true;  // the file stays until the song is saved
    autosaveHash_ = 0;
    ImGui::CloseCurrentPopup();
  }
  ImGui::SameLine();
  if (ImGui::Button(T("Discard"), ImVec2(S(140), 0))) {
    clearAutosave();
    recoverRequest_ = false;
    ImGui::CloseCurrentPopup();
  }
  ImGui::EndPopup();
}

// Songs open (asking first about unsaved changes), instruments and samples
// are added to the song.
void App::dropFile(const std::string& path) {
  std::string err;
  if (hasExtension(path, supportedSongExtensions())) {
    droppedPath_ = path;
    askAction(PendingAction::OpenPath);
  } else if (hasExtension(path, instrumentFileExtensions())) {
    Song next = song_;
    int idx = loadInstrumentFile(path, next, err);
    if (idx >= 0) {
      pushFullUndo();
      song_ = std::move(next);
      curIns_ = idx;
      dirty_ = true;
      setStatus(std::string(T("Loaded instrument ")) + song_.instruments[idx].name);
    } else {
      setStatus(std::string(T("Instrument load failed: ")) + err);
    }
  } else if (hasExtension(path, {".flac", ".mp3", ".ogg", ".oga"})) {
    Sample smp;
    if ((int)song_.samples.size() >= MAX_SAMPLES) {
      setStatus(T("Too many samples"));
    } else if (loadAudioSample(path, smp, err)) {
      pushFullUndo();
      song_.samples.push_back(std::move(smp));
      curSample_ = (int)song_.samples.size() - 1;
      showSamples_ = true;
      dirty_ = true;
      setStatus(std::string(T("Loaded sample ")) + path);
    } else {
      setStatus(std::string(T("Sample load failed: ")) + err);
    }
  } else {
    setStatus(std::string(T("Unknown file type: ")) + fs::path(path).filename().string());
  }
}

// ---------------------------------------------------------------------------
// Settings window
// ---------------------------------------------------------------------------

void App::settingsWindow() {
  ImGui::SetNextWindowSize(ImVec2(S(620), S(520)), ImGuiCond_FirstUseEver);
  if (!ImGui::Begin(TW("Settings").c_str(), &showSettings_)) {
    ImGui::End();
    capturing_ = -1;
    return;
  }
  if (!ImGui::BeginTabBar("settingstabs")) {
    ImGui::End();
    return;
  }

  if (ImGui::BeginTabItem(T("Audio"))) {
    if (ImGui::IsItemActivated() || audioDevices_.empty()) {
      audioDevices_.clear();
      int n = SDL_GetNumAudioDevices(0);
      for (int i = 0; i < n; i++)
        if (const char* name = SDL_GetAudioDeviceName(i, 0)) audioDevices_.push_back(name);
    }
    std::string cur = audioDevice_.empty() ? T("System default") : audioDevice_;
    ImGui::SetNextItemWidth(S(-160));
    if (ImGui::BeginCombo(T("Output device"), cur.c_str())) {
      if (ImGui::Selectable(T("System default"), audioDevice_.empty())) {
        audioDevice_.clear();
        audioReopen_ = true;
      }
      for (const std::string& d : audioDevices_)
        if (ImGui::Selectable(d.c_str(), d == audioDevice_)) {
          audioDevice_ = d;
          audioReopen_ = true;
        }
      ImGui::EndCombo();
    }
    ImGui::SameLine();
    if (ImGui::Button(T("Rescan"))) audioDevices_.clear();

    int bi = 2;
    for (int i = 0; i < (int)(sizeof(BUFFER_SIZES) / sizeof(int)); i++)
      if (BUFFER_SIZES[i] == audioBuffer_) bi = i;
    char label[64];
    std::snprintf(label, sizeof(label), "%d (%.1f ms)", audioBuffer_, audioBuffer_ * 1000.0 / 44100);
    ImGui::SetNextItemWidth(S(-160));
    if (ImGui::BeginCombo(T("Buffer size"), label)) {
      for (int i = 0; i < (int)(sizeof(BUFFER_SIZES) / sizeof(int)); i++) {
        std::snprintf(label, sizeof(label), "%d (%.1f ms)", BUFFER_SIZES[i], BUFFER_SIZES[i] * 1000.0 / 44100);
        if (ImGui::Selectable(label, i == bi)) {
          audioBuffer_ = BUFFER_SIZES[i];
          audioReopen_ = true;
        }
      }
      ImGui::EndCombo();
    }
    ImGui::TextWrapped("%s", T("Smaller buffers answer the keyboard faster; raise it if the sound crackles."));
    ImGui::Spacing();
    ImGui::TextDisabled("%s", audioStatus_.c_str());
    if (audioDev_) ImGui::TextDisabled(T("%s %.1f ms"), T("Latency:"), audioLatencyMs_);
    ImGui::EndTabItem();
  }

  if (ImGui::BeginTabItem(T("Interface"))) {
    int lang = language_;
    ImGui::SetNextItemWidth(S(220));
    if (ImGui::BeginCombo(T("Language"), languageName(lang))) {
      for (int i = 0; i < LANGUAGE_COUNT; i++)
        if (ImGui::Selectable(languageName(i), i == lang)) {
          language_ = i;
          setLanguage(i);
        }
      ImGui::EndCombo();
    }
    char scaleText[16];
    std::snprintf(scaleText, sizeof(scaleText), "%d%%", (int)std::lround(100));
    ImGui::SetNextItemWidth(S(220));
    if (ImGui::BeginCombo(T("Interface size"), scaleText)) {
      for (float s : UI_SCALES) {
        std::snprintf(scaleText, sizeof(scaleText), "%d%%", (int)std::lround(s * 100));
        if (ImGui::Selectable(scaleText, std::abs(s - uiScale_) < 0.01f)) {
          uiScale_ = s;
          setThemeScale(s);
          fontDirty_ = true;
        }
      }
      ImGui::EndCombo();
    }
    int th = themeIndex();
    ImGui::SetNextItemWidth(S(220));
    if (ImGui::BeginCombo(T("Theme"), THEMES[th].name)) {
      for (int i = 0; i < THEME_COUNT; i++)
        if (ImGui::Selectable(THEMES[i].name, i == th)) setTheme(i);
      ImGui::EndCombo();
    }
    ImGui::EndTabItem();
  }

  if (ImGui::BeginTabItem(T("Keyboard"))) {
    ImGui::TextWrapped("%s", T("Click a shortcut, then press the new key (with Ctrl, Shift or Alt if you like). "
                               "Esc cancels, Backspace removes the shortcut. Notes and cursor keys stay fixed."));
    if (ImGui::Button(T("Restore the defaults"))) {
      shortcuts_ = Shortcuts();
      capturing_ = -1;
    }
    if (ImGui::BeginTable("keys", 3, ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY | ImGuiTableFlags_BordersInnerH,
                          ImVec2(0, ImGui::GetContentRegionAvail().y))) {
      ImGui::TableSetupColumn(T("Action"), ImGuiTableColumnFlags_WidthStretch);
      ImGui::TableSetupColumn(T("Shortcut"), ImGuiTableColumnFlags_WidthFixed, S(190));
      ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed, S(70));
      ImGui::TableSetupScrollFreeze(0, 1);
      ImGui::TableHeadersRow();
      for (int i = 0; i < ACTION_COUNT; i++) {
        ImGui::PushID(i);
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted(T(actionName((Action)i)));
        Action other = shortcuts_.conflict((Action)i);
        if (other != Action::Count) {
          ImGui::SameLine();
          ImGui::TextColored(ImVec4(1, 0.5f, 0.3f, 1), "(%s %s)", T("same key as"), T(actionName(other)));
        }
        ImGui::TableNextColumn();
        std::string text = capturing_ == i ? T("press a key...") : bindingText(shortcuts_.bind[i]);
        if (text.empty()) text = "-";
        if (ImGui::Button(text.c_str(), ImVec2(-1, 0))) capturing_ = capturing_ == i ? -1 : i;
        ImGui::TableNextColumn();
        bool isDefault = shortcuts_.bind[i] == defaultBinding((Action)i);
        ImGui::BeginDisabled(isDefault);
        if (ImGui::SmallButton(T("Default"))) shortcuts_.bind[i] = defaultBinding((Action)i);
        ImGui::EndDisabled();
        ImGui::PopID();
      }
      ImGui::EndTable();
    }
    ImGui::EndTabItem();
  } else {
    capturing_ = -1;
  }

  if (ImGui::BeginTabItem(T("Files"))) {
    int ai = 2;
    for (int i = 0; i < (int)(sizeof(AUTOSAVE_MINUTES) / sizeof(int)); i++)
      if (AUTOSAVE_MINUTES[i] == autosaveMinutes_) ai = i;
    auto autosaveText = [&](int m) {
      char b[64];
      if (m == 0) std::snprintf(b, sizeof(b), "%s", T("Off"));
      else std::snprintf(b, sizeof(b), T("Every %d min"), m);
      return std::string(b);
    };
    ImGui::SetNextItemWidth(S(220));
    if (ImGui::BeginCombo(T("Autosave"), autosaveText(autosaveMinutes_).c_str())) {
      for (int i = 0; i < (int)(sizeof(AUTOSAVE_MINUTES) / sizeof(int)); i++)
        if (ImGui::Selectable(autosaveText(AUTOSAVE_MINUTES[i]).c_str(), i == ai)) autosaveMinutes_ = AUTOSAVE_MINUTES[i];
      ImGui::EndCombo();
    }
    ImGui::TextWrapped("%s", T("Unsaved changes are copied to the settings folder. If DATRACKARXD closes without "
                               "saving (crash, power cut), the copy is offered at the next start."));
    ImGui::Spacing();
    ImGui::TextDisabled("%s %s", T("Settings folder:"), configDir().c_str());
    if (ImGui::Button(T("Clear the recent files"))) recent_.clear();
    ImGui::EndTabItem();
  }
  ImGui::EndTabBar();
  ImGui::End();
}
