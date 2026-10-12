// Audio effects: a resonant state-variable filter (also used per channel by
// SID-style instruments), a Freeverb-style reverb, a stereo delay, and the
// per-channel insert effects.
#pragma once
#include <vector>

#include "song.h"

// Topology-preserving state-variable filter (Zavalishin). One input sample
// gives low-, band- and high-pass outputs at once.
class SVFilter {
 public:
  // cutoff in Hz, q = resonance (0.707 = flat, higher = sharper peak).
  void set(float cutoff, float q, float sampleRate);
  // type: 0 low-pass, 1 band-pass, 2 high-pass.
  float process(float x, int type);
  void clear() { ic1_ = ic2_ = 0; }

 private:
  float a1_ = 1, a2_ = 0, a3_ = 0, k_ = 2;
  float ic1_ = 0, ic2_ = 0;
  float lastCutoff_ = -1, lastQ_ = -1;
};

class Reverb {
 public:
  void init(int sampleRate);
  void clear();
  // Mono in, stereo out. room and damping are 0..1.
  void process(float in, float room, float damping, float& outL, float& outR);

 private:
  struct Comb {
    std::vector<float> buf;
    int pos = 0;
    float store = 0;
  };
  struct Allpass {
    std::vector<float> buf;
    int pos = 0;
  };
  Comb combL_[8], combR_[8];
  Allpass apL_[4], apR_[4];
};

class StereoDelay {
 public:
  void init(int sampleRate, float maxSeconds);
  void clear();
  // delay in samples (clamped to the buffer), feedback 0..0.95.
  void process(float inL, float inR, int delay, float feedback, bool pingPong, float& outL, float& outR);

 private:
  std::vector<float> l_, r_;
  int pos_ = 0;
};

// Biquad (RBJ cookbook) for the channel EQ.
struct Biquad {
  float b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0;
  float z1 = 0, z2 = 0;
  void lowShelf(float freq, float gainDb, float sampleRate);
  void highShelf(float freq, float gainDb, float sampleRate);
  void peak(float freq, float gainDb, float q, float sampleRate);
  float process(float x) {
    float y = b0 * x + z1;
    z1 = b1 * x - a1 * y + z2;
    z2 = b2 * x - a2 * y;
    return y;
  }
  void clear() { z1 = z2 = 0; }
};

// One slot of a channel's insert chain: keeps the state of whichever effect
// the slot holds (it starts clean when the effect type changes).
class ChannelFxUnit {
 public:
  void clear();
  float process(float x, const ChannelEffect& e, float sampleRate);

 private:
  int type_ = -1;
  // Bitcrusher.
  float hold_ = 0;
  int count_ = 0;
  // Distortion tone filter.
  float lp_ = 0;
  // Chorus.
  std::vector<float> buf_;
  int pos_ = 0;
  double phase_ = 0;
  // EQ, with the settings its coefficients were made for.
  Biquad low_, mid_, high_;
  float eqFor_[4] = {-1e9f, -1e9f, -1e9f, -1e9f};
  // Compressor level follower.
  float env_ = 0;
};
