// Sample browser: walk the folders, hear a sound before loading it, then
// add it to the song, to a new instrument or to a drum kit pad.
#include <imgui.h>

#include <algorithm>
#include <cctype>
#include <filesystem>

#include "app.h"
#include "audiofile.h"
#include "i18n.h"
#include "theme.h"

namespace fs = std::filesystem;

namespace {

std::string lowerCase(std::string s) {
  for (char& c : s) c = (char)std::tolower((unsigned char)c);
  return s;
}

}  // namespace

void App::browserScan() {
  browserEntries_.clear();
  std::error_code ec;
  if (browseDir_.empty() || !fs::is_directory(browseDir_, ec)) browseDir_ = fs::current_path(ec).string();
  for (fs::directory_iterator it(browseDir_, fs::directory_options::skip_permission_denied, ec), end; !ec && it != end;
       it.increment(ec)) {
    std::string name = it->path().filename().string();
    if (name.empty() || name[0] == '.') continue;
    bool dir = it->is_directory(ec);
    if (!dir && !isAudioFile(name)) continue;
    browserEntries_.push_back({name, dir});
  }
  std::sort(browserEntries_.begin(), browserEntries_.end(), [](const BrowserEntry& a, const BrowserEntry& b) {
    if (a.dir != b.dir) return a.dir;
    return lowerCase(a.name) < lowerCase(b.name);
  });
  browserScanned_ = browseDir_;
}

// Decodes a file for the preview and the buttons; plays it if asked.
void App::browserSelect(const std::string& name, bool play) {
  browserSel_ = name;
  std::string path = (fs::path(browseDir_) / name).string();
  auto s = std::make_shared<Sample>();
  std::string err;
  if (!loadAudioSample(path, *s, err)) {
    browserSample_.reset();
    browserError_ = err;
    return;
  }
  s->name = fs::path(name).stem().string();
  browserError_.clear();
  browserSample_ = s;
  if (play) engine_->previewSample(browserSample_);
}

