#include "video.h"

#include <SDL.h>
#include <imgui.h>
#include <imgui_internal.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>

#include "engine.h"
#include "player.h"

#define STB_IMAGE_WRITE_IMPLEMENTATION
#define STBI_WRITE_NO_STDIO
#include "stb_image_write.h"

namespace fs = std::filesystem;

namespace {

constexpr int RATE = 44100;

#ifdef _WIN32
FILE* openPipe(const std::string& cmd, const char* mode) { return _popen(cmd.c_str(), mode); }
int closePipe(FILE* f) { return _pclose(f); }
const char* NULL_DEV = "NUL";
#else
FILE* openPipe(const std::string& cmd, const char* mode) { return popen(cmd.c_str(), mode); }
int closePipe(FILE* f) { return pclose(f); }
const char* NULL_DEV = "/dev/null";
#endif

std::string lower(std::string s) {
  for (char& c : s) c = (char)std::tolower((unsigned char)c);
  return s;
}

void put16(std::vector<uint8_t>& b, int v) {
  b.push_back((uint8_t)(v & 255));
  b.push_back((uint8_t)((v >> 8) & 255));
}
void put32(std::vector<uint8_t>& b, uint32_t v) {
  for (int i = 0; i < 4; i++) b.push_back((uint8_t)(v >> (8 * i)));
}
void fourcc(std::vector<uint8_t>& b, const char* s) { b.insert(b.end(), s, s + 4); }

// AVI 1.0 file: MJPEG video and PCM sound, interleaved frame by frame, with
// an idx1 index. The counts in the headers are patched at the end.
class AviWriter {
 public:
  bool open(const std::string& path, int w, int h, int fps) {
    f_.open(path, std::ios::binary | std::ios::trunc);
    if (!f_) return false;
    w_ = w;
    h_ = h;
    fps_ = fps;
    std::vector<uint8_t> b;
    fourcc(b, "RIFF");
    put32(b, 0);  // patched
    fourcc(b, "AVI ");
    fourcc(b, "LIST");
    size_t hdrlSize = b.size();
    put32(b, 0);
    fourcc(b, "hdrl");
    fourcc(b, "avih");
    put32(b, 56);
    put32(b, (uint32_t)(1000000 / fps));
    put32(b, 0);
    put32(b, 0);
    put32(b, 0x10 | 0x100);  // has index, interleaved
    totalFramesAt_ = b.size();
    put32(b, 0);  // total frames, patched
    put32(b, 0);
    put32(b, 2);  // streams
    put32(b, 1 << 20);
    put32(b, (uint32_t)w);
    put32(b, (uint32_t)h);
    for (int i = 0; i < 4; i++) put32(b, 0);
    // Video stream.
    fourcc(b, "LIST");
    put32(b, 4 + 8 + 56 + 8 + 40);
    fourcc(b, "strl");
    fourcc(b, "strh");
    put32(b, 56);
    fourcc(b, "vids");
    fourcc(b, "MJPG");
    put32(b, 0);
    put16(b, 0);
    put16(b, 0);
    put32(b, 0);
    put32(b, 1);
    put32(b, (uint32_t)fps);
    put32(b, 0);
    videoLenAt_ = b.size();
    put32(b, 0);  // length, patched
    put32(b, 1 << 20);
    put32(b, 0xFFFFFFFF);
    put32(b, 0);
    put16(b, 0);
    put16(b, 0);
    put16(b, w);
    put16(b, h);
    fourcc(b, "strf");
    put32(b, 40);
    put32(b, 40);
    put32(b, (uint32_t)w);
    put32(b, (uint32_t)h);
    put16(b, 1);
    put16(b, 24);
    fourcc(b, "MJPG");
    put32(b, (uint32_t)(w * h * 3));
    for (int i = 0; i < 4; i++) put32(b, 0);
    // Audio stream: 16-bit stereo PCM.
    fourcc(b, "LIST");
    put32(b, 4 + 8 + 56 + 8 + 18);
    fourcc(b, "strl");
    fourcc(b, "strh");
    put32(b, 56);
    fourcc(b, "auds");
    put32(b, 0);
    put32(b, 0);
    put16(b, 0);
    put16(b, 0);
    put32(b, 0);
    put32(b, 1);
    put32(b, RATE);
    put32(b, 0);
    audioLenAt_ = b.size();
    put32(b, 0);  // length in samples, patched
    put32(b, RATE * 4);
    put32(b, 0xFFFFFFFF);
    put32(b, 4);
    for (int i = 0; i < 4; i++) put16(b, 0);
    fourcc(b, "strf");
    put32(b, 18);
    put16(b, 1);
    put16(b, 2);
    put32(b, RATE);
    put32(b, RATE * 4);
    put16(b, 4);
    put16(b, 16);
    put16(b, 0);
    uint32_t hdrl = (uint32_t)(b.size() - hdrlSize - 4);
    std::memcpy(&b[hdrlSize], &hdrl, 4);
    fourcc(b, "LIST");
    moviSizeAt_ = b.size();
    put32(b, 0);  // patched
    moviAt_ = b.size();
    fourcc(b, "movi");
    f_.write((const char*)b.data(), (std::streamsize)b.size());
    pos_ = b.size();
    return (bool)f_;
  }

