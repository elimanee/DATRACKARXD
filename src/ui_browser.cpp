// The browser panel, on the left: folders (the song's, the demo songs, home,
// pinned places) with songs, instrument presets and sounds. Click a sound
// to hear it, double-click or drag a file into the project.
#include <SDL.h>
#include <imgui.h>
#include <imgui_internal.h>

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <filesystem>

#include "app.h"
#include "audiofile.h"
#include "i18n.h"
#include "icons.h"
#include "import.h"
#include "theme.h"
#include "ui_widgets.h"

namespace fs = std::filesystem;

namespace {

const char* PAYLOAD = "DT_FILE";

std::string lowerCase(std::string s) {
  for (char& c : s) c = (char)std::tolower((unsigned char)c);
  return s;
}

enum Kind { K_OTHER, K_SONG, K_INSTRUMENT, K_SOUND };

Kind kindOf(const std::string& path) {
  if (isAudioFile(path)) return K_SOUND;  // .wav too: it is heard before it is used
  std::string e = lowerCase(fs::path(path).extension().string());
  for (const std::string& x : supportedSongExtensions())
    if (e == x) return K_SONG;
  for (const std::string& x : instrumentFileExtensions())
    if (e == x) return K_INSTRUMENT;
  return K_OTHER;
}

const char* kindIcon(Kind k) {
  switch (k) {
    case K_SONG: return ICON_SONG;
    case K_INSTRUMENT: return ICON_PIANO;
    case K_SOUND: return ICON_WAVE;
    default: return ICON_FILE;
  }
}

std::string homeDir() {
  const char* h = std::getenv("HOME");
  if (!h || !*h) h = std::getenv("USERPROFILE");
  return h ? h : "";
}

// The songs folder shipped next to the program (or in the current folder).
std::string demoDir() {
  std::error_code ec;
  if (char* base = SDL_GetBasePath()) {
    fs::path b(base);
    SDL_free(base);
    for (fs::path p : {b / "songs", b / ".." / "songs", b / ".." / "share" / "datrackarxd" / "songs"})
      if (fs::is_directory(p, ec)) return fs::weakly_canonical(p, ec).string();
  }
  fs::path p = fs::current_path(ec) / "songs";
  return fs::is_directory(p, ec) ? p.string() : "";
}

// Section headers: framed, but quieter than selected items.
void pushSectionColors() {
  ImVec4 f = ImGui::GetStyleColorVec4(ImGuiCol_FrameBg), a = ImGui::GetStyleColorVec4(ImGuiCol_HeaderHovered);
  ImGui::PushStyleColor(ImGuiCol_Header, f);
  ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ImVec4((f.x + a.x) / 2, (f.y + a.y) / 2, (f.z + a.z) / 2, 1));
  ImGui::PushStyleColor(ImGuiCol_HeaderActive, a);
}

}  // namespace

const std::vector<App::BrowserEntry>& App::browserList(const std::string& dir) {
  auto it = browserCache_.find(dir);
  if (it != browserCache_.end()) return it->second;
  std::vector<BrowserEntry> list;
  std::error_code ec;
  for (fs::directory_iterator d(dir, fs::directory_options::skip_permission_denied, ec), end; !ec && d != end; d.increment(ec)) {
    std::string name = d->path().filename().string();
    if (name.empty() || name[0] == '.') continue;
    std::error_code ec2;
    bool isDir = d->is_directory(ec2);
    if (!isDir && kindOf(name) == K_OTHER) continue;
    list.push_back({name, isDir});
  }
  std::sort(list.begin(), list.end(), [](const BrowserEntry& a, const BrowserEntry& b) {
    if (a.dir != b.dir) return a.dir;
    return lowerCase(a.name) < lowerCase(b.name);
  });
  return browserCache_[dir] = std::move(list);
}