void App::sampleBrowserWindow() {
  ImGui::SetNextWindowSize(ImVec2(S(520), S(460)), ImGuiCond_FirstUseEver);
  if (!ImGui::Begin(TW("Sample browser").c_str(), &showBrowser_)) {
    ImGui::End();
    return;
  }
  if (browserScanned_ != browseDir_ || browserScanned_.empty()) browserScan();

  // Path and navigation.
  if (ImGui::Button(T("Up"))) {
    fs::path p(browseDir_);
    if (p.has_parent_path() && p.parent_path() != p) browseDir_ = p.parent_path().string();
  }
  ImGui::SameLine();
  if (ImGui::Button(T("Song folder")) && !filePath_.empty()) browseDir_ = fs::path(filePath_).parent_path().string();
  ImGui::SameLine();
  if (ImGui::Button(T("Refresh"))) browserScanned_.clear();
  ImGui::SameLine();
  ImGui::Checkbox(T("Play on click"), &browserAutoplay_);
  ImGui::TextDisabled("%s", browseDir_.c_str());
  ImGui::SetNextItemWidth(-1);
  ImGui::InputTextWithHint("##bfilter", T("Filter..."), browserFilter_, sizeof(browserFilter_));
  std::string filter = lowerCase(browserFilter_);

  float infoH = S(150);
  ImGui::BeginChild("blist", ImVec2(0, std::max(S(80), ImGui::GetContentRegionAvail().y - infoH)), ImGuiChildFlags_Borders);
  std::vector<int> files;  // visible files, for the arrow keys
  std::string enter;
  for (int i = 0; i < (int)browserEntries_.size(); i++) {
    const BrowserEntry& e = browserEntries_[i];
    if (!filter.empty() && lowerCase(e.name).find(filter) == std::string::npos) continue;
    if (e.dir) {
      std::string label = "[" + e.name + "]";
      if (ImGui::Selectable(label.c_str(), false)) enter = e.name;
      continue;
    }
    files.push_back(i);
    bool sel = e.name == browserSel_;
    if (ImGui::Selectable(e.name.c_str(), sel)) browserSelect(e.name, browserAutoplay_);
    if (sel && browserScrollTo_) {
      ImGui::SetScrollHereY();
      browserScrollTo_ = false;
    }
  }
  // Up / down go through the files and play them.
  if (ImGui::IsWindowFocused() && !files.empty()) {
    int cur = -1;
    for (size_t k = 0; k < files.size(); k++)
      if (browserEntries_[files[k]].name == browserSel_) cur = (int)k;
    int next = cur;
    if (ImGui::IsKeyPressed(ImGuiKey_DownArrow)) next = std::min(cur + 1, (int)files.size() - 1);
    if (ImGui::IsKeyPressed(ImGuiKey_UpArrow)) next = std::max(cur - 1, 0);
    if (next != cur && next >= 0) {
      browserSelect(browserEntries_[files[next]].name, browserAutoplay_);
      browserScrollTo_ = true;
    }
  }
  ImGui::EndChild();
  if (!enter.empty()) {
    browseDir_ = (fs::path(browseDir_) / enter).string();
    browserSel_.clear();
  }

  // The chosen sound.
  if (!browserError_.empty()) ImGui::TextColored(ImVec4(1, 0.5f, 0.4f, 1), "%s", browserError_.c_str());
  if (browserSample_) {
    const Sample& s = *browserSample_;
    ImGui::Text("%s  -  %.2f s, %d Hz", s.name.c_str(), s.data.size() / (double)std::max(s.rate, 1), s.rate);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 p0 = ImGui::GetCursorScreenPos();
    float w = ImGui::GetContentRegionAvail().x, h = S(48);
    dl->AddRectFilled(p0, ImVec2(p0.x + w, p0.y + h), IM_COL32(15, 15, 25, 255));
    size_t n = s.data.size();
    for (int x = 0; x < (int)w && n; x++) {
      size_t a = n * x / (size_t)w, b = std::max(a + 1, n * (x + 1) / (size_t)w);
      float lo = 0, hi = 0;
      for (size_t i = a; i < b && i < n; i++) lo = std::min(lo, s.data[i]), hi = std::max(hi, s.data[i]);
      dl->AddLine(ImVec2(p0.x + x, p0.y + h / 2 - hi * h / 2), ImVec2(p0.x + x, p0.y + h / 2 - lo * h / 2 + 1),
                  IM_COL32(120, 220, 180, 255));
    }
    ImGui::Dummy(ImVec2(w, h));
    if (ImGui::Button(engine_->previewing() ? T("Stop") : T("Play"))) {
      if (engine_->previewing()) engine_->previewSample(nullptr);
      else engine_->previewSample(browserSample_);
    }
    ImGui::SameLine();
    bool full = (int)song_.samples.size() >= MAX_SAMPLES;
    ImGui::BeginDisabled(full);
    // Adds the sound to the song, or reuses it if it is already there.
    auto addSample = [&]() {
      pushFullUndo();
      for (int i = 0; i < (int)song_.samples.size(); i++) {
        const Sample& o = song_.samples[i];
        if (o.name == s.name && o.rate == s.rate && o.data == s.data) {
          curSample_ = i;
          return i;
        }
      }
      song_.samples.push_back(s);
      curSample_ = (int)song_.samples.size() - 1;
      dirty_ = true;
      return curSample_;
    };
    if (ImGui::Button(T("Add to samples"))) {
      addSample();
      setStatus(std::string(T("Loaded sample ")) + s.name);
    }
    ImGui::SameLine();
    if (ImGui::Button(T("New instrument")) && (int)song_.instruments.size() < MAX_INSTRUMENTS) {
      int idx = addSample();
      Instrument ins;
      ins.type = INS_SAMPLE;
      ins.name = s.name;
      ins.sample = idx;
      song_.instruments.push_back(ins);
      curIns_ = (int)song_.instruments.size() - 1;
      setStatus(std::string(T("New instrument ")) + s.name);
    }
    // Straight onto the drum kit pad picked in the instrument editor.
    bool kit = curIns_ >= 0 && curIns_ < (int)song_.instruments.size() && song_.instruments[curIns_].kit && kitPad_ >= 0 &&
               kitPad_ < NOTE_COUNT;
    if (kit) {
      ImGui::SameLine();
      std::string label = std::string(T("To pad")) + " " + noteName(kitPad_);
      if (ImGui::Button(label.c_str())) {
        int idx = addSample();
        Instrument& ins = song_.instruments[curIns_];
        if (ins.sampleMap.empty()) ins.sampleMap.assign(NOTE_COUNT, -1);
        ins.sampleMap[kitPad_] = (int16_t)idx;
        setStatus(label);
      }
    }
    ImGui::EndDisabled();
  } else {
    ImGui::TextDisabled("%s", T("Click a sound to hear it; arrows go through the list."));
  }
  ImGui::End();
}
