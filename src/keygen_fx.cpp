#include "keygen_fx.h"

#include <algorithm>
#include <cctype>
#include <cfloat>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <sstream>

std::string KeygenStyle::toText() const {
  char buf[512];
  std::snprintf(buf, sizeof(buf),
                "rainbow=%d\ncolor=%06x\ncopper=%d\nstars=%d\nstarspeed=%g\nscrollspeed=%g\nwave=%g\nscope=%d\n"
                "background=%d\ncycle=%d\nscenetime=%g\nobject=%d\nlogofx=%d\nscrollfx=%d\nvu=%d\nflash=%d\n",
                rainbow, (unsigned)(color & 0xffffff), copper, stars, starSpeed, scrollSpeed, wave, scope, background, cycle,
                sceneTime, object, logoFx, scrollFx, vu, flash);
  return buf;
}

bool KeygenStyle::set(const std::string& k, const std::string& v) {
  auto num = [&](float lo, float hi, float def) {
    char* end = nullptr;
    float f = std::strtof(v.c_str(), &end);
    return end != v.c_str() && std::isfinite(f) ? std::clamp(f, lo, hi) : def;
  };
  auto choice = [&](int hi) { return std::clamp(std::atoi(v.c_str()), 0, hi); };
  bool on = v == "1";
  if (k == "rainbow") rainbow = on;
  else if (k == "color") color = (uint32_t)std::strtoul(v.c_str(), nullptr, 16) & 0xffffff;
  else if (k == "copper") copper = on;
  else if (k == "stars") stars = on;
  else if (k == "starspeed") starSpeed = num(0, 5, 1);
  else if (k == "scrollspeed") scrollSpeed = num(0, 5, 1);
  else if (k == "wave") wave = num(0, 3, 1);
  else if (k == "scope") scope = on;
  else if (k == "background") background = choice(4);
  else if (k == "cycle") cycle = on;
  else if (k == "scenetime") sceneTime = num(2, 120, 12);
  else if (k == "object") object = choice(3);
  else if (k == "logofx") logoFx = choice(2);
  else if (k == "scrollfx") scrollFx = choice(2);
  else if (k == "vu") vu = on;
  else if (k == "flash") flash = on;
  else return false;
  return true;
}

namespace {

ImU32 hsv(float h, float s, float v, int alpha = 255) {
  float r, g, b;
  ImGui::ColorConvertHSVtoRGB(h - std::floor(h), std::clamp(s, 0.0f, 1.0f), std::clamp(v, 0.0f, 1.0f), r, g, b);
  return IM_COL32((int)(r * 255), (int)(g * 255), (int)(b * 255), alpha);
}

// A tiny hash for the glitch effect (no rand(): the scene must not depend on it).
float hash(float x) {
  float s = std::sin(x * 12.9898f) * 43758.5453f;
  return s - std::floor(s);
}

}  // namespace