  void chunk(const char* id, const std::vector<uint8_t>& data, bool key) {
    std::vector<uint8_t> b;
    fourcc(b, id);
    put32(b, (uint32_t)data.size());
    b.insert(b.end(), data.begin(), data.end());
    if (data.size() & 1) b.push_back(0);
    index_.push_back({id, key ? 0x10u : 0u, (uint32_t)(pos_ - moviAt_), (uint32_t)data.size()});
    f_.write((const char*)b.data(), (std::streamsize)b.size());
    pos_ += b.size();
  }

  void frame(const std::vector<uint8_t>& jpeg, const std::vector<uint8_t>& pcm) {
    chunk("00dc", jpeg, true);
    if (!pcm.empty()) chunk("01wb", pcm, false);
    frames_++;
    samples_ += (uint32_t)(pcm.size() / 4);
  }

  size_t bytes() const { return pos_; }
  void abort() { f_.close(); }

  bool close() {
    uint32_t moviSize = (uint32_t)(pos_ - moviAt_);
    std::vector<uint8_t> b;
    fourcc(b, "idx1");
    put32(b, (uint32_t)(index_.size() * 16));
    for (const Entry& e : index_) {
      fourcc(b, e.id.c_str());
      put32(b, e.flags);
      put32(b, e.offset);
      put32(b, e.size);
    }
    f_.write((const char*)b.data(), (std::streamsize)b.size());
    pos_ += b.size();
    auto patch = [&](size_t at, uint32_t v) {
      f_.seekp((std::streamoff)at);
      f_.write((const char*)&v, 4);
    };
    patch(4, (uint32_t)(pos_ - 8));
    patch(totalFramesAt_, frames_);
    patch(videoLenAt_, frames_);
    patch(audioLenAt_, samples_);
    patch(moviSizeAt_, moviSize);
    f_.close();
    return !f_.fail();
  }