// Decodes a sound for the preview and the buttons; plays it if asked.
void App::browserSelect(const std::string& path, bool play) {
  browserSel_ = path;
  browserError_.clear();
  browserSample_.reset();
  if (kindOf(path) != K_SOUND) return;
  auto s = std::make_shared<Sample>();
  std::string err;
  if (!loadAudioSample(path, *s, err)) {
    browserError_ = err;
    return;
  }
  s->name = fs::path(path).stem().string();
  browserSample_ = s;
  if (play) engine_->previewSample(browserSample_);
}

int App::addSampleFile(const std::string& path) {
  std::shared_ptr<Sample> s = path == browserSel_ ? browserSample_ : nullptr;
  if (!s) {
    s = std::make_shared<Sample>();
    std::string err;
    if (!loadAudioSample(path, *s, err)) {
      setStatus(std::string(T("Sample load failed: ")) + err);
      return -1;
    }
    s->name = fs::path(path).stem().string();
  }
  // A sound already in the song is used again rather than copied.
  for (int i = 0; i < (int)song_.samples.size(); i++) {
    const Sample& o = song_.samples[i];
    if (o.name == s->name && o.rate == s->rate && o.data == s->data) {
      pushFullUndo();
      curSample_ = i;
      return i;
    }
  }
  if ((int)song_.samples.size() >= MAX_SAMPLES) {
    setStatus(T("Too many samples"));
    return -1;
  }
  pushFullUndo();
  song_.samples.push_back(*s);
  curSample_ = (int)song_.samples.size() - 1;
  dirty_ = true;
  setStatus(std::string(T("Loaded sample ")) + s->name);
  return curSample_;
}

void App::newInstrumentFromSample(int sample) {
  if ((int)song_.instruments.size() >= MAX_INSTRUMENTS) {
    setStatus(T("Too many instruments"));
    return;
  }
  Instrument ins;
  ins.type = INS_SAMPLE;
  ins.name = song_.samples[sample].name;
  ins.sample = sample;
  song_.instruments.push_back(ins);
  curIns_ = (int)song_.instruments.size() - 1;
  dirty_ = true;
  setStatus(std::string(T("New instrument ")) + ins.name);
}

void App::sampleToPad(int sample, int pad) {
  if (curIns_ < 0 || curIns_ >= (int)song_.instruments.size() || pad < 0 || pad >= NOTE_COUNT) return;
  Instrument& ins = song_.instruments[curIns_];
  if (ins.sampleMap.empty()) ins.sampleMap.assign(NOTE_COUNT, -1);
  ins.sampleMap[pad] = (int16_t)sample;
  dirty_ = true;
  setStatus(std::string(T("To pad")) + " " + noteName(pad) + ": " + song_.samples[sample].name);
}

void App::useFile(const std::string& path, DropTarget target) {
  switch (kindOf(path)) {
    case K_SONG:
      droppedPath_ = path;
      askAction(PendingAction::OpenPath);
      break;
    case K_INSTRUMENT: dropFile(path); break;
    case K_SOUND: {
      int s = addSampleFile(path);
      if (s >= 0 && target != DropTarget::Samples) newInstrumentFromSample(s);
      if (s >= 0 && target == DropTarget::Samples) showSamples_ = true;
      break;
    }
    default: setStatus(std::string(T("Unknown file type: ")) + fs::path(path).filename().string());
  }
}

bool App::acceptFileDrop(std::string& path) {
  ImGuiWindow* w = ImGui::GetCurrentWindow();
  if (!ImGui::BeginDragDropTargetCustom(w->InnerRect, w->ID)) return false;
  bool got = false;
  if (const ImGuiPayload* p = ImGui::AcceptDragDropPayload(PAYLOAD)) {
    path.assign((const char*)p->Data, (size_t)p->DataSize);
    got = true;
  }
  ImGui::EndDragDropTarget();
  return got;
}

