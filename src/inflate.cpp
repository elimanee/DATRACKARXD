// Minimal zlib/deflate decoder (RFC 1950/1951), enough for .fur files, and a
// small encoder (LZ77 with the fixed Huffman codes) for the player payload.
#include <algorithm>
#include <cstring>

#include "import.h"

namespace {

struct Huffman {
  short count[16];
  short symbol[320];
};

class Inflater {
 public:
  Inflater(const uint8_t* in, size_t len, std::vector<uint8_t>& out) : in_(in), len_(len), out_(out) {}

  bool run() {
    int last;
    do {
      last = bits(1);
      int type = bits(2);
      bool ok;
      if (type == 0) ok = stored();
      else if (type == 1) ok = fixed();
      else if (type == 2) ok = dynamic();
      else ok = false;
      if (!ok || err_) return false;
    } while (!last);
    return true;
  }

 private:
  const uint8_t* in_;
  size_t len_, pos_ = 0;
  uint32_t buf_ = 0;
  int cnt_ = 0;
  bool err_ = false;
  std::vector<uint8_t>& out_;

  int bits(int n) {
    uint32_t v = buf_;
    while (cnt_ < n) {
      if (pos_ >= len_) {
        err_ = true;
        return 0;
      }
      v |= (uint32_t)in_[pos_++] << cnt_;
      cnt_ += 8;
    }
    buf_ = v >> n;
    cnt_ -= n;
    return (int)(v & ((1u << n) - 1));
  }

  bool stored() {
    buf_ = 0;
    cnt_ = 0;
    if (pos_ + 4 > len_) return false;
    unsigned n = in_[pos_] | (in_[pos_ + 1] << 8);
    unsigned nc = in_[pos_ + 2] | (in_[pos_ + 3] << 8);
    pos_ += 4;
    if (n != (~nc & 0xffff) || pos_ + n > len_) return false;
    out_.insert(out_.end(), in_ + pos_, in_ + pos_ + n);
    pos_ += n;
    return true;
  }

  int decode(const Huffman& h) {
    int code = 0, first = 0, index = 0;
    for (int len = 1; len < 16; len++) {
      code |= bits(1);
      int count = h.count[len];
      if (code - count < first) return h.symbol[index + (code - first)];
      index += count;
      first += count;
      first <<= 1;
      code <<= 1;
      if (err_) return -1;
    }
    return -1;
  }

  static int build(Huffman& h, const short* length, int n) {
    std::memset(h.count, 0, sizeof(h.count));
    for (int s = 0; s < n; s++) h.count[length[s]]++;
    if (h.count[0] == n) return 0;
    int left = 1;
    for (int len = 1; len < 16; len++) {
      left <<= 1;
      left -= h.count[len];
      if (left < 0) return left;
    }
    short offs[16];
    offs[1] = 0;
    for (int len = 1; len < 15; len++) offs[len + 1] = offs[len] + h.count[len];
    for (int s = 0; s < n; s++)
      if (length[s]) h.symbol[offs[length[s]]++] = (short)s;
    return left;
  }

  bool codes(const Huffman& lencode, const Huffman& distcode) {
    static const short lbase[29] = {3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27, 31, 35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258};
    static const short lext[29] = {0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0};
    static const short dbase[30] = {1, 2, 3, 4, 5, 7, 9, 13, 17, 25, 33, 49, 65, 97, 129, 193, 257, 385, 513, 769, 1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577};
    static const short dext[30] = {0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13};
    for (;;) {
      int symbol = decode(lencode);
      if (symbol < 0 || err_) return false;
      if (symbol < 256) {
        out_.push_back((uint8_t)symbol);
      } else if (symbol == 256) {
        return true;
      } else {
        symbol -= 257;
        if (symbol >= 29) return false;
        int len = lbase[symbol] + bits(lext[symbol]);
        symbol = decode(distcode);
        if (symbol < 0 || symbol >= 30) return false;
        size_t dist = dbase[symbol] + bits(dext[symbol]);
        if (dist > out_.size()) return false;
        size_t from = out_.size() - dist;
        for (int i = 0; i < len; i++) out_.push_back(out_[from + i]);
      }
    }
  }

  bool fixed() {
    static Huffman lencode, distcode;
    static bool ready = false;
    if (!ready) {
      short lengths[288];
      int s = 0;
      for (; s < 144; s++) lengths[s] = 8;
      for (; s < 256; s++) lengths[s] = 9;
      for (; s < 280; s++) lengths[s] = 7;
      for (; s < 288; s++) lengths[s] = 8;
      build(lencode, lengths, 288);
      for (s = 0; s < 30; s++) lengths[s] = 5;
      build(distcode, lengths, 30);
      ready = true;
    }
    return codes(lencode, distcode);
  }

