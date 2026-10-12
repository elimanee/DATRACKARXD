#include "keygen_fx.h"

#include <algorithm>
#include <cctype>
#include <cfloat>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <sstream>

std::string KeygenStyle::toText() const {
  char buf[256];
  std::snprintf(buf, sizeof(buf),
                "rainbow=%d\ncolor=%06x\ncopper=%d\nstars=%d\nstarspeed=%g\nscrollspeed=%g\nwave=%g\nscope=%d\n", rainbow,
                (unsigned)(color & 0xffffff), copper, stars, starSpeed, scrollSpeed, wave, scope);
  return buf;
}

bool KeygenStyle::set(const std::string& k, const std::string& v) {
  auto num = [&](float lo, float hi, float def) {
    char* end = nullptr;
    float f = std::strtof(v.c_str(), &end);
    return end != v.c_str() && std::isfinite(f) ? std::clamp(f, lo, hi) : def;
  };
  bool on = v == "1";
  if (k == "rainbow") rainbow = on;
  else if (k == "color") color = (uint32_t)std::strtoul(v.c_str(), nullptr, 16) & 0xffffff;
  else if (k == "copper") copper = on;
  else if (k == "stars") stars = on;
  else if (k == "starspeed") starSpeed = num(0, 5, 1);
  else if (k == "scrollspeed") scrollSpeed = num(0, 5, 1);
  else if (k == "wave") wave = num(0, 3, 1);
  else if (k == "scope") scope = on;
  else return false;
  return true;
}

void KeygenScene::draw(ImDrawList* dl, ImVec2 p0, ImVec2 size, float time, float dt, float level) {
  ImVec2 p1(p0.x + size.x, p0.y + size.y);
  float t = time;
  dt = std::min(dt, 0.1f);
  dl->PushClipRect(p0, p1, true);
  dl->AddRectFilled(p0, p1, IM_COL32(0, 0, 0, 255));

  // Music level drives the bars and the stars.
  float peak = std::clamp(level, 0.0f, 1.0f);

  // Starfield.
  std::vector<Star>& stars = stars_;
  if (stars.empty()) {
    for (int i = 0; i < 220; i++) stars.push_back({(std::rand() % 2000 - 1000) / 1000.0f, (std::rand() % 2000 - 1000) / 1000.0f, (std::rand() % 1000 + 1) / 1000.0f});
  }
  ImVec2 c((p0.x + p1.x) / 2, (p0.y + p1.y) / 2);
  for (Star& s : stars) {
    if (!style.stars) break;
    s.z -= dt * (0.25f + peak * 0.8f) * style.starSpeed;
    if (s.z <= 0.02f) {
      s = {(std::rand() % 2000 - 1000) / 1000.0f, (std::rand() % 2000 - 1000) / 1000.0f, 1.0f};
    }
    float sx = c.x + s.x / s.z * size.x * 0.5f, sy = c.y + s.y / s.z * size.y * 0.5f;
    int b = (int)(255 * (1.0f - s.z));
    float r = 1.0f + (1.0f - s.z) * 1.5f;
    dl->AddRectFilled(ImVec2(sx - r / 2, sy - r / 2), ImVec2(sx + r / 2, sy + r / 2), IM_COL32(b, b, b, 255));
  }

  // Copper bars.
  static const ImU32 barCols[3][2] = {{IM_COL32(255, 0, 200, 0), IM_COL32(255, 0, 200, 200)},
                                      {IM_COL32(0, 255, 240, 0), IM_COL32(0, 255, 240, 200)},
                                      {IM_COL32(120, 255, 60, 0), IM_COL32(120, 255, 60, 200)}};
  for (int i = 0; i < 3 && style.copper; i++) {
    float y = c.y + std::sin(t * 1.3f + i * 2.1f) * size.y * 0.3f;
    float hh = 6 + peak * 14;
    dl->AddRectFilledMultiColor(ImVec2(p0.x, y - hh), ImVec2(p1.x, y), barCols[i][0], barCols[i][0], barCols[i][1], barCols[i][1]);
    dl->AddRectFilledMultiColor(ImVec2(p0.x, y), ImVec2(p1.x, y + hh), barCols[i][1], barCols[i][1], barCols[i][0], barCols[i][0]);
  }

  ImFont* font = ImGui::GetFont();
  const KeygenStyle& st = style;
  auto hue = [&st](float h, int alpha) {
    if (!st.rainbow) return IM_COL32((st.color >> 16) & 255, (st.color >> 8) & 255, st.color & 255, alpha);
    float r, g, b;
    ImGui::ColorConvertHSVtoRGB(h - std::floor(h), 0.9f, 1.0f, r, g, b);
    return IM_COL32((int)(r * 255), (int)(g * 255), (int)(b * 255), alpha);
  };

  // Logo.
  const char* logo = this->logo.c_str();
  float logoSize = std::clamp(size.y * 0.22f, 16.0f, 64.0f);
  float lw = font->CalcTextSizeA(logoSize, FLT_MAX, 0, logo).x;
  float lx = c.x - lw / 2, ly = p0.y + size.y * 0.12f;
  for (const char* ch = logo; *ch; ch++) {
    int i = (int)(ch - logo);
    float oy = std::sin(t * 4 + i * 0.6f) * logoSize * 0.12f;
    dl->AddText(font, logoSize, ImVec2(lx + 3, ly + oy + 3), IM_COL32(0, 0, 0, 200), ch, ch + 1);
    dl->AddText(font, logoSize, ImVec2(lx, ly + oy), hue(t * 0.2f + i * 0.07f, 255), ch, ch + 1);
    lx += font->CalcTextSizeA(logoSize, FLT_MAX, 0, ch, ch + 1).x;
  }

  // Sine scroller.
  std::string text = scroll.empty() ? "   " : scroll;
  for (char& ch : text) ch = (char)std::toupper((unsigned char)ch);
  float fs = std::clamp(size.y * 0.16f, 14.0f, 48.0f);
  float charW = font->CalcTextSizeA(fs, FLT_MAX, 0, "W").x;
  float total = text.size() * charW;
  // The scroll position follows real time so a speed change doesn't jump.
  scrollPos_ += std::max(dt, 0.0f) * 140.0f * style.scrollSpeed;
  if (total > 0) scrollPos_ = std::fmod(scrollPos_, total);
  float offset = scrollPos_;
  float baseY = p0.y + size.y * 0.72f;
  for (size_t i = 0; i < text.size() * 2; i++) {
    float x = p0.x + i * charW - offset;
    if (x < p0.x - charW) continue;
    if (x > p1.x) break;
    const char* ch = &text[i % text.size()];
    float y = baseY + std::sin(t * 3.0f + x * 0.015f) * size.y * 0.1f * style.wave;
    dl->AddText(font, fs, ImVec2(x, y), hue(x * 0.002f + t * 0.3f, 255), ch, ch + 1);
  }
  dl->PopClipRect();
}