void App::browserFile(const std::string& path) {
  Kind k = kindOf(path);
  std::string name = fs::path(path).filename().string();
  if (k == K_SOUND) browserVisible_.push_back(path);
  bool sel = path == browserSel_;
  ImGui::PushID(path.c_str());
  ImGuiTreeNodeFlags fl = ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen | ImGuiTreeNodeFlags_SpanAvailWidth;
  if (sel) fl |= ImGuiTreeNodeFlags_Selected;
  ImGui::TreeNodeEx("f", fl, "%s  %s", kindIcon(k), name.c_str());
  if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal | ImGuiHoveredFlags_NoSharedDelay)) ImGui::SetTooltip("%s", path.c_str());
  if (ImGui::IsItemClicked(ImGuiMouseButton_Left)) browserSelect(path, browserAutoplay_);
  if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
    useFile(path, k == K_SONG ? DropTarget::Song : DropTarget::Instruments);
  if (sel && browserScrollTo_) {
    ImGui::SetScrollHereY();
    browserScrollTo_ = false;
  }
  if (ImGui::BeginDragDropSource()) {
    ImGui::SetDragDropPayload(PAYLOAD, path.data(), path.size());
    ImGui::Text("%s  %s", kindIcon(k), name.c_str());
    ImGui::TextDisabled("%s", k == K_SONG ? T("Drop on the pattern to open") : k == K_SOUND ? T("Drop on Instruments, Samples or a pad")
                                                                                            : T("Drop on Instruments"));
    ImGui::EndDragDropSource();
  }
  if (ImGui::BeginPopupContextItem("ctx")) {
    if (k == K_SONG && ImGui::MenuItem(T("Open"))) useFile(path, DropTarget::Song);
    if (k == K_INSTRUMENT && ImGui::MenuItem(T("Add to the instruments"))) useFile(path, DropTarget::Instruments);
    if (k == K_SOUND) {
      if (ImGui::MenuItem(T("Play"))) browserSelect(path, true);
      if (ImGui::MenuItem(T("Add to samples"))) useFile(path, DropTarget::Samples);
      if (ImGui::MenuItem(T("New instrument"))) useFile(path, DropTarget::Instruments);
      bool kit = curIns_ >= 0 && curIns_ < (int)song_.instruments.size() && song_.instruments[curIns_].kit && kitPad_ >= 0 &&
                 kitPad_ < NOTE_COUNT;
      if (kit && ImGui::MenuItem((std::string(T("To pad")) + " " + noteName(kitPad_)).c_str())) {
        int s = addSampleFile(path);
        if (s >= 0) sampleToPad(s, kitPad_);
      }
    }
    ImGui::Separator();
    if (ImGui::MenuItem(T("Copy the path"))) ImGui::SetClipboardText(path.c_str());
    ImGui::EndPopup();
  }
  ImGui::PopID();
}

void App::browserFolder(const std::string& dir) {
  const std::vector<BrowserEntry>& list = browserList(dir);
  std::string filter = lowerCase(browserFilter_);
  if (list.empty()) ImGui::TextDisabled("%s", T("(nothing here)"));
  for (const BrowserEntry& e : list) {
    std::string path = (fs::path(dir) / e.name).string();
    if (!e.dir) {
      if (!filter.empty() && lowerCase(e.name).find(filter) == std::string::npos) continue;
      browserFile(path);
      continue;
    }
    ImGui::PushID(path.c_str());
    bool open = ImGui::TreeNodeEx("d", ImGuiTreeNodeFlags_SpanAvailWidth, "%s  %s", ICON_FOLDER, e.name.c_str());
    if (ImGui::BeginPopupContextItem("ctx")) {
      bool pinned = std::find(places_.begin(), places_.end(), path) != places_.end();
      if (!pinned && ImGui::MenuItem(T("Add to places"))) places_.push_back(path);
      if (ImGui::MenuItem(T("Copy the path"))) ImGui::SetClipboardText(path.c_str());
      ImGui::EndPopup();
    }
    if (open) {
      browserFolder(path);
      ImGui::TreePop();
    }
    ImGui::PopID();
  }
}

