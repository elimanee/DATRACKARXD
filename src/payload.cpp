#include "payload.h"

#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <vector>

#if defined(_WIN32)
#include <windows.h>
#elif defined(__APPLE__)
#include <mach-o/dyld.h>
#include <climits>
#include <cstdlib>
#else
#include <unistd.h>
#endif

namespace fs = std::filesystem;

namespace {

const char MAGIC[8] = {'D', 'T', 'K', 'P', 'L', 'A', 'Y', '1'};
const size_t TRAILER = 8 + 8;  // payload length, magic

fs::path selfPath() {
#if defined(_WIN32)
  wchar_t buf[32768];
  DWORD n = GetModuleFileNameW(nullptr, buf, 32768);
  if (n == 0 || n >= 32768) return {};
  return fs::path(std::wstring(buf, n));
#elif defined(__APPLE__)
  uint32_t size = 0;
  _NSGetExecutablePath(nullptr, &size);
  std::vector<char> buf(size + 1);
  if (_NSGetExecutablePath(buf.data(), &size) != 0) return {};
  char real[PATH_MAX];
  if (realpath(buf.data(), real)) return fs::path(real);
  return fs::path(buf.data());
#else
  std::error_code ec;
  fs::path p = fs::read_symlink("/proc/self/exe", ec);
  return ec ? fs::path() : p;
#endif
}

// Escapes newlines so every field fits on one "key=value" line.
std::string esc(const std::string& s) {
  std::string o;
  for (char c : s) {
    if (c == '\\') o += "\\\\";
    else if (c == '\n') o += "\\n";
    else if (c != '\r') o += c;
  }
  return o;
}

std::string unesc(const std::string& s) {
  std::string o;
  for (size_t i = 0; i < s.size(); i++) {
    if (s[i] == '\\' && i + 1 < s.size()) {
      o += s[i + 1] == 'n' ? '\n' : s[i + 1];
      i++;
    } else {
      o += s[i];
    }
  }
  return o;
}

uint64_t get64(const char* p) {
  uint64_t v = 0;
  for (int i = 7; i >= 0; i--) v = (v << 8) | (uint8_t)p[i];
  return v;
}

// Size of the executable part (without payload) and the payload text.
bool split(const std::vector<char>& data, size_t& exeSize, std::string& payload) {
  exeSize = data.size();
  payload.clear();
  if (data.size() < TRAILER) return false;
  const char* t = data.data() + data.size() - TRAILER;
  if (std::memcmp(t + 8, MAGIC, 8) != 0) return false;
  uint64_t len = get64(t);
  if (len > data.size() - TRAILER) return false;
  exeSize = data.size() - TRAILER - (size_t)len;
  payload.assign(data.data() + exeSize, (size_t)len);
  return true;
}

bool readAll(const fs::path& p, std::vector<char>& out) {
  std::ifstream f(p, std::ios::binary);
  if (!f) return false;
  out.assign(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
  return true;
}

}  // namespace

bool readOwnPayload(PlayerPayload& out) {
  fs::path self = selfPath();
  if (self.empty()) return false;
  // Only the end of the file is needed to know whether there is a payload.
  std::ifstream f(self, std::ios::binary);
  if (!f) return false;
  f.seekg(0, std::ios::end);
  std::streamoff size = f.tellg();
  if (size < (std::streamoff)TRAILER) return false;
  char t[TRAILER];
  f.seekg(size - (std::streamoff)TRAILER);
  f.read(t, TRAILER);
  if (!f || std::memcmp(t + 8, MAGIC, 8) != 0) return false;
  uint64_t len = get64(t);
  if (len > (uint64_t)size - TRAILER) return false;
  std::string payload((size_t)len, '\0');
  f.seekg(size - (std::streamoff)TRAILER - (std::streamoff)len);
  f.read(payload.data(), (std::streamsize)len);
  if (!f) return false;

  // Header lines "key=value" up to an empty line, then the song.
  std::istringstream ss(payload);
  std::string line;
  while (std::getline(ss, line) && !line.empty()) {
    size_t eq = line.find('=');
    if (eq == std::string::npos) continue;
    std::string k = line.substr(0, eq), v = unesc(line.substr(eq + 1));
    if (k == "logo") out.logo = v;
    else if (k == "scroll") out.scroll = v;
    else if (k == "title") out.title = v;
  }
  out.song.assign(std::istreambuf_iterator<char>(ss), std::istreambuf_iterator<char>());
  return !out.song.empty();
}

bool writePlayer(const std::string& path, const PlayerPayload& payload, std::string& err) {
  fs::path self = selfPath();
  std::vector<char> exe;
  if (self.empty() || !readAll(self, exe)) {
    err = "Cannot read the program file";
    return false;
  }
  size_t exeSize;
  std::string old;
  split(exe, exeSize, old);  // a player exporting again drops its own song

  std::string body = "logo=" + esc(payload.logo) + "\ntitle=" + esc(payload.title) + "\nscroll=" + esc(payload.scroll) +
                     "\n\n" + payload.song;
  fs::path out(path);
  {
    std::ofstream f(out, std::ios::binary | std::ios::trunc);
    if (!f) {
      err = "Cannot write " + path;
      return false;
    }
    f.write(exe.data(), (std::streamsize)exeSize);
    f.write(body.data(), (std::streamsize)body.size());
    uint64_t len = body.size();
    char t[TRAILER];
    for (int i = 0; i < 8; i++) t[i] = (char)((len >> (8 * i)) & 0xff);
    std::memcpy(t + 8, MAGIC, 8);
    f.write(t, TRAILER);
    if (!f) {
      err = "Write error on " + path;
      return false;
    }
  }
  std::error_code ec;
  fs::permissions(out, fs::perms::owner_exec | fs::perms::group_exec | fs::perms::others_exec, fs::perm_options::add, ec);
  return true;
}