 private:
  struct Entry {
    std::string id;
    uint32_t flags, offset, size;
  };
  std::ofstream f_;
  int w_ = 0, h_ = 0, fps_ = 30;
  size_t pos_ = 0, totalFramesAt_ = 0, videoLenAt_ = 0, audioLenAt_ = 0, moviSizeAt_ = 0, moviAt_ = 0;
  uint32_t frames_ = 0, samples_ = 0;
  std::vector<Entry> index_;
};

// Off-screen drawing: ImGui draw lists rasterized by SDL's software renderer
// into an RGBA picture.
class Canvas {
 public:
  ~Canvas() {
    if (font_) SDL_DestroyTexture(font_);
    if (ren_) SDL_DestroyRenderer(ren_);
    if (surf_) SDL_FreeSurface(surf_);
  }
  bool init(int w, int h) {
    w_ = w;
    h_ = h;
    surf_ = SDL_CreateRGBSurfaceWithFormat(0, w, h, 32, SDL_PIXELFORMAT_RGBA32);
    if (!surf_) return false;
    ren_ = SDL_CreateSoftwareRenderer(surf_);
    if (!ren_) return false;
    unsigned char* px = nullptr;
    int fw = 0, fh = 0;
    ImGui::GetIO().Fonts->GetTexDataAsRGBA32(&px, &fw, &fh);
    font_ = SDL_CreateTexture(ren_, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STATIC, fw, fh);
    if (!font_) return false;
    SDL_UpdateTexture(font_, nullptr, px, fw * 4);
    SDL_SetTextureBlendMode(font_, SDL_BLENDMODE_BLEND);
    return true;
  }
  void render(const ImDrawList& dl, std::vector<uint8_t>& rgba) {
    SDL_RenderSetClipRect(ren_, nullptr);
    SDL_SetRenderDrawColor(ren_, 0, 0, 0, 255);
    SDL_RenderClear(ren_);
    for (const ImDrawCmd& cmd : dl.CmdBuffer) {
      if (cmd.UserCallback || cmd.ElemCount == 0) continue;
      SDL_Rect r{(int)cmd.ClipRect.x, (int)cmd.ClipRect.y, (int)(cmd.ClipRect.z - cmd.ClipRect.x), (int)(cmd.ClipRect.w - cmd.ClipRect.y)};
      if (r.w <= 0 || r.h <= 0) continue;
      SDL_RenderSetClipRect(ren_, &r);
      const ImDrawVert* v = dl.VtxBuffer.Data + cmd.VtxOffset;
      const ImDrawIdx* idx = dl.IdxBuffer.Data + cmd.IdxOffset;
      SDL_RenderGeometryRaw(ren_, font_, &v->pos.x, sizeof(ImDrawVert), (const SDL_Color*)&v->col, sizeof(ImDrawVert), &v->uv.x,
                            sizeof(ImDrawVert), dl.VtxBuffer.Size - (int)cmd.VtxOffset, idx, (int)cmd.ElemCount, sizeof(ImDrawIdx));
    }
    SDL_RenderFlush(ren_);
    rgba.resize((size_t)w_ * h_ * 4);
    SDL_RenderReadPixels(ren_, nullptr, SDL_PIXELFORMAT_RGBA32, rgba.data(), w_ * 4);
  }

 private:
  int w_ = 0, h_ = 0;
  SDL_Surface* surf_ = nullptr;
  SDL_Renderer* ren_ = nullptr;
  SDL_Texture* font_ = nullptr;
};

ImU32 hueColor(float h, int alpha = 255) {
  float r, g, b;
  ImGui::ColorConvertHSVtoRGB(h - std::floor(h), 0.65f, 1.0f, r, g, b);
  return IM_COL32((int)(r * 255), (int)(g * 255), (int)(b * 255), alpha);
}

// One oscilloscope per channel in a grid, each triggered on a rising zero
// crossing so a steady note stands still, like the chiptune videos.
void drawScopeWall(ImDrawList* dl, ImVec2 size, const Engine& e, const Song& song, const std::string& footer) {
  dl->AddRectFilled(ImVec2(0, 0), size, IM_COL32(8, 8, 14, 255));
  int n = std::max(1, song.channelCount());
  int cols = (int)std::ceil(std::sqrt(n * size.x / size.y / 1.6f));
  cols = std::clamp(cols, 1, n);
  int rows = (n + cols - 1) / cols;
  float fs = std::clamp(size.y / 36.0f, 13.0f, 40.0f);
  float top = fs * 1.6f, bottom = fs * 1.4f;
  float cw = size.x / cols, ch = (size.y - top - bottom) / rows;
  ImFont* font = ImGui::GetFont();
  std::string title = song.name + (song.author.empty() ? "" : "  -  " + song.author);
  dl->AddText(font, fs, ImVec2(fs * 0.6f, fs * 0.3f), IM_COL32(220, 230, 255, 255), title.c_str());
  dl->AddText(font, fs * 0.85f, ImVec2(fs * 0.6f, size.y - bottom + fs * 0.2f), IM_COL32(150, 160, 190, 255), footer.c_str());
  int pos = e.scopePos();
  const int win = SCOPE_LEN / 4;
  for (int c = 0; c < n; c++) {
    float x0 = (c % cols) * cw, y0 = top + (c / cols) * ch;
    ImVec2 a(x0 + 3, y0 + 3), b(x0 + cw - 3, y0 + ch - 3);
    dl->AddRectFilled(a, b, IM_COL32(16, 16, 26, 255), 4);
    dl->AddRect(a, b, IM_COL32(50, 50, 75, 255), 4);
    ImU32 col = hueColor(c / (float)n + 0.5f);
    const auto& sc = e.channel(c).scope;
    auto at = [&](int i) { return sc[((i % SCOPE_LEN) + SCOPE_LEN) % SCOPE_LEN]; };
    // Trigger: the last rising crossing that leaves a full window to draw.
    int start = pos - win * 2;
    for (int i = pos - win; i > pos - win * 3; i--)
      if (at(i - 1) < 0 && at(i) >= 0) {
        start = i;
        break;
      }
    float mid = (a.y + b.y) / 2, amp = (b.y - a.y) * 0.42f;
    ImVec2 prev;
    int pts = std::min(win, (int)(b.x - a.x));
    for (int k = 0; k < pts; k++) {
      float v = std::clamp(at(start + k * win / pts) * 1.6f, -1.0f, 1.0f);
      ImVec2 p(a.x + (b.x - a.x) * k / (pts - 1), mid - v * amp);
      if (k) dl->AddLine(prev, p, col, std::max(1.5f, size.y / 400.0f));
      prev = p;
    }
    const ChannelInfo& info = song.channels[c];
    dl->AddText(font, fs * 0.8f, ImVec2(a.x + 6, a.y + 4), info.muted ? IM_COL32(110, 110, 130, 255) : col, info.name.c_str());
  }
}

std::string clockText(double seconds) {
  int s = (int)seconds;
  char buf[32];
  std::snprintf(buf, sizeof(buf), "%02d:%02d", s / 60, s % 60);
  return buf;
}

}  // namespace

