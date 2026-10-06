// Format detection and the effect translation shared by the tracker importers.
#include "import.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <fstream>

float ByteReader::f32() {
  uint32_t v = u32();
  float f;
  std::memcpy(&f, &v, 4);
  return f;
}

std::string ByteReader::str(size_t len) {
  std::string s;
  for (size_t i = 0; i < len; i++) {
    char c = (char)u8();
    s += c;
  }
  size_t z = s.find('\0');
  if (z != std::string::npos) s.resize(z);
  while (!s.empty() && (s.back() == ' ' || (unsigned char)s.back() < 32)) s.pop_back();
  for (char& c : s)
    if ((unsigned char)c < 32) c = ' ';
  return s;
}

std::string ByteReader::cstr() {
  std::string s;
  while (!eof()) {
    char c = (char)u8();
    if (!c) break;
    s += c;
  }
  return s;
}

std::vector<std::string> supportedSongExtensions() { return {".dtk", ".fur", ".mod", ".xm", ".it", ".s3m"}; }

bool loadAnySong(const std::string& path, Song& song, std::string& err) {
  std::ifstream f(path, std::ios::binary);
  if (!f) {
    err = "Cannot open " + path;
    return false;
  }
  std::vector<uint8_t> d((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
  auto has = [&](size_t off, const char* magic) {
    size_t n = std::strlen(magic);
    return d.size() >= off + n && std::memcmp(d.data() + off, magic, n) == 0;
  };
  std::string ext;
  size_t dot = path.find_last_of('.');
  if (dot != std::string::npos) ext = path.substr(dot);
  std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
  size_t slash = path.find_last_of("/\\");
  std::string base = path.substr(slash == std::string::npos ? 0 : slash + 1);
  std::transform(base.begin(), base.end(), base.begin(), ::tolower);

  Song s;
  bool ok;
  if (has(0, "DATRACKARXD")) {
    ok = s.load(path, err);
  } else if (has(0, "-Furnace module-") || (d.size() > 2 && d[0] == 0x78 && ((d[0] << 8) | d[1]) % 31 == 0)) {
    ok = importFUR(d, s, err);
  } else if (has(0, "IMPM")) {
    ok = importIT(d, s, err);
  } else if (has(0, "Extended Module:")) {
    ok = importXM(d, s, err);
  } else if (has(44, "SCRM")) {
    ok = importS3M(d, s, err);
  } else if (ext == ".mod" || base.rfind("mod.", 0) == 0 || d.size() >= 1084) {
    ok = importMOD(d, s, err);
  } else {
    err = "Unknown file format";
    ok = false;
  }
  if (!ok) return false;
  if (s.channels.empty()) s.setChannelCount(1);
  if (s.orders.empty()) s.orders.push_back({});
  if (s.instruments.empty()) s.instruments.emplace_back();
  if (s.speeds.empty()) s.speeds = {6};
  // Imports can carry patterns no order uses; a .dtk keeps them on purpose.
  if (!has(0, "DATRACKARXD")) s.cleanup();
  song = std::move(s);
  return true;
}

// ---------------------------------------------------------------------------
// TrackerConverter
// ---------------------------------------------------------------------------

namespace {

enum : int { C_ARP = 1, C_SLIDE = 2, C_PORTA = 4, C_VIB = 8, C_VOLSLIDE = 16, C_TREM = 32, C_RETRIG = 64 };

struct ChanState {
  int mem[32] = {};
  int vibX = 0, vibY = 0, tremX = 0, tremY = 0;
  int active = 0;
};

int memo(ChanState& st, TFx t, int p, bool enabled) {
  int& m = st.mem[(int)t];
  if (p == 0 && enabled) return m;
  m = p;
  return p;
}

}  // namespace

void TrackerConverter::build(Song& song) {
  int nch = std::min(channels, MAX_CHANNELS);
  song.setChannelCount(std::max(nch, 1));
  int maxRows = 1;
  std::vector<int> ords;
  for (int o : orders)
    if (o >= 0 && o < (int)patterns.size() && o < MAX_PATTERNS && (int)ords.size() < MAX_ORDERS) ords.push_back(o);
  if (ords.empty()) ords.push_back(0);
  for (int o : ords)
    if (o < (int)patterns.size()) maxRows = std::max(maxRows, patterns[o].rows);
  song.patternLength = std::min(maxRows, MAX_ROWS);

  song.orders.clear();
  for (int o : ords) {
    std::array<uint8_t, MAX_CHANNELS> row{};
    for (int c = 0; c < nch; c++) row[c] = (uint8_t)o;
    song.orders.push_back(row);
  }
  if (patterns.empty()) return;

  // Effects possibly active when each pattern starts (from the previous one's last row).
  auto lastRowActive = [&](int p, int c) {
    int mask = 0;
    TPattern& pat = patterns[p];
    if (pat.rows <= 0) return 0;
    for (auto& fx : pat.at(pat.rows - 1, c, channels).fx) {
      switch (fx.type) {
        case TFx::Arp: if (fx.param) mask |= C_ARP; break;
        case TFx::PortaUp: case TFx::PortaDown: mask |= C_SLIDE; break;
        case TFx::TonePorta: mask |= C_PORTA; break;
        case TFx::Vibrato: mask |= C_VIB; break;
        case TFx::PortaVolSlide: mask |= C_PORTA | C_VOLSLIDE; break;
        case TFx::VibVolSlide: mask |= C_VIB | C_VOLSLIDE; break;
        case TFx::VolSlide: mask |= C_VOLSLIDE; break;
        case TFx::Tremolo: mask |= C_TREM; break;
        case TFx::Retrig: mask |= C_RETRIG; break;
        default: break;
      }
    }
    return mask;
  };
  std::vector<std::vector<int>> entry(patterns.size(), std::vector<int>(nch, 0));
  for (size_t i = 0; i < ords.size(); i++) {
    int prev = ords[(i + ords.size() - 1) % ords.size()];
    for (int c = 0; c < nch; c++) entry[ords[i]][c] |= lastRowActive(prev, c);
  }

  std::vector<ChanState> state(nch);
  std::vector<bool> done(patterns.size(), false);
  std::vector<int> fxCols(nch, 1);

  for (int p : ords) {
    if (done[p]) continue;
    done[p] = true;
    TPattern& pat = patterns[p];
    int rows = std::min(pat.rows, MAX_ROWS);
    bool rowHasJump = false;
    for (int c = 0; c < nch; c++) {
      ChanState& st = state[c];
      st.active = entry[p][c];
      Pattern* out = song.pattern(c, p, true);
      for (int r = 0; r < rows; r++) {
        TrackerCell& tc = pat.at(r, c, channels);
        Cell& cell = out->rows[r];
        cell.note = (int16_t)tc.note;
        cell.ins = (int16_t)(tc.ins >= 0 && tc.ins < MAX_INSTRUMENTS ? tc.ins : -1);
        if (tc.vol >= 0) cell.vol = (int16_t)std::min(0x7f, std::min(tc.vol, 64) * 127 / 64);

        std::vector<Effect> fx;
        int active = 0;
        auto add = [&](int cmd, int val) { fx.push_back({(int16_t)cmd, (int16_t)std::clamp(val, 0, 255)}); };
        auto slide = [&](int v) { return std::clamp((int)std::lround(v * slideScale), v ? 1 : 0, 255); };
        for (auto& e : tc.fx) {
          int prm = e.param;
          switch (e.type) {
            case TFx::Arp:
              if (prm) {
                add(0x00, prm);
                active |= C_ARP;
              }
              break;
            case TFx::PortaUp:
            case TFx::PortaDown:
              prm = memo(st, e.type, prm, effectMemory);
              if (prm) {
                add(e.type == TFx::PortaUp ? 0x01 : 0x02, slide(prm));
                active |= C_SLIDE;
              }
              break;
            case TFx::TonePorta:
              prm = memo(st, e.type, prm, true);
              if (prm) add(0x03, slide(prm));
              active |= C_PORTA;
              break;
            case TFx::Vibrato:
              if (prm >> 4) st.vibX = prm >> 4;
              if (prm & 15) st.vibY = prm & 15;
              add(0x04, (st.vibX << 4) | st.vibY);
              active |= C_VIB;
              break;
            case TFx::PortaVolSlide:
            case TFx::VibVolSlide:
            case TFx::VolSlide:
              prm = memo(st, TFx::VolSlide, prm, effectMemory);
              if (e.type == TFx::PortaVolSlide) {
                add(0x06, prm);
                active |= C_PORTA | C_VOLSLIDE;
              } else if (e.type == TFx::VibVolSlide) {
                add(0x05, prm);
                active |= C_VIB | C_VOLSLIDE;
              } else if (prm) {
                add(0x0A, prm);
                active |= C_VOLSLIDE;
              }
              break;
            case TFx::Tremolo:
              if (prm >> 4) st.tremX = prm >> 4;
              if (prm & 15) st.tremY = prm & 15;
              add(0x07, (st.tremX << 4) | st.tremY);
              active |= C_TREM;
              break;
            case TFx::Pan: add(0x80, prm); break;
            case TFx::Offset:
              prm = memo(st, e.type, prm, true);
              if (prm) add(0x91, prm);
              break;
            case TFx::Jump: add(0x0B, prm); rowHasJump = true; break;
            case TFx::Break: add(0x0D, prm); rowHasJump = true; break;
            case TFx::SetVol: cell.vol = (int16_t)(std::min(prm, 64) * 127 / 64); break;
            case TFx::Speed:
              if (prm > 0) add(0x0F, prm);
              break;
            case TFx::Tempo:
              if (prm >= 32) add(0xF0, prm);
              break;
            case TFx::FinePortaUp:
            case TFx::FinePortaDown:
              prm = memo(st, e.type, prm, effectMemory);
              if (prm) add(e.type == TFx::FinePortaUp ? 0xF1 : 0xF2, slide(prm));
              break;
            case TFx::ExtraFinePortaUp:
            case TFx::ExtraFinePortaDown:
              prm = memo(st, e.type, prm, effectMemory);
              if (prm)
                add(e.type == TFx::ExtraFinePortaUp ? 0xF1 : 0xF2, std::max(1, (int)std::lround(prm * slideScale / 4)));
              break;
            case TFx::FineVolUp:
            case TFx::FineVolDown:
              prm = memo(st, e.type, prm, effectMemory);
              if (prm) add(e.type == TFx::FineVolUp ? 0xF3 : 0xF4, prm * 2);
              break;
            case TFx::Retrig:
              if (prm) {
                add(0x0C, prm);
                active |= C_RETRIG;
              }
              break;
            case TFx::Cut: add(0xEC, prm); break;
            case TFx::Delay:
              if (prm) add(0xED, prm);
              break;
            case TFx::PatternDelay:
              if (prm) add(0xEE, prm);
              break;
            case TFx::Loop: add(0xE6, prm); break;
            case TFx::KeyOff:
              if (prm == 0) {
                if (cell.note == NOTE_EMPTY) cell.note = NOTE_OFF;
              } else {
                add(0xEC, prm);
              }
              break;
            default: break;
          }
        }
        // Stop effects that were running on the previous row only.
        int stopped = st.active & ~active;
        if (stopped & C_ARP) add(0x00, 0);
        if (stopped & C_SLIDE) add(0x01, 0);
        if ((stopped & C_PORTA) && cell.note < 0) add(0x03, 0);
        if (stopped & C_VIB) add(0x04, 0);
        if (stopped & C_VOLSLIDE) add(0x0A, 0);
        if (stopped & C_TREM) add(0x07, 0);
        if (stopped & C_RETRIG) add(0x0C, 0);
        st.active = active;

        int n = std::min((int)fx.size(), MAX_EFFECTS);
        for (int k = 0; k < n; k++) cell.fx[k] = fx[k];
        fxCols[c] = std::max(fxCols[c], n);
      }
    }
    // Shorter patterns end early with a pattern break.
    if (rows < song.patternLength && rows > 0) {
      bool jump = false;
      for (int c = 0; c < nch; c++) {
        Pattern* out = song.pattern(c, p, true);
        for (auto& e : out->rows[rows - 1].fx) jump |= e.cmd == 0x0B || e.cmd == 0x0D;
      }
      for (int c = 0; c < nch && !jump; c++) {
        Pattern* out = song.pattern(c, p, true);
        for (int k = 0; k < MAX_EFFECTS; k++) {
          Effect& e = out->rows[rows - 1].fx[k];
          if (e.cmd < 0 && e.val < 0) {
            e = {0x0D, 0};
            fxCols[c] = std::max(fxCols[c], k + 1);
            jump = true;
            break;
          }
        }
      }
    }
    (void)rowHasJump;
  }
  for (int c = 0; c < nch; c++) song.channels[c].effectCols = std::clamp(fxCols[c], 1, MAX_EFFECTS);
}
