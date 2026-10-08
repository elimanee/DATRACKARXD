#include "effects.h"

#include <algorithm>
#include <cmath>

void SVFilter::set(float cutoff, float q, float sampleRate) {
  if (cutoff == lastCutoff_ && q == lastQ_) return;
  lastCutoff_ = cutoff;
  lastQ_ = q;
  float fc = std::clamp(cutoff, 10.0f, sampleRate * 0.45f);
  float g = std::tan(3.14159265f * fc / sampleRate);
  k_ = 1.0f / std::clamp(q, 0.5f, 20.0f);
  a1_ = 1.0f / (1.0f + g * (g + k_));
  a2_ = g * a1_;
  a3_ = g * a2_;
}

float SVFilter::process(float x, int type) {
  float v3 = x - ic2_;
  float v1 = a1_ * ic1_ + a2_ * v3;
  float v2 = ic2_ + a2_ * ic1_ + a3_ * v3;
  ic1_ = 2 * v1 - ic1_;
  ic2_ = 2 * v2 - ic2_;
  if (type == 1) return v1;
  if (type == 2) return x - k_ * v1 - v2;
  return v2;
}

// Freeverb tunings, given for 44.1 kHz.
static const int COMB_LEN[8] = {1116, 1188, 1277, 1356, 1422, 1491, 1557, 1617};
static const int AP_LEN[4] = {556, 441, 341, 225};
static const int SPREAD = 23;

void Reverb::init(int sampleRate) {
  float k = sampleRate / 44100.0f;
  for (int i = 0; i < 8; i++) {
    combL_[i].buf.assign(std::max(1, (int)(COMB_LEN[i] * k)), 0.0f);
    combR_[i].buf.assign(std::max(1, (int)((COMB_LEN[i] + SPREAD) * k)), 0.0f);
  }
  for (int i = 0; i < 4; i++) {
    apL_[i].buf.assign(std::max(1, (int)(AP_LEN[i] * k)), 0.0f);
    apR_[i].buf.assign(std::max(1, (int)((AP_LEN[i] + SPREAD) * k)), 0.0f);
  }
  clear();
}

void Reverb::clear() {
  for (int i = 0; i < 8; i++) {
    for (Comb* c : {&combL_[i], &combR_[i]}) {
      std::fill(c->buf.begin(), c->buf.end(), 0.0f);
      c->store = 0;
      c->pos = 0;
    }
  }
  for (int i = 0; i < 4; i++) {
    for (Allpass* a : {&apL_[i], &apR_[i]}) {
      std::fill(a->buf.begin(), a->buf.end(), 0.0f);
      a->pos = 0;
    }
  }
}

void Reverb::process(float in, float room, float damping, float& outL, float& outR) {
  if (combL_[0].buf.empty()) {
    outL = outR = 0;
    return;
  }
  float fb = 0.7f + 0.28f * std::clamp(room, 0.0f, 1.0f);
  float damp = 0.4f * std::clamp(damping, 0.0f, 1.0f);
  float x = in * 0.015f;
  auto comb = [&](Comb& c) {
    float y = c.buf[c.pos];
    c.store = y * (1 - damp) + c.store * damp;
    c.buf[c.pos] = x + c.store * fb;
    if (++c.pos >= (int)c.buf.size()) c.pos = 0;
    return y;
  };
  auto allpass = [](Allpass& a, float v) {
    float b = a.buf[a.pos];
    a.buf[a.pos] = v + b * 0.5f;
    if (++a.pos >= (int)a.buf.size()) a.pos = 0;
    return b - v;
  };
  float l = 0, r = 0;
  for (int i = 0; i < 8; i++) {
    l += comb(combL_[i]);
    r += comb(combR_[i]);
  }
  for (int i = 0; i < 4; i++) {
    l = allpass(apL_[i], l);
    r = allpass(apR_[i], r);
  }
  // Freeverb's output gain (3 for its wet scale).
  outL = l * 3.0f;
  outR = r * 3.0f;
}

void StereoDelay::init(int sampleRate, float maxSeconds) {
  size_t n = std::max<size_t>(2, (size_t)(sampleRate * maxSeconds));
  l_.assign(n, 0.0f);
  r_.assign(n, 0.0f);
  pos_ = 0;
}

void StereoDelay::clear() {
  std::fill(l_.begin(), l_.end(), 0.0f);
  std::fill(r_.begin(), r_.end(), 0.0f);
}

void StereoDelay::process(float inL, float inR, int delay, float feedback, bool pingPong, float& outL, float& outR) {
  int n = (int)l_.size();
  if (n < 2) {
    outL = outR = 0;
    return;
  }
  delay = std::clamp(delay, 1, n - 1);
  int rd = pos_ - delay;
  if (rd < 0) rd += n;
  outL = l_[rd];
  outR = r_[rd];
  float fb = std::clamp(feedback, 0.0f, 0.95f);
  if (pingPong) {
    // The echo bounces between the sides.
    l_[pos_] = (inL + inR) * 0.5f + outR * fb;
    r_[pos_] = outL * fb;
  } else {
    l_[pos_] = inL + outL * fb;
    r_[pos_] = inR + outR * fb;
  }
  if (++pos_ >= n) pos_ = 0;
}