// The background: starfield, plasma, tunnel, rotozoom or black.
void KeygenScene::background(ImDrawList* dl, ImVec2 p0, ImVec2 p1, int kind, float t, float dt, float peak) {
  ImVec2 size(p1.x - p0.x, p1.y - p0.y);
  ImVec2 c((p0.x + p1.x) / 2, (p0.y + p1.y) / 2);
  float hueShift = style.rainbow ? t * 0.05f : 0.55f;
  switch (kind) {
    case 0: {  // starfield
      if (stars_.empty())
        for (int i = 0; i < 220; i++) stars_.push_back({hash(i * 1.1f) * 2 - 1, hash(i * 2.3f + 7) * 2 - 1, hash(i * 3.7f + 3) + 0.01f});
      for (size_t i = 0; i < stars_.size(); i++) {
        Star& s = stars_[i];
        s.z -= dt * (0.25f + peak * 0.8f) * style.starSpeed;
        if (s.z <= 0.02f) {
          float k = t * 13.7f + i;
          s = {hash(k) * 2 - 1, hash(k + 5.1f) * 2 - 1, 1.0f};
        }
        float sx = c.x + s.x / s.z * size.x * 0.5f, sy = c.y + s.y / s.z * size.y * 0.5f;
        int b = (int)(255 * (1.0f - s.z));
        float r = 1.0f + (1.0f - s.z) * 1.5f;
        dl->AddRectFilled(ImVec2(sx - r / 2, sy - r / 2), ImVec2(sx + r / 2, sy + r / 2), IM_COL32(b, b, b, 255));
      }
      break;
    }
    case 1: {  // plasma: sums of sines, coloured by a cycling palette
      const float cell = std::max(8.0f, size.x / 96);
      for (float y = p0.y; y < p1.y; y += cell)
        for (float x = p0.x; x < p1.x; x += cell) {
          float u = (x - p0.x) / size.x * 10, v = (y - p0.y) / size.y * 10;
          float val = std::sin(u + t) + std::sin(v * 1.3f - t * 1.1f) + std::sin((u + v) * 0.7f + t * 0.6f) +
                      std::sin(std::sqrt((u - 5) * (u - 5) + (v - 5) * (v - 5)) * 1.5f - t * 1.7f);
          dl->AddRectFilled(ImVec2(x, y), ImVec2(x + cell + 1, y + cell + 1),
                            hsv(val * 0.12f + hueShift, 0.85f, 0.35f + 0.15f * std::sin(val * 3) + peak * 0.3f));
        }
      break;
    }
    case 2: {  // tunnel: rings flying at the viewer, checkered
      const int rings = 36, segs = 20;
      float speed = 2.5f + peak * 3;
      float travel = t * speed;
      float maxR = std::max(size.x, size.y) * 0.9f;
      auto ringCenter = [&](float z) {
        return ImVec2(c.x + std::sin(t * 0.7f + z * 0.25f) * size.x * 0.12f * z, c.y + std::cos(t * 0.5f + z * 0.2f) * size.y * 0.1f * z);
      };
      for (int i = rings; i >= 1; i--) {
        float z0 = (i - std::fmod(travel, 1.0f)) / rings * 6 + 0.15f;
        float z1 = z0 + 6.0f / rings;
        float r0 = maxR * 0.25f / z0, r1 = maxR * 0.25f / z1;
        ImVec2 c0 = ringCenter(z0), c1 = ringCenter(z1);
        int ring = i + (int)std::floor(travel);
        float fog = std::clamp(1.2f - z0 / 6.0f, 0.0f, 1.0f);
        for (int k = 0; k < segs; k++) {
          float a0 = (k + t * 0.3f) * 6.2831853f / segs, a1 = (k + 1 + t * 0.3f) * 6.2831853f / segs;
          bool odd = ((ring + k) & 1) != 0;
          ImU32 col = hsv(hueShift + ring * 0.03f, 0.8f, (odd ? 0.55f : 0.2f) * fog + peak * 0.2f * fog);
          dl->AddQuadFilled(ImVec2(c0.x + std::cos(a0) * r0, c0.y + std::sin(a0) * r0), ImVec2(c0.x + std::cos(a1) * r0, c0.y + std::sin(a1) * r0),
                            ImVec2(c1.x + std::cos(a1) * r1, c1.y + std::sin(a1) * r1), ImVec2(c1.x + std::cos(a0) * r1, c1.y + std::sin(a0) * r1),
                            col);
        }
      }
      break;
    }
    case 3: {  // rotozoom of an XOR texture
      const float cell = std::max(8.0f, size.x / 100);
      float a = t * 0.4f, zoom = 1.6f + std::sin(t * 0.6f) * 0.9f - peak * 0.4f;
      float ca = std::cos(a) * zoom, sa = std::sin(a) * zoom;
      for (float y = p0.y; y < p1.y; y += cell)
        for (float x = p0.x; x < p1.x; x += cell) {
          float dx = x - c.x, dy = y - c.y;
          int u = (int)std::floor((dx * ca - dy * sa) * 0.5f + t * 40), v = (int)std::floor((dx * sa + dy * ca) * 0.5f);
          int pattern = (u ^ v) & 0x3F;
          dl->AddRectFilled(ImVec2(x, y), ImVec2(x + cell + 1, y + cell + 1), hsv(hueShift + pattern / 256.0f, 0.75f, 0.15f + pattern / 120.0f));
        }
      break;
    }
    default:
      break;
  }
}