void App::browserSection(const char* icon, const char* label, const std::string& dir, bool open, bool place) {
  if (dir.empty()) return;
  ImGui::PushID(label);
  ImGui::PushID(dir.c_str());
  if (open) ImGui::SetNextItemOpen(true, ImGuiCond_Once);
  pushSectionColors();
  bool nodeOpen = ImGui::TreeNodeEx("s", ImGuiTreeNodeFlags_SpanAvailWidth | ImGuiTreeNodeFlags_Framed, "%s  %s", icon, label);
  ImGui::PopStyleColor(3);
  if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal)) ImGui::SetTooltip("%s", dir.c_str());
  if (ImGui::BeginPopupContextItem("ctx")) {
    if (place && ImGui::MenuItem(T("Remove from places"))) places_.erase(std::remove(places_.begin(), places_.end(), dir), places_.end());
    if (ImGui::MenuItem(T("Copy the path"))) ImGui::SetClipboardText(dir.c_str());
    ImGui::EndPopup();
  }
  if (nodeOpen) {
    browserFolder(dir);
    ImGui::TreePop();
  }
  ImGui::PopID();
  ImGui::PopID();
}

void App::browserWindow() {
  if (!ImGui::Begin(withIcon(ICON_FOLDER, TW("Browser")).c_str(), &showBrowser_)) {
    ImGui::End();
    return;
  }
  // Search and options.
  float btn = ImGui::GetFrameHeight();
  ImGui::SetNextItemWidth(-(btn * 2 + ImGui::GetStyle().ItemSpacing.x * 2));
  std::string hint = std::string(ICON_SEARCH "  ") + T("Filter...");
  ImGui::InputTextWithHint("##bfilter", hint.c_str(), browserFilter_, sizeof(browserFilter_));
  ImGui::SameLine();
  if (iconButton("refresh", ICON_REFRESH, nullptr, false, ImVec2(btn, btn), T("Read the folders again"))) browserCache_.clear();
  ImGui::SameLine();
  if (iconButton("autoplay", ICON_SPEAKER, nullptr, browserAutoplay_, ImVec2(btn, btn), T("Play sounds when clicked")))
    browserAutoplay_ = !browserAutoplay_;

  float previewH = browserSample_ ? S(132) : (browserSel_.empty() && browserError_.empty() ? S(46) : S(62));
  ImGui::BeginChild("tree", ImVec2(0, -previewH), ImGuiChildFlags_None);
  browserVisible_.clear();
  ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(ImGui::GetStyle().ItemSpacing.x, S(2)));
  if (!filePath_.empty()) browserSection(ICON_SONG, T("Song folder"), fs::path(filePath_).parent_path().string(), true, false);
  browserSection(ICON_STAR, T("Demo songs"), demoDir(), filePath_.empty(), false);
  for (const std::string& p : std::vector<std::string>(places_))
    browserSection(ICON_FOLDER, fs::path(p).filename().string().c_str(), p, false, true);
  browserSection(ICON_HOME, T("Home"), homeDir(), false, false);
  if (!recent_.empty()) {
    ImGui::PushID("recent");
    pushSectionColors();
    bool open = ImGui::TreeNodeEx("s", ImGuiTreeNodeFlags_SpanAvailWidth | ImGuiTreeNodeFlags_Framed, "%s  %s", ICON_LIST, T("Recent songs"));
    ImGui::PopStyleColor(3);
    if (open) {
      for (const std::string& r : std::vector<std::string>(recent_)) browserFile(r);
      ImGui::TreePop();
    }
    ImGui::PopID();
  }
  ImGui::PopStyleVar();
  // Up / down go through the sounds and play them.
  if (ImGui::IsWindowFocused() && !ImGui::IsAnyItemActive() && !browserVisible_.empty()) {
    int cur = -1;
    for (size_t i = 0; i < browserVisible_.size(); i++)
      if (browserVisible_[i] == browserSel_) cur = (int)i;
    int next = cur;
    if (ImGui::IsKeyPressed(ImGuiKey_DownArrow)) next = std::min(cur + 1, (int)browserVisible_.size() - 1);
    if (ImGui::IsKeyPressed(ImGuiKey_UpArrow)) next = std::max(cur - 1, 0);
    if (next != cur && next >= 0) {
      browserSelect(browserVisible_[next], browserAutoplay_);
      browserScrollTo_ = true;
    }
  }
  ImGui::EndChild();
  ImGui::Separator();
  browserPreview();
  ImGui::End();
}