std::string findFfmpeg() {
  static int found = -1;
  if (found < 0) {
    std::string cmd = std::string("ffmpeg -version 2>") + NULL_DEV;
    FILE* p = openPipe(cmd, "r");
    char line[256] = {};
    if (p) {
      if (!std::fgets(line, sizeof(line), p)) line[0] = 0;
      closePipe(p);
    }
    found = std::strstr(line, "ffmpeg version") ? 1 : 0;
  }
  return found ? "ffmpeg" : "";
}

bool videoNeedsFfmpeg(const std::string& path) {
  std::string e = lower(fs::path(path).extension().string());
  return e == ".mp4" || e == ".mkv" || e == ".mov";
}

struct VideoWriter::Impl {
  Song song;
  VideoOptions opt;
  std::string path, wavPath, error;
  std::vector<int16_t> pcm;  // the whole sound, rendered first
  size_t stopAt = 0;         // sample where the song ends (then the tail)
  int totalFrames = 0, frame = 0;
  std::unique_ptr<Engine> engine;
  size_t rendered = 0;
  Canvas canvas;
  KeygenScene scene;
  AviWriter avi;
  FILE* pipe = nullptr;
  bool ownContext = false, failed = false, begun = false, finished = false;
  ImGuiContext* context = nullptr;

  // Renders `n` samples on the video's engine (stopping it at stopAt).
  void advance(size_t n, std::vector<float>& buf) {
    while (n > 0) {
      size_t chunk = n;
      if (rendered < stopAt) chunk = std::min(chunk, stopAt - rendered);
      buf.resize(chunk * 2);
      engine->render(buf.data(), (int)chunk);
      rendered += chunk;
      n -= chunk;
      if (rendered == stopAt) engine->stop();
    }
  }
};

VideoWriter::VideoWriter() : d_(std::make_unique<Impl>()) {}
VideoWriter::~VideoWriter() { cancel(); }