// A spinning 3D vector object, flat-shaded, swelling with the music.
void KeygenScene::object(ImDrawList* dl, ImVec2 c, float size, float t, float peak) {
  struct V3 {
    float x, y, z;
  };
  static const V3 CUBE[] = {{-1, -1, -1}, {1, -1, -1}, {1, 1, -1}, {-1, 1, -1}, {-1, -1, 1}, {1, -1, 1}, {1, 1, 1}, {-1, 1, 1}};
  static const V3 PYRAMID[] = {{-1, 1, -1}, {1, 1, -1}, {1, 1, 1}, {-1, 1, 1}, {0, -1.3f, 0}};
  static const V3 OCTA[] = {{1.3f, 0, 0}, {-1.3f, 0, 0}, {0, 1.3f, 0}, {0, -1.3f, 0}, {0, 0, 1.3f}, {0, 0, -1.3f}};
  // Faces: vertex count, then the vertices.
  static const int CUBE_F[] = {4, 0, 1, 2, 3, 4, 5, 4, 7, 6, 4, 4, 0, 3, 7, 4, 1, 5, 6, 2, 4, 4, 5, 1, 0, 4, 3, 2, 6, 7, 0};
  static const int PYRAMID_F[] = {4, 0, 1, 2, 3, 3, 4, 1, 0, 3, 4, 2, 1, 3, 4, 3, 2, 3, 4, 0, 3, 0};
  static const int OCTA_F[] = {3, 0, 2, 4, 3, 4, 2, 1, 3, 1, 2, 5, 3, 5, 2, 0, 3, 4, 3, 0, 3, 1, 3, 4, 3, 5, 3, 1, 3, 0, 3, 5, 0};
  const V3* verts;
  const int* fdata;
  size_t nv;
  switch (style.object) {
    case 1: verts = CUBE, nv = 8, fdata = CUBE_F; break;
    case 2: verts = PYRAMID, nv = 5, fdata = PYRAMID_F; break;
    case 3: verts = OCTA, nv = 6, fdata = OCTA_F; break;
    default: return;
  }
  std::vector<V3> v(verts, verts + nv);
  std::vector<std::vector<int>> faces;
  for (const int* f = fdata; *f; f += *f + 1) faces.push_back(std::vector<int>(f + 1, f + 1 + *f));
  float ax = t * 0.7f, ay = t * 1.1f, az = t * 0.4f;
  float scale = size * (1.0f + peak * 0.35f);
  std::vector<V3> r(v.size());
  std::vector<ImVec2> s(v.size());
  for (size_t i = 0; i < v.size(); i++) {
    V3 p = v[i];
    float y1 = p.y * std::cos(ax) - p.z * std::sin(ax), z1 = p.y * std::sin(ax) + p.z * std::cos(ax);
    float x2 = p.x * std::cos(ay) + z1 * std::sin(ay), z2 = -p.x * std::sin(ay) + z1 * std::cos(ay);
    float x3 = x2 * std::cos(az) - y1 * std::sin(az), y3 = x2 * std::sin(az) + y1 * std::cos(az);
    r[i] = {x3, y3, z2};
    float persp = 3.5f / (z2 + 4.5f);
    s[i] = ImVec2(c.x + x3 * scale * persp, c.y + y3 * scale * persp);
  }
  // Painter's order, back faces skipped, shaded by how much they face us.
  std::vector<std::pair<float, int>> order;
  for (size_t f = 0; f < faces.size(); f++) {
    float z = 0;
    for (int i : faces[f]) z += r[i].z;
    order.push_back({z / faces[f].size(), (int)f});
  }
  std::sort(order.begin(), order.end(), [](auto& a, auto& b) { return a.first > b.first; });
  for (auto& [z, f] : order) {
    const std::vector<int>& face = faces[f];
    ImVec2 a = s[face[0]], b = s[face[1]], d = s[face[2]];
    float cross = (b.x - a.x) * (d.y - a.y) - (b.y - a.y) * (d.x - a.x);
    if (cross <= 0) continue;
    V3 e1{r[face[1]].x - r[face[0]].x, r[face[1]].y - r[face[0]].y, r[face[1]].z - r[face[0]].z};
    V3 e2{r[face[2]].x - r[face[0]].x, r[face[2]].y - r[face[0]].y, r[face[2]].z - r[face[0]].z};
    float nz = e1.x * e2.y - e1.y * e2.x, len = std::sqrt((e1.y * e2.z - e1.z * e2.y) * (e1.y * e2.z - e1.z * e2.y) +
                                                         (e1.z * e2.x - e1.x * e2.z) * (e1.z * e2.x - e1.x * e2.z) + nz * nz);
    float light = len > 0 ? std::abs(nz) / len : 1;
    float h = style.rainbow ? t * 0.1f + f * 0.12f : 0.5f;
    std::vector<ImVec2> pts;
    for (int i : face) pts.push_back(s[i]);
    ImU32 col;
    if (style.rainbow) {
      col = hsv(h, 0.75f, 0.25f + light * 0.7f, 230);
    } else {
      float k = 0.25f + light * 0.75f;
      col = IM_COL32((int)(((style.color >> 16) & 255) * k), (int)(((style.color >> 8) & 255) * k), (int)((style.color & 255) * k), 230);
    }
    dl->AddConvexPolyFilled(pts.data(), (int)pts.size(), col);
    dl->AddPolyline(pts.data(), (int)pts.size(), IM_COL32(255, 255, 255, 120), ImDrawFlags_Closed, 1.5f);
  }
}

