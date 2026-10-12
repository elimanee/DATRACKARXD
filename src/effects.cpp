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

// ---------------------------------------------------------------------------
// Channel insert effects
// ---------------------------------------------------------------------------

void Biquad::lowShelf(float freq, float gainDb, float sr) {
  float A = std::pow(10.0f, gainDb / 40.0f), w = 2 * 3.14159265f * freq / sr;
  float cs = std::cos(w), alpha = std::sin(w) / 2 * std::sqrt(2.0f), sq = 2 * std::sqrt(A) * alpha;
  float a0 = (A + 1) + (A - 1) * cs + sq;
  b0 = A * ((A + 1) - (A - 1) * cs + sq) / a0;
  b1 = 2 * A * ((A - 1) - (A + 1) * cs) / a0;
  b2 = A * ((A + 1) - (A - 1) * cs - sq) / a0;
  a1 = -2 * ((A - 1) + (A + 1) * cs) / a0;
  a2 = ((A + 1) + (A - 1) * cs - sq) / a0;
}

void Biquad::highShelf(float freq, float gainDb, float sr) {
  float A = std::pow(10.0f, gainDb / 40.0f), w = 2 * 3.14159265f * freq / sr;
  float cs = std::cos(w), alpha = std::sin(w) / 2 * std::sqrt(2.0f), sq = 2 * std::sqrt(A) * alpha;
  float a0 = (A + 1) - (A - 1) * cs + sq;
  b0 = A * ((A + 1) + (A - 1) * cs + sq) / a0;
  b1 = -2 * A * ((A - 1) + (A + 1) * cs) / a0;
  b2 = A * ((A + 1) + (A - 1) * cs - sq) / a0;
  a1 = 2 * ((A - 1) - (A + 1) * cs) / a0;
  a2 = ((A + 1) - (A - 1) * cs - sq) / a0;
}

void Biquad::peak(float freq, float gainDb, float q, float sr) {
  float A = std::pow(10.0f, gainDb / 40.0f), w = 2 * 3.14159265f * std::min(freq, sr * 0.45f) / sr;
  float cs = std::cos(w), alpha = std::sin(w) / (2 * q);
  float a0 = 1 + alpha / A;
  b0 = (1 + alpha * A) / a0;
  b1 = -2 * cs / a0;
  b2 = (1 - alpha * A) / a0;
  a1 = -2 * cs / a0;
  a2 = (1 - alpha / A) / a0;
}

void ChannelFxUnit::clear() {
  hold_ = 0;
  count_ = 0;
  lp_ = 0;
  std::fill(buf_.begin(), buf_.end(), 0.0f);
  pos_ = 0;
  phase_ = 0;
  low_.clear();
  mid_.clear();
  high_.clear();
  env_ = 0;
}

float ChannelFxUnit::process(float x, const ChannelEffect& e, float sr) {
  if (e.type != type_) {
    clear();
    type_ = e.type;
  }
  switch (e.type) {
    case CFX_BITCRUSH: {
      // Sample-and-hold at a lower rate, then fewer amplitude steps.
      if (count_ <= 0) {
        float levels = std::pow(2.0f, std::clamp(e.p[0], 1.0f, 16.0f) - 1);
        hold_ = std::round(std::clamp(x, -1.0f, 1.0f) * levels) / levels;
        count_ = std::max(1, (int)std::lround(e.p[1]));
      }
      count_--;
      return x + (hold_ - x) * e.p[2];
    }
    case CFX_DISTORTION: {
      float g = 1 + e.p[0] * e.p[0] * 49;
      float y = x * g;
      switch ((int)std::lround(e.p[3])) {
        case 1: y = std::clamp(y, -1.0f, 1.0f); break;
        case 2: y = std::sin(y); break;
        default: y = std::tanh(y); break;
      }
      // Tone: a one-pole low-pass from 500 Hz to 20 kHz.
      float fc = 500.0f * std::pow(2.0f, e.p[1] * 5.32f);
      lp_ += (y - lp_) * (1 - std::exp(-2 * 3.14159265f * std::min(fc, sr * 0.45f) / sr));
      return x + (lp_ - x) * e.p[2];
    }
    case CFX_CHORUS: {
      int len = (int)(sr * 0.05f);
      if ((int)buf_.size() != len) {
        buf_.assign(len, 0.0f);
        pos_ = 0;
      }
      phase_ += e.p[0] / sr;
      if (phase_ >= 1) phase_ -= 1;
      float ms = 7.0f + e.p[1] * (0.5f + 0.5f * (float)std::sin(phase_ * 2 * 3.14159265358979));
      float d = std::clamp(ms * sr / 1000.0f, 1.0f, len - 2.0f);
      float rp = pos_ - d;
      if (rp < 0) rp += len;
      int i0 = (int)rp, i1 = (i0 + 1) % len;
      float fr = rp - i0;
      float wet = buf_[i0] + (buf_[i1] - buf_[i0]) * fr;
      buf_[pos_] = x + wet * e.p[3];
      pos_ = (pos_ + 1) % len;
      return x * (1 - e.p[2]) + wet * e.p[2];
    }
    case CFX_EQ: {
      if (e.p[0] != eqFor_[0]) low_.lowShelf(200, e.p[0], sr);
      if (e.p[1] != eqFor_[1] || e.p[2] != eqFor_[2]) mid_.peak(e.p[2], e.p[1], 0.9f, sr);
      if (e.p[3] != eqFor_[3]) high_.highShelf(4000, e.p[3], sr);
      std::copy(e.p, e.p + 4, eqFor_);
      float y = x;
      if (e.p[0] != 0) y = low_.process(y);
      if (e.p[1] != 0) y = mid_.process(y);
      if (e.p[3] != 0) y = high_.process(y);
      return y;
    }
    case CFX_COMPRESSOR: {
      float a = std::abs(x);
      float att = std::exp(-1000.0f / (std::max(e.p[2], 0.1f) * sr));
      float rel = std::exp(-1000.0f / (std::max(e.p[3], 1.0f) * sr));
      env_ = a > env_ ? a + (env_ - a) * att : a + (env_ - a) * rel;
      float db = 20 * std::log10(env_ + 1e-9f);
      float over = db - e.p[0];
      float slope = 1 - 1 / std::max(e.p[1], 1.0f);
      float gainDb = (over > 0 ? -over * slope : 0) - e.p[0] * slope * 0.5f;  // with half the reduction made up
      return x * std::pow(10.0f, gainDb / 20);
    }
    default:
      return x;
  }
}