bool VideoWriter::begin(const Song& src, const std::string& path, const VideoOptions& opt, std::string& err) {
  Impl& d = *d_;
  d.song = src.currentSubsongOnly();
  d.opt = opt;
  d.opt.width = std::clamp(opt.width / 2 * 2, 160, 3840);
  d.opt.height = std::clamp(opt.height / 2 * 2, 120, 2160);
  d.opt.fps = std::clamp(opt.fps, 10, 60);
  d.path = path;

  // The sound first: its length gives the number of frames, and ffmpeg
  // needs it as a file.
  {
    Engine e(d.song, RATE);
    e.play(0, 0);
    std::vector<float> buf(1024 * 2);
    const size_t maxSamples = (size_t)RATE * 60 * 20;
    while (e.playing() && e.loops() < std::max(1, opt.loops) && d.pcm.size() / 2 < maxSamples) {
      e.render(buf.data(), 1024);
      for (float v : buf) d.pcm.push_back((int16_t)std::lround(std::clamp(v, -1.0f, 1.0f) * 32767));
    }
    d.stopAt = d.pcm.size() / 2;
    e.stop();
    size_t tail = (size_t)(std::clamp(opt.tail, 0.0f, 30.0f) * RATE);
    for (size_t done = 0; done < tail; done += 1024) {
      e.render(buf.data(), 1024);
      for (float v : buf) d.pcm.push_back((int16_t)std::lround(std::clamp(v, -1.0f, 1.0f) * 32767));
    }
  }
  d.totalFrames = std::max(1, (int)((d.pcm.size() / 2) * d.opt.fps / RATE));

  if (!ImGui::GetCurrentContext()) {
    // Command line: a context of our own for the fonts and draw lists.
    d.context = ImGui::CreateContext();
    d.ownContext = true;
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.Fonts->AddFontDefault();
    io.Fonts->Build();
    io.BackendFlags |= ImGuiBackendFlags_RendererHasVtxOffset;
    io.DisplaySize = ImVec2((float)d.opt.width, (float)d.opt.height);
  }
  if (!d.canvas.init(d.opt.width, d.opt.height)) {
    err = std::string("Cannot draw the video: ") + SDL_GetError();
    return false;
  }

  if (videoNeedsFfmpeg(path)) {
    std::string ff = findFfmpeg();
    if (ff.empty()) {
      err = "ffmpeg was not found: install it, or export an .avi";
      return false;
    }
    if (path.find('"') != std::string::npos) {
      err = "The file name cannot contain a double quote";
      return false;
    }
    d.wavPath = path + ".sound.wav";
    {
      std::vector<uint8_t> h;
      fourcc(h, "RIFF");
      put32(h, (uint32_t)(36 + d.pcm.size() * 2));
      fourcc(h, "WAVE");
      fourcc(h, "fmt ");
      put32(h, 16);
      put16(h, 1);
      put16(h, 2);
      put32(h, RATE);
      put32(h, RATE * 4);
      put16(h, 4);
      put16(h, 16);
      fourcc(h, "data");
      put32(h, (uint32_t)(d.pcm.size() * 2));
      std::ofstream w(d.wavPath, std::ios::binary | std::ios::trunc);
      w.write((const char*)h.data(), (std::streamsize)h.size());
      w.write((const char*)d.pcm.data(), (std::streamsize)(d.pcm.size() * 2));
      if (!w) {
        err = "Cannot write " + d.wavPath;
        return false;
      }
    }
    char cmd[2048];
    std::snprintf(cmd, sizeof(cmd),
                  "%s -y -loglevel error -f rawvideo -pix_fmt rgba -s %dx%d -r %d -i - -i \"%s\" -c:v libx264 -preset veryfast "
                  "-crf 18 -pix_fmt yuv420p -c:a aac -b:a 192k -shortest \"%s\"",
                  ff.c_str(), d.opt.width, d.opt.height, d.opt.fps, d.wavPath.c_str(), path.c_str());
#ifdef _WIN32
    d.pipe = openPipe(cmd, "wb");
#else
    d.pipe = openPipe(cmd, "w");
#endif
    if (!d.pipe) {
      err = "Cannot start ffmpeg";
      return false;
    }
  } else if (!d.avi.open(path, d.opt.width, d.opt.height, d.opt.fps)) {
    err = "Cannot open " + path + " for writing";
    return false;
  }

  d.engine = std::make_unique<Engine>(d.song, RATE);
  d.engine->play(0, 0);
  d.scene.logo = opt.logo.empty() ? std::string("DATRACKARXD") : opt.logo;
  std::string text = opt.scroll.empty() ? d.song.name + (d.song.author.empty() ? "" : "  by " + d.song.author) : opt.scroll;
  d.scene.scroll = "      " + text + "      ***      ";
  d.scene.greets = opt.greets;
  d.scene.style = opt.style;
  d.begun = true;
  return true;
}