void KeygenScene::draw(ImDrawList* dl, ImVec2 p0, ImVec2 size, float time, float dt, float level) {
  ImVec2 p1(p0.x + size.x, p0.y + size.y);
  float t = time;
  dt = std::min(dt, 0.1f);
  dl->PushClipRect(p0, p1, true);
  dl->AddRectFilled(p0, p1, IM_COL32(0, 0, 0, 255));

  // Music level drives the bars, the stars and the effects.
  float peak = std::clamp(level, 0.0f, 1.0f);

  // Background, possibly cycling with a fade through black.
  int kind = style.background;
  if (kind == 0 && !style.stars) kind = 4;
  float fade = 0;
  if (style.cycle) {
    float st = std::max(2.0f, style.sceneTime);
    kind = (int)std::floor(t / st) % 4;
    float into = std::fmod(t, st);
    fade = std::max(std::clamp(1.0f - into / 0.6f, 0.0f, 1.0f), std::clamp((into - (st - 0.6f)) / 0.6f, 0.0f, 1.0f));
  }
  background(dl, p0, p1, kind, t, dt, peak);
  if (fade > 0) dl->AddRectFilled(p0, p1, IM_COL32(0, 0, 0, (int)(fade * 255)));
  ImVec2 c((p0.x + p1.x) / 2, (p0.y + p1.y) / 2);

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

  // 3D object, between the logo and the scroller.
  if (style.object) object(dl, ImVec2(c.x, p0.y + size.y * 0.48f), std::min(size.x, size.y) * 0.13f, t, peak);

  ImFont* font = ImGui::GetFont();
  const KeygenStyle& st = style;
  auto hue = [&st](float h, int alpha) {
    if (!st.rainbow) return IM_COL32((st.color >> 16) & 255, (st.color >> 8) & 255, st.color & 255, alpha);
    return hsv(h, 0.9f, 1.0f, alpha);
  };

  // Logo.
  const char* logo = this->logo.c_str();
  float logoSize = std::clamp(size.y * 0.22f, 16.0f, 64.0f);
  if (style.logoFx == 1) logoSize *= 1.0f + peak * 0.25f;
  float lw = font->CalcTextSizeA(logoSize, FLT_MAX, 0, logo).x;
  float lx = c.x - lw / 2, ly = p0.y + size.y * 0.12f;
  bool glitch = style.logoFx == 2 && (peak > 0.55f || hash(std::floor(t * 8)) > 0.85f);
  for (const char* ch = logo; *ch; ch++) {
    int i = (int)(ch - logo);
    float oy = style.logoFx == 1 ? -std::abs(std::sin(t * 6 + i * 0.5f)) * logoSize * 0.15f * (0.3f + peak)
                                 : std::sin(t * 4 + i * 0.6f) * logoSize * 0.12f;
    float ox = 0;
    if (glitch) ox = (hash(i + std::floor(t * 20)) - 0.5f) * logoSize * 0.5f;
    if (glitch) {
      // Split colour channels.
      dl->AddText(font, logoSize, ImVec2(lx + ox - 3, ly + oy), IM_COL32(255, 0, 60, 160), ch, ch + 1);
      dl->AddText(font, logoSize, ImVec2(lx + ox + 3, ly + oy), IM_COL32(0, 200, 255, 160), ch, ch + 1);
    }
    dl->AddText(font, logoSize, ImVec2(lx + ox + 3, ly + oy + 3), IM_COL32(0, 0, 0, 200), ch, ch + 1);
    dl->AddText(font, logoSize, ImVec2(lx + ox, ly + oy), hue(t * 0.2f + i * 0.07f, 255), ch, ch + 1);
    lx += font->CalcTextSizeA(logoSize, FLT_MAX, 0, ch, ch + 1).x;
  }

  // Main scroller.
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
    if (style.scrollFx == 2) {
      // Roller: letters on a turning drum, squashed at its edges.
      float a = (x - c.x) / (size.x * 0.5f) * 1.4f;
      float k = std::cos(a);
      if (k <= 0.05f) continue;
      float h = fs * (0.4f + 0.6f * k);
      float y = baseY + std::sin(t * 2 + a) * size.y * 0.04f * style.wave + (fs - h) / 2;
      dl->AddText(font, h, ImVec2(x, y), hue(x * 0.002f + t * 0.3f, (int)(255 * k)), ch, ch + 1);
      continue;
    }
    float y = style.scrollFx == 1 ? baseY + size.y * 0.06f - std::abs(std::sin(t * 4.0f + x * 0.012f)) * size.y * 0.14f * style.wave
                                  : baseY + std::sin(t * 3.0f + x * 0.015f) * size.y * 0.1f * style.wave;
    dl->AddText(font, fs, ImVec2(x, y), hue(x * 0.002f + t * 0.3f, 255), ch, ch + 1);
  }

  // Greetings scroller, small, along the top.
  if (!greets.empty()) {
    std::string g = "      " + greets + "      ";
    for (char& ch : g) ch = (char)std::toupper((unsigned char)ch);
    float gs = std::clamp(size.y * 0.045f, 11.0f, 18.0f);
    float gw = font->CalcTextSizeA(gs, FLT_MAX, 0, g.c_str()).x;
    greetsPos_ += std::max(dt, 0.0f) * 60.0f * style.scrollSpeed;
    if (gw > 0) greetsPos_ = std::fmod(greetsPos_, gw);
    for (int k = 0; k < 3; k++) {
      float x = p0.x + k * gw - greetsPos_;
      if (x > p1.x || x + gw < p0.x) continue;
      dl->AddText(font, gs, ImVec2(x, p0.y + 4), IM_COL32(200, 200, 220, 210), g.c_str());
    }
  }

  // VU meters along the bottom, one per channel.
  if (style.vu && !levels.empty()) {
    vuShown_.resize(levels.size(), 0);
    int n = (int)levels.size();
    float w = std::min(28.0f, size.x * 0.6f / n), gap = 3;
    float total = n * (w + gap);
    float x0 = c.x - total / 2, yb = p0.y + size.y * 0.95f, hmax = size.y * 0.14f;
    for (int i = 0; i < n; i++) {
      float l = std::clamp(levels[i], 0.0f, 1.0f);
      vuShown_[i] = std::max(l, vuShown_[i] - dt * 1.5f);
      float h = vuShown_[i] * hmax;
      int segs = (int)(h / 5);
      for (int k = 0; k < segs; k++) {
        float y = yb - k * 5;
        float f = k * 5 / hmax;
        ImU32 col = f > 0.8f ? IM_COL32(255, 60, 60, 220) : f > 0.55f ? IM_COL32(255, 220, 60, 220) : IM_COL32(60, 255, 120, 220);
        dl->AddRectFilled(ImVec2(x0 + i * (w + gap), y - 4), ImVec2(x0 + i * (w + gap) + w, y), col);
      }
    }
  }

  // Flash on loud hits.
  if (style.flash) {
    if (peak > 0.6f && peak > lastPeak_ + 0.15f) flash_ = 0.5f;
    flash_ = std::max(0.0f, flash_ - dt * 2.5f);
    if (flash_ > 0) dl->AddRectFilled(p0, p1, IM_COL32(255, 255, 255, (int)(flash_ * 255)));
  }
  lastPeak_ = peak;
  dl->PopClipRect();
}
