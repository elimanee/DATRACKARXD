// Audio effects: a resonant state-variable filter (also used per channel by
// SID-style instruments), a Freeverb-style reverb and a stereo delay.
#pragma once
#include <vector>

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