  bool dynamic() {
    static const short order[19] = {16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15};
    short lengths[320];
    int nlen = bits(5) + 257, ndist = bits(5) + 1, ncode = bits(4) + 4;
    if (nlen > 286 || ndist > 30) return false;
    int idx;
    for (idx = 0; idx < ncode; idx++) lengths[order[idx]] = (short)bits(3);
    for (; idx < 19; idx++) lengths[order[idx]] = 0;
    Huffman lencode, distcode;
    if (build(lencode, lengths, 19) != 0) return false;
    idx = 0;
    while (idx < nlen + ndist) {
      int symbol = decode(lencode);
      if (symbol < 0 || err_) return false;
      if (symbol < 16) {
        lengths[idx++] = (short)symbol;
      } else {
        int len = 0;
        if (symbol == 16) {
          if (idx == 0) return false;
          len = lengths[idx - 1];
          symbol = 3 + bits(2);
        } else if (symbol == 17) {
          symbol = 3 + bits(3);
        } else {
          symbol = 11 + bits(7);
        }
        if (idx + symbol > nlen + ndist) return false;
        while (symbol--) lengths[idx++] = (short)len;
      }
    }
    if (lengths[256] == 0) return false;
    int e = build(lencode, lengths, nlen);
    if (e < 0 || (e > 0 && nlen - lencode.count[0] != 1)) return false;
    e = build(distcode, lengths + nlen, ndist);
    if (e < 0 || (e > 0 && ndist - distcode.count[0] != 1)) return false;
    return codes(lencode, distcode);
  }
};

}  // namespace

bool zlibInflate(const uint8_t* src, size_t len, std::vector<uint8_t>& out) {
  if (len < 2 || (src[0] & 15) != 8 || ((src[0] << 8) | src[1]) % 31 != 0) return false;
  out.clear();
  out.reserve(len * 4);
  Inflater inf(src + 2, len - 2, out);
  return inf.run();
}

namespace {

class BitWriter {
 public:
  std::vector<uint8_t>& out;
  uint32_t acc = 0;
  int n = 0;
  explicit BitWriter(std::vector<uint8_t>& o) : out(o) {}
  void bits(uint32_t v, int count) {  // least significant bit first
    acc |= v << n;
    n += count;
    while (n >= 8) {
      out.push_back((uint8_t)acc);
      acc >>= 8;
      n -= 8;
    }
  }
  void code(uint32_t c, int len) {  // Huffman codes go most significant bit first
    uint32_t r = 0;
    for (int i = 0; i < len; i++) r |= ((c >> i) & 1) << (len - 1 - i);
    bits(r, len);
  }
  void flush() {
    if (n > 0) out.push_back((uint8_t)acc);
    acc = 0;
    n = 0;
  }
};

const int LEN_BASE[29] = {3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27, 31, 35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258};
const int LEN_EXTRA[29] = {0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0};
const int DIST_BASE[30] = {1, 2, 3, 4, 5, 7, 9, 13, 17, 25, 33, 49, 65, 97, 129, 193, 257, 385, 513, 769, 1025, 1537, 2049, 3073,
                           4097, 6145, 8193, 12289, 16385, 24577};
const int DIST_EXTRA[30] = {0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13};

void literal(BitWriter& w, int sym) {
  if (sym < 144) w.code(0x30 + sym, 8);
  else if (sym < 256) w.code(0x190 + sym - 144, 9);
  else if (sym < 280) w.code(sym - 256, 7);
  else w.code(0xC0 + sym - 280, 8);
}

}  // namespace

void zlibDeflate(const uint8_t* src, size_t len, std::vector<uint8_t>& out) {
  out.clear();
  out.push_back(0x78);
  out.push_back(0x01);
  BitWriter w(out);
  w.bits(1, 1);  // final block
  w.bits(1, 2);  // fixed Huffman codes
  const int WINDOW = 32768, HASH = 1 << 15, MAX_CHAIN = 64;
  std::vector<int> head(HASH, -1), prev(len, -1);
  auto hash = [&](size_t i) { return ((src[i] << 10) ^ (src[i + 1] << 5) ^ src[i + 2]) & (HASH - 1); };
  size_t i = 0;
  while (i < len) {
    int bestLen = 0, bestDist = 0;
    if (i + 2 < len) {
      int h = hash(i);
      int cand = head[h], chain = 0;
      while (cand >= 0 && (int)(i - cand) <= WINDOW && chain++ < MAX_CHAIN) {
        int l = 0, maxL = (int)std::min<size_t>(258, len - i);
        while (l < maxL && src[cand + l] == src[i + l]) l++;
        if (l > bestLen) {
          bestLen = l;
          bestDist = (int)(i - cand);
          if (l == maxL) break;
        }
        cand = prev[cand];
      }
    }
    size_t step = bestLen >= 3 ? (size_t)bestLen : 1;
    if (bestLen >= 3) {
      int c = 28;
      while (LEN_BASE[c] > bestLen) c--;
      literal(w, 257 + c);
      w.bits(bestLen - LEN_BASE[c], LEN_EXTRA[c]);
      int d = 29;
      while (DIST_BASE[d] > bestDist) d--;
      w.code(d, 5);
      w.bits(bestDist - DIST_BASE[d], DIST_EXTRA[d]);
    } else {
      literal(w, src[i]);
    }
    for (size_t k = 0; k < step; k++, i++) {
      if (i + 2 < len) {
        int h = hash(i);
        prev[i] = head[h];
        head[h] = (int)i;
      }
    }
  }
  literal(w, 256);
  w.flush();
  // Adler-32 of the data.
  uint32_t a = 1, b = 0;
  for (size_t k = 0; k < len; k++) {
    a = (a + src[k]) % 65521;
    b = (b + a) % 65521;
  }
  uint32_t ad = (b << 16) | a;
  for (int k = 3; k >= 0; k--) out.push_back((uint8_t)(ad >> (8 * k)));
}