// The chosen file: a sound's waveform and buttons, or what to do with it.
void App::browserPreview() {
  if (!browserError_.empty()) ImGui::TextColored(ImVec4(1, 0.5f, 0.4f, 1), "%s", browserError_.c_str());
  if (!browserSample_) {
    if (browserSel_.empty())
      ImGui::TextWrapped("%s", T("Click a sound to hear it, double-click or drag a file into the song."));
    else if (browserError_.empty())
      ImGui::TextWrapped("%s  %s\n%s", kindIcon(kindOf(browserSel_)), fs::path(browserSel_).filename().string().c_str(),
                         kindOf(browserSel_) == K_SONG ? T("Double-click to open it.") : T("Double-click to add it to the instruments."));
    return;
  }
  const Sample& s = *browserSample_;
  ImGui::Text("%s  %s", ICON_WAVE, s.name.c_str());
  ImGui::SameLine();
  ImGui::TextDisabled("%.2f s, %d Hz", s.data.size() / (double)std::max(s.rate, 1), s.rate);
  ImDrawList* dl = ImGui::GetWindowDrawList();
  ImVec2 p0 = ImGui::GetCursorScreenPos();
  float w = ImGui::GetContentRegionAvail().x, h = S(44);
  dl->AddRectFilled(p0, ImVec2(p0.x + w, p0.y + h), ImGui::GetColorU32(ImGuiCol_FrameBg), S(4));
  ImVec4 ac = accentColor();
  ImU32 waveCol = ImGui::ColorConvertFloat4ToU32(ImVec4(ac.x * 0.5f + 0.4f, ac.y * 0.5f + 0.4f, ac.z * 0.5f + 0.4f, 1));
  size_t n = s.data.size();
  for (int x = 0; x < (int)w && n; x++) {
    size_t a = n * x / (size_t)w, b = std::max(a + 1, n * (x + 1) / (size_t)w);
    float lo = 0, hi = 0;
    for (size_t i = a; i < b && i < n; i++) lo = std::min(lo, s.data[i]), hi = std::max(hi, s.data[i]);
    dl->AddLine(ImVec2(p0.x + x, p0.y + h / 2 - hi * h / 2), ImVec2(p0.x + x, p0.y + h / 2 - lo * h / 2 + 1), waveCol);
  }
  ImGui::Dummy(ImVec2(w, h));
  // The sound can be dragged from its waveform too.
  if (ImGui::BeginDragDropSource()) {
    ImGui::SetDragDropPayload(PAYLOAD, browserSel_.data(), browserSel_.size());
    ImGui::Text("%s  %s", ICON_WAVE, s.name.c_str());
    ImGui::EndDragDropSource();
  }
  bool playing = engine_->previewing();
  if (iconButton("pv", playing ? ICON_STOP : ICON_PLAY, nullptr, playing, ImVec2(ImGui::GetFrameHeight() * 1.4f, 0),
                 playing ? T("Stop") : T("Play")))
    engine_->previewSample(playing ? nullptr : browserSample_);
  ImGui::SameLine();
  std::string path = browserSel_;
  if (iconButton("add", ICON_PLUS, T("Samples"), false, ImVec2(0, 0), T("Add to samples"))) useFile(path, DropTarget::Samples);
  ImGui::SameLine();
  if (iconButton("ins", ICON_PIANO, T("Instrument"), false, ImVec2(0, 0), T("New instrument"))) useFile(path, DropTarget::Instruments);
  // Straight onto the drum kit pad picked in the instrument editor.
  bool kit = curIns_ >= 0 && curIns_ < (int)song_.instruments.size() && song_.instruments[curIns_].kit && kitPad_ >= 0 &&
             kitPad_ < NOTE_COUNT;
  if (kit) {
    ImGui::SameLine();
    std::string label = std::string(T("To pad")) + " " + noteName(kitPad_);
    if (iconButton("pad", ICON_DRUM, label.c_str())) {
      int idx = addSampleFile(path);
      if (idx >= 0) sampleToPad(idx, kitPad_);
    }
  }
}
