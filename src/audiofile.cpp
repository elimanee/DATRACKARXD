#include "audiofile.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <vector>

#define DR_FLAC_IMPLEMENTATION
#include "dr_flac.h"
#define DR_MP3_IMPLEMENTATION
#include "dr_mp3.h"
#define OV_EXCLUDE_STATIC_CALLBACKS
#include "vorbis/vorbisfile.h"

namespace {

// Longest sample kept: about 6 minutes at 44.1 kHz.
constexpr size_t MAX_FRAMES = 1 << 24;

std::string extension(const std::string& path) {
  size_t dot = path.find_last_of('.');
  size_t slash = path.find_last_of("/\\");
  if (dot == std::string::npos || (slash != std::string::npos && dot < slash)) return "";
  std::string ext = path.substr(dot + 1);
  for (char& c : ext) c = (char)std::tolower((unsigned char)c);
  return ext;
}

// Mixes interleaved frames down to mono.
void toMono(const float* in, size_t frames, int channels, Sample& out) {
  frames = std::min(frames, MAX_FRAMES);
  out.data.assign(frames, 0.0f);
  for (size_t i = 0; i < frames; i++) {
    float sum = 0;
    for (int c = 0; c < channels; c++) sum += in[i * channels + c];
    out.data[i] = std::clamp(sum / channels, -1.0f, 1.0f);
  }
}

void finish(const std::string& path, int rate, Sample& out) {
  out.rate = rate > 0 ? rate : 44100;
  out.volume = 64;
  out.loopMode = 0;
  out.loopStart = 0;
  out.loopEnd = (int)out.data.size();
  size_t slash = path.find_last_of("/\\");
  out.name = path.substr(slash == std::string::npos ? 0 : slash + 1);
}

bool loadFlac(const std::string& path, Sample& out, std::string& err) {
  unsigned int channels = 0, rate = 0;
  drflac_uint64 frames = 0;
  float* pcm = drflac_open_file_and_read_pcm_frames_f32(path.c_str(), &channels, &rate, &frames, nullptr);
  if (!pcm || channels == 0 || frames == 0) {
    drflac_free(pcm, nullptr);
    err = "Cannot decode FLAC file " + path;
    return false;
  }
  toMono(pcm, (size_t)frames, (int)channels, out);
  drflac_free(pcm, nullptr);
  finish(path, (int)rate, out);
  return true;
}

bool loadMp3(const std::string& path, Sample& out, std::string& err) {
  drmp3_config config{};
  drmp3_uint64 frames = 0;
  float* pcm = drmp3_open_file_and_read_pcm_frames_f32(path.c_str(), &config, &frames, nullptr);
  if (!pcm || config.channels == 0 || frames == 0) {
    drmp3_free(pcm, nullptr);
    err = "Cannot decode MP3 file " + path;
    return false;
  }
  toMono(pcm, (size_t)frames, (int)config.channels, out);
  drmp3_free(pcm, nullptr);
  finish(path, (int)config.sampleRate, out);
  return true;
}

size_t oggRead(void* ptr, size_t size, size_t n, void* f) { return std::fread(ptr, size, n, (FILE*)f); }
int oggSeek(void* f, ogg_int64_t offset, int whence) { return std::fseek((FILE*)f, (long)offset, whence); }
int oggClose(void* f) { return std::fclose((FILE*)f); }
long oggTell(void* f) { return std::ftell((FILE*)f); }

bool loadOgg(const std::string& path, Sample& out, std::string& err) {
  FILE* f = std::fopen(path.c_str(), "rb");
  if (!f) {
    err = "Cannot open " + path;
    return false;
  }
  OggVorbis_File vf;
  const ov_callbacks callbacks = {oggRead, oggSeek, oggClose, oggTell};
  if (ov_open_callbacks(f, &vf, nullptr, 0, callbacks) != 0) {
    std::fclose(f);
    err = "Not an Ogg Vorbis file: " + path;
    return false;
  }
  vorbis_info* info = ov_info(&vf, -1);
  int rate = info ? (int)info->rate : 44100;
  std::vector<float> mono;
  int section = 0;
  for (;;) {
    float** pcm = nullptr;
    long n = ov_read_float(&vf, &pcm, 4096, &section);
    if (n == OV_HOLE) continue;
    if (n <= 0) break;
    int ch = ov_info(&vf, section)->channels;
    for (long i = 0; i < n && mono.size() < MAX_FRAMES; i++) {
      float sum = 0;
      for (int c = 0; c < ch; c++) sum += pcm[c][i];
      mono.push_back(std::clamp(sum / std::max(ch, 1), -1.0f, 1.0f));
    }
    if (mono.size() >= MAX_FRAMES) break;
  }
  ov_clear(&vf);  // closes the file too
  if (mono.empty()) {
    err = "No audio in " + path;
    return false;
  }
  out.data = std::move(mono);
  finish(path, rate, out);
  return true;
}

}  // namespace

bool isAudioFile(const std::string& path) {
  std::string ext = extension(path);
  return ext == "wav" || ext == "flac" || ext == "mp3" || ext == "ogg" || ext == "oga";
}

bool loadAudioSample(const std::string& path, Sample& out, std::string& err) {
  // Go by the file's first bytes, then by its extension.
  unsigned char head[4] = {0, 0, 0, 0};
  if (FILE* f = std::fopen(path.c_str(), "rb")) {
    size_t got = std::fread(head, 1, 4, f);
    (void)got;
    std::fclose(f);
  } else {
    err = "Cannot open " + path;
    return false;
  }
  if (!std::memcmp(head, "RIFF", 4)) return loadWavSample(path, out, err);
  if (!std::memcmp(head, "fLaC", 4)) return loadFlac(path, out, err);
  if (!std::memcmp(head, "OggS", 4)) return loadOgg(path, out, err);
  if (!std::memcmp(head, "ID3", 3) || (head[0] == 0xFF && (head[1] & 0xE0) == 0xE0)) return loadMp3(path, out, err);
  std::string ext = extension(path);
  if (ext == "mp3") return loadMp3(path, out, err);
  err = "Unknown audio format (use WAV, FLAC, MP3 or Ogg Vorbis)";
  return false;
}
