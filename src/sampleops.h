// Sample editing operations. Ranges are frames [a, b); a range that is
// empty or invalid means the whole sample. Loop points follow the edits.
#pragma once
#include <vector>

#include "song.h"

namespace sampleops {

// Clamps [a, b) to the sample; the whole sample if the range is empty.
void wholeIfEmpty(const Sample& s, int& a, int& b);

std::vector<float> copy(const Sample& s, int a, int b);
void erase(Sample& s, int a, int b);
void crop(Sample& s, int a, int b);
void insert(Sample& s, int at, const std::vector<float>& data);
void silence(Sample& s, int a, int b);
void normalize(Sample& s, int a, int b);  // peak to full scale
void fadeIn(Sample& s, int a, int b);
void fadeOut(Sample& s, int a, int b);
void reverse(Sample& s, int a, int b);
void invert(Sample& s, int a, int b);
void amplify(Sample& s, int a, int b, float gain);
void removeDc(Sample& s, int a, int b);
// Converts the whole sample to a new rate (it keeps its pitch).
void resample(Sample& s, int newRate);

}  // namespace sampleops
