#include "sampleops.h"

#include <algorithm>
#include <cmath>

namespace sampleops {

void wholeIfEmpty(const Sample& s, int& a, int& b) {
  int len = (int)s.data.size();
  if (a > b) std::swap(a, b);
  a = std::clamp(a, 0, len);
  b = std::clamp(b, 0, len);
  if (a >= b) {
    a = 0;
    b = len;
  }
}

// Keeps the loop inside the sample after an edit.
static void fixLoop(Sample& s) {
  int len = (int)s.data.size();
  s.loopStart = std::clamp(s.loopStart, 0, len);
  s.loopEnd = std::clamp(s.loopEnd, s.loopStart, len);
  if (s.loopEnd <= s.loopStart) s.loopMode = 0;
}

std::vector<float> copy(const Sample& s, int a, int b) {
  wholeIfEmpty(s, a, b);
  return std::vector<float>(s.data.begin() + a, s.data.begin() + b);
}

void erase(Sample& s, int a, int b) {
  wholeIfEmpty(s, a, b);
  int n = b - a;
  s.data.erase(s.data.begin() + a, s.data.begin() + b);
  auto shift = [&](int& p) {
    if (p >= b) p -= n;
    else if (p > a) p = a;
  };
  shift(s.loopStart);
  shift(s.loopEnd);
  fixLoop(s);
}

void crop(Sample& s, int a, int b) {
  wholeIfEmpty(s, a, b);
  s.data = std::vector<float>(s.data.begin() + a, s.data.begin() + b);
  s.loopStart -= a;
  s.loopEnd -= a;
  fixLoop(s);
}

void insert(Sample& s, int at, const std::vector<float>& data) {
  at = std::clamp(at, 0, (int)s.data.size());
  int n = (int)data.size();
  s.data.insert(s.data.begin() + at, data.begin(), data.end());
  if (s.loopStart > at) s.loopStart += n;
  if (s.loopEnd >= at) s.loopEnd += n;
  fixLoop(s);
}

void silence(Sample& s, int a, int b) {
  wholeIfEmpty(s, a, b);
  std::fill(s.data.begin() + a, s.data.begin() + b, 0.0f);
}

void normalize(Sample& s, int a, int b) {
  wholeIfEmpty(s, a, b);
  float peak = 0;
  for (int i = a; i < b; i++) peak = std::max(peak, std::abs(s.data[i]));
  if (peak > 1e-6f) amplify(s, a, b, 1.0f / peak);
}

void fadeIn(Sample& s, int a, int b) {
  wholeIfEmpty(s, a, b);
  int n = b - a;
  for (int i = 0; i < n; i++) s.data[a + i] *= (float)i / n;
}

void fadeOut(Sample& s, int a, int b) {
  wholeIfEmpty(s, a, b);
  int n = b - a;
  for (int i = 0; i < n; i++) s.data[a + i] *= (float)(n - 1 - i) / n;
}

void reverse(Sample& s, int a, int b) {
  wholeIfEmpty(s, a, b);
  std::reverse(s.data.begin() + a, s.data.begin() + b);
}

void invert(Sample& s, int a, int b) {
  wholeIfEmpty(s, a, b);
  for (int i = a; i < b; i++) s.data[i] = -s.data[i];
}

void amplify(Sample& s, int a, int b, float gain) {
  wholeIfEmpty(s, a, b);
  for (int i = a; i < b; i++) s.data[i] = std::clamp(s.data[i] * gain, -1.0f, 1.0f);
}

void removeDc(Sample& s, int a, int b) {
  wholeIfEmpty(s, a, b);
  if (b <= a) return;
  double sum = 0;
  for (int i = a; i < b; i++) sum += s.data[i];
  float dc = (float)(sum / (b - a));
  for (int i = a; i < b; i++) s.data[i] = std::clamp(s.data[i] - dc, -1.0f, 1.0f);
}

void resample(Sample& s, int newRate) {
  if (newRate <= 0 || s.rate <= 0 || newRate == s.rate || s.data.empty()) return;
  double ratio = (double)s.rate / newRate;  // source frames per new frame
  size_t n = std::max<size_t>(1, (size_t)std::llround(s.data.size() / ratio));
  // Going down, a box filter over each output frame keeps the aliasing low.
  std::vector<float> out(n);
  for (size_t i = 0; i < n; i++) {
    double pos = i * ratio;
    if (ratio > 1) {
      size_t lo = (size_t)pos, hi = std::min(s.data.size(), (size_t)(pos + ratio) + 1);
      double sum = 0;
      for (size_t k = lo; k < hi; k++) sum += s.data[k];
      out[i] = (float)(sum / std::max<size_t>(1, hi - lo));
    } else {
      size_t k = std::min((size_t)pos, s.data.size() - 1), k1 = std::min(k + 1, s.data.size() - 1);
      float fr = (float)(pos - k);
      out[i] = s.data[k] + (s.data[k1] - s.data[k]) * fr;
    }
  }
  s.loopStart = (int)std::lround(s.loopStart / ratio);
  s.loopEnd = (int)std::lround(s.loopEnd / ratio);
  s.data = std::move(out);
  s.rate = newRate;
  fixLoop(s);
}

}  // namespace sampleops