bool VideoWriter::step(int frames) {
  Impl& d = *d_;
  if (d.failed || d.frame >= d.totalFrames) return false;
  ImGuiContext* prev = ImGui::GetCurrentContext();
  if (d.ownContext) ImGui::SetCurrentContext(d.context);
  std::vector<float> buf;
  std::vector<uint8_t> rgba, jpeg, pcm;
  for (int k = 0; k < frames && d.frame < d.totalFrames && !d.failed; k++, d.frame++) {
    size_t from = (size_t)d.frame * RATE / d.opt.fps, to = (size_t)(d.frame + 1) * RATE / d.opt.fps;
    d.advance(to - from, buf);
    if (d.ownContext) {
      ImGuiIO& io = ImGui::GetIO();
      io.DeltaTime = 1.0f / d.opt.fps;
      ImGui::NewFrame();
    }
    ImDrawList dl(ImGui::GetDrawListSharedData());
    dl._ResetForNewFrame();
    dl.Flags = ImDrawListFlags_AntiAliasedLines | ImDrawListFlags_AntiAliasedFill | ImDrawListFlags_AllowVtxOffset;
    dl.PushTextureID(ImGui::GetIO().Fonts->TexID);
    ImVec2 size((float)d.opt.width, (float)d.opt.height);
    dl.PushClipRect(ImVec2(0, 0), size);
    double t = (double)d.frame / d.opt.fps;
    std::string info = d.song.name + (d.song.author.empty() ? "" : " - " + d.song.author) + "   " + clockText(t);
    if (d.opt.scene == 1) {
      drawScopeWall(&dl, size, *d.engine, d.song, info);
    } else {
      channelLevels(*d.engine, d.song.channelCount(), d.scene.levels);
      drawPlayerScreen(&dl, ImVec2(0, 0), size, d.scene, d.engine->masterScope().data(), SCOPE_LEN, d.engine->scopePos(), (float)t,
                       1.0f / d.opt.fps, info);
    }
    if (d.ownContext) ImGui::EndFrame();
    d.canvas.render(dl, rgba);
    if (d.pipe) {
      if (std::fwrite(rgba.data(), 1, rgba.size(), d.pipe) != rgba.size()) {
        d.failed = true;
        d.error = "ffmpeg stopped (see the console for its message)";
      }
    } else {
      jpeg.clear();
      stbi_write_jpg_to_func([](void* ctx, void* data, int size) {
        auto* out = (std::vector<uint8_t>*)ctx;
        out->insert(out->end(), (uint8_t*)data, (uint8_t*)data + size);
      }, &jpeg, d.opt.width, d.opt.height, 4, rgba.data(), std::clamp(d.opt.jpegQuality, 10, 100));
      size_t a = std::min(from, d.pcm.size() / 2), b = std::min(to, d.pcm.size() / 2);
      pcm.assign((const uint8_t*)(d.pcm.data() + a * 2), (const uint8_t*)(d.pcm.data() + b * 2));
      d.avi.frame(jpeg, pcm);
      if (d.avi.bytes() > 0x7F000000) {
        d.failed = true;
        d.error = "The AVI file is over 2 GB: use a smaller size, fewer frames per second or ffmpeg (.mp4)";
      }
    }
  }
  if (d.ownContext) ImGui::SetCurrentContext(prev ? prev : d.context);
  return !d.failed && d.frame < d.totalFrames;
}

float VideoWriter::progress() const { return d_->totalFrames ? (float)d_->frame / d_->totalFrames : 0.0f; }
bool VideoWriter::done() const { return d_->failed || d_->frame >= d_->totalFrames; }

bool VideoWriter::finish(std::string& err) {
  Impl& d = *d_;
  d.finished = true;
  bool ok = !d.failed;
  if (d.pipe) {
    int status = closePipe(d.pipe);
    d.pipe = nullptr;
    if (status != 0 && ok) {
      ok = false;
      d.error = "ffmpeg failed (exit status " + std::to_string(status) + ")";
    }
  } else if (!d.avi.close() && ok) {
    ok = false;
    d.error = "Write error on " + d.path;
  }
  std::error_code ec;
  if (!d.wavPath.empty()) fs::remove(d.wavPath, ec);
  if (d.ownContext && d.context) {
    ImGui::DestroyContext(d.context);
    d.context = nullptr;
    d.ownContext = false;
  }
  if (!ok) err = d.error;
  return ok;
}

void VideoWriter::cancel() {
  Impl& d = *d_;
  if (d.finished) return;
  d.finished = true;
  std::error_code ec;
  if (d.pipe) {
    closePipe(d.pipe);
    d.pipe = nullptr;
    fs::remove(d.path, ec);
  } else if (d.begun) {
    d.avi.abort();
    fs::remove(d.path, ec);
  }
  if (!d.wavPath.empty()) fs::remove(d.wavPath, ec);
  if (d.ownContext && d.context) {
    ImGui::DestroyContext(d.context);
    d.context = nullptr;
    d.ownContext = false;
  }
}
