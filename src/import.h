// Loading songs in other formats: MOD, S3M, XM, IT (sample trackers) and
// FUR (Furnace). Everything is converted to the DATRACKARXD song model, so
// the result is an approximation of the original playback.
#pragma once
#include <cstdint>
#include <string>
#include <vector>

#include "song.h"

// Extensions accepted by loadAnySong, ".dtk" first.
std::vector<std::string> supportedSongExtensions();

// Loads a .dtk song or imports a module, picking the format from the file
// contents (falling back to the extension).
bool loadAnySong(const std::string& path, Song& song, std::string& err);

bool importMOD(const std::vector<uint8_t>& data, Song& song, std::string& err);
bool importS3M(const std::vector<uint8_t>& data, Song& song, std::string& err);
bool importXM(const std::vector<uint8_t>& data, Song& song, std::string& err);
bool importIT(const std::vector<uint8_t>& data, Song& song, std::string& err);
bool importFUR(const std::vector<uint8_t>& data, Song& song, std::string& err);

// zlib decompression (used by .fur files). Returns false on corrupt data.
bool zlibInflate(const uint8_t* src, size_t len, std::vector<uint8_t>& out);

// ---------------------------------------------------------------------------
// Helpers shared by the importers.
// ---------------------------------------------------------------------------

// Little-endian reader that never reads out of bounds (returns zeros instead).
class ByteReader {
 public:
  ByteReader(const uint8_t* data, size_t size) : d_(data), n_(size) {}
  explicit ByteReader(const std::vector<uint8_t>& v) : d_(v.data()), n_(v.size()) {}
  size_t pos() const { return p_; }
  size_t size() const { return n_; }
  void seek(size_t p) { p_ = p; }
  void skip(size_t n) { p_ += n; }
  bool eof() const { return p_ >= n_; }
  bool ok(size_t need) const { return p_ + need <= n_; }
  uint8_t u8() { return p_ < n_ ? d_[p_++] : (p_++, 0); }
  int8_t s8() { return (int8_t)u8(); }
  uint16_t u16() {
    uint16_t a = u8();
    return (uint16_t)(a | (u8() << 8));
  }
  uint16_t u16be() {
    uint16_t a = u8();
    return (uint16_t)((a << 8) | u8());
  }
  int16_t s16() { return (int16_t)u16(); }
  uint32_t u32() {
    uint32_t a = u16();
    return a | ((uint32_t)u16() << 16);
  }
  int32_t s32() { return (int32_t)u32(); }
  float f32();
  std::string str(size_t len);  // fixed-length, trailing zeros/spaces trimmed
  std::string cstr();           // zero-terminated
  const uint8_t* ptr() const { return p_ < n_ ? d_ + p_ : nullptr; }
  size_t left() const { return p_ < n_ ? n_ - p_ : 0; }

 private:
  const uint8_t* d_;
  size_t n_;
  size_t p_ = 0;
};

// Effects of the classic trackers, in a format-neutral form.
enum class TFx {
  None, Arp, PortaUp, PortaDown, TonePorta, Vibrato, PortaVolSlide, VibVolSlide, Tremolo,
  Pan, Offset, VolSlide, Jump, SetVol, Break, Speed, Tempo,
  FinePortaUp, FinePortaDown, ExtraFinePortaUp, ExtraFinePortaDown, FineVolUp, FineVolDown,
  Retrig, Cut, Delay, PatternDelay, Loop, KeyOff,
};

struct TrackerCell {
  int note = NOTE_EMPTY;  // already in DATRACKARXD numbering
  int ins = -1;           // 0-based
  int vol = -1;           // 0-64
  struct Fx {
    TFx type = TFx::None;
    int param = 0;
  };
  std::vector<Fx> fx;
};

// Converts tracker patterns to DATRACKARXD patterns, keeping effect memory and
// turning "active on this row only" effects into start/stop effects.
struct TrackerConverter {
  // Pitch slide speed in DATRACKARXD units (1/32 semitone) per tracker unit.
  double slideScale = 1.3;
  bool effectMemory = true;  // S3M/XM/IT recall the last parameter when it is 0
  int channels = 0;

  // patterns[p] = rows of channels*rowCount cells
  struct TPattern {
    int rows = 64;
    std::vector<TrackerCell> cells;  // rows * channels
    TrackerCell& at(int r, int c, int nch) { return cells[r * nch + c]; }
  };
  std::vector<TPattern> patterns;
  std::vector<int> orders;  // pattern indexes in play order

  // Fills song orders and patterns. Song must already have the channels.
  void build(Song& song);
};
