// The icons are little vector shapes in a unit square (y down), turned into
// anti-aliased glyphs of the font atlas at whatever size the font has.
#include "icons.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace {

struct P {
  float x, y;
};

constexpr float PI = 3.14159265f;

float cross(P a, P b, P c) { return (b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x); }

bool tri(P p, P a, P b, P c) {
  float d1 = cross(a, b, p), d2 = cross(b, c, p), d3 = cross(c, a, p);
  bool neg = d1 < 0 || d2 < 0 || d3 < 0, pos = d1 > 0 || d2 > 0 || d3 > 0;
  return !(neg && pos);
}

bool poly(P p, std::initializer_list<P> pts) {
  std::vector<P> v(pts);
  bool in = false;
  for (size_t i = 0, j = v.size() - 1; i < v.size(); j = i++)
    if ((v[i].y > p.y) != (v[j].y > p.y) && p.x < (v[j].x - v[i].x) * (p.y - v[i].y) / (v[j].y - v[i].y) + v[i].x) in = !in;
  return in;
}

bool box(P p, float x0, float y0, float x1, float y1) { return p.x >= x0 && p.x <= x1 && p.y >= y0 && p.y <= y1; }

bool rbox(P p, float x0, float y0, float x1, float y1, float r) {
  if (!box(p, x0, y0, x1, y1)) return false;
  float cx = std::clamp(p.x, x0 + r, x1 - r), cy = std::clamp(p.y, y0 + r, y1 - r);
  return (p.x - cx) * (p.x - cx) + (p.y - cy) * (p.y - cy) <= r * r;
}

// The outline of a rounded box, `w` thick.
bool frame(P p, float x0, float y0, float x1, float y1, float r, float w) {
  return rbox(p, x0, y0, x1, y1, r) && !rbox(p, x0 + w, y0 + w, x1 - w, y1 - w, std::max(r - w, 0.0f));
}

bool disc(P p, float cx, float cy, float r) { return (p.x - cx) * (p.x - cx) + (p.y - cy) * (p.y - cy) <= r * r; }

bool ring(P p, float cx, float cy, float r0, float r1) { return disc(p, cx, cy, r1) && !disc(p, cx, cy, r0); }

// A stroke from a to b, `w` thick, with round ends.
bool seg(P p, float ax, float ay, float bx, float by, float w) {
  float dx = bx - ax, dy = by - ay;
  float t = std::clamp(((p.x - ax) * dx + (p.y - ay) * dy) / std::max(dx * dx + dy * dy, 1e-6f), 0.0f, 1.0f);
  float ex = ax + t * dx - p.x, ey = ay + t * dy - p.y;
  return ex * ex + ey * ey <= w * w / 4;
}

// Angles in degrees, 0 = right, growing clockwise (y is down).
float angleOf(P p, float cx, float cy) {
  float a = std::atan2(p.y - cy, p.x - cx) * 180 / PI;
  return a < 0 ? a + 360 : a;
}

// A ring piece of radius r, `w` thick, from angle a0 to a1 (clockwise).
bool arc(P p, float cx, float cy, float r, float w, float a0, float a1) {
  float d = std::sqrt((p.x - cx) * (p.x - cx) + (p.y - cy) * (p.y - cy));
  if (std::abs(d - r) > w / 2) return false;
  float a = angleOf(p, cx, cy);
  while (a < a0) a += 360;
  return a <= a1;
}

// An arrow head at angle `a` of a circle, pointing along it (dir +1 =
// clockwise, -1 = counterclockwise).
bool arrowHead(P p, float cx, float cy, float r, float a, float dir, float len, float hw) {
  float c = std::cos(a * PI / 180), s = std::sin(a * PI / 180);
  P b{cx + r * c, cy + r * s};
  P tip{b.x - s * dir * len, b.y + c * dir * len};
  return tri(p, tip, P{b.x + c * hw, b.y + s * hw}, P{b.x - c * hw, b.y - s * hw});
}

bool ellipse(P p, float cx, float cy, float rx, float ry, float k = 1.0f) {
  float dx = (p.x - cx) / rx, dy = (p.y - cy) / ry;
  return dx * dx + dy * dy <= k;
}

bool sineStroke(P p, float x0, float x1, float cy, float amp, float cycles, float w) {
  if (p.x < x0 || p.x > x1) return false;
  float t = (p.x - x0) / (x1 - x0) * 2 * PI * cycles;
  float y = cy - amp * std::sin(t);
  float slope = amp * std::cos(t) * 2 * PI * cycles / (x1 - x0);
  return std::abs(p.y - y) <= w / 2 * std::sqrt(1 + slope * slope);
}

P mirror(P p) { return P{1 - p.x, p.y}; }

bool undoShape(P p) {
  return arc(p, 0.52f, 0.56f, 0.28f, 0.12f, 270, 495) || arrowHead(p, 0.52f, 0.56f, 0.28f, 270, -1, 0.24f, 0.18f);
}

bool inside(int icon, P p) {
  switch (icon) {
    case 0: return tri(p, {0.24f, 0.12f}, {0.24f, 0.88f}, {0.88f, 0.5f});  // play
    case 1: return rbox(p, 0.2f, 0.14f, 0.42f, 0.86f, 0.05f) || rbox(p, 0.58f, 0.14f, 0.8f, 0.86f, 0.05f);  // pause
    case 2: return rbox(p, 0.18f, 0.18f, 0.82f, 0.82f, 0.08f);  // stop
    case 3: return disc(p, 0.5f, 0.5f, 0.34f);                   // record
    case 4:                                                      // loop
      return (frame(p, 0.1f, 0.26f, 0.9f, 0.74f, 0.16f, 0.11f) && !box(p, 0.52f, 0.2f, 0.66f, 0.4f) &&
              !box(p, 0.34f, 0.6f, 0.48f, 0.8f)) ||
             tri(p, {0.52f, 0.13f}, {0.52f, 0.47f}, {0.72f, 0.3f}) || tri(p, {0.48f, 0.53f}, {0.48f, 0.87f}, {0.28f, 0.7f});
    case 5:  // follow
      return seg(p, 0.5f, 0.1f, 0.5f, 0.55f, 0.13f) || tri(p, {0.24f, 0.46f}, {0.76f, 0.46f}, {0.5f, 0.74f}) ||
             rbox(p, 0.16f, 0.8f, 0.84f, 0.92f, 0.04f);
    case 6:  // metronome: a pyramid outline with its pendulum
      return (poly(p, {{0.14f, 0.92f}, {0.86f, 0.92f}, {0.64f, 0.08f}, {0.36f, 0.08f}}) &&
              !poly(p, {{0.3f, 0.8f}, {0.7f, 0.8f}, {0.56f, 0.2f}, {0.44f, 0.2f}})) ||
             seg(p, 0.5f, 0.72f, 0.82f, 0.16f, 0.1f) || box(p, 0.3f, 0.66f, 0.7f, 0.8f);
    case 7: return rbox(p, 0.12f, 0.14f, 0.26f, 0.86f, 0.03f) || tri(p, {0.36f, 0.14f}, {0.36f, 0.86f}, {0.9f, 0.5f});  // play row
    case 8: return rbox(p, 0.14f, 0.14f, 0.28f, 0.86f, 0.03f) || tri(p, {0.86f, 0.14f}, {0.86f, 0.86f}, {0.34f, 0.5f});  // to start
    case 9: return rbox(p, 0.06f, 0.3f, 0.94f, 0.86f, 0.07f) || rbox(p, 0.06f, 0.16f, 0.44f, 0.4f, 0.07f);  // folder
    case 10:  // file
      return rbox(p, 0.18f, 0.06f, 0.82f, 0.94f, 0.06f) && !tri(p, {0.56f, 0.0f}, {1.0f, 0.0f}, {1.0f, 0.44f}) &&
             !box(p, 0.3f, 0.5f, 0.7f, 0.57f) && !box(p, 0.3f, 0.68f, 0.7f, 0.75f);
    case 11:  // note
      return ellipse(p, 0.36f, 0.74f, 0.19f, 0.15f) || box(p, 0.45f, 0.12f, 0.56f, 0.74f) || seg(p, 0.5f, 0.16f, 0.82f, 0.38f, 0.12f);
    case 12: return sineStroke(p, 0.06f, 0.94f, 0.5f, 0.3f, 1.0f, 0.12f);  // wave
    case 13: {                                                           // piano
      if (!rbox(p, 0.06f, 0.14f, 0.94f, 0.86f, 0.07f)) return false;
      bool blackKey = box(p, 0.28f, 0.14f, 0.44f, 0.56f) || box(p, 0.56f, 0.14f, 0.72f, 0.56f);
      if (blackKey) return true;
      bool around = box(p, 0.22f, 0.14f, 0.5f, 0.62f) || box(p, 0.5f, 0.14f, 0.78f, 0.62f);
      bool split = (std::abs(p.x - 0.36f) < 0.03f || std::abs(p.x - 0.64f) < 0.03f) && p.y > 0.56f;
      return !around && !split;
    }
    case 14: return rbox(p, 0.42f, 0.12f, 0.58f, 0.88f, 0.04f) || rbox(p, 0.12f, 0.42f, 0.88f, 0.58f, 0.04f);  // plus
    case 15: return rbox(p, 0.12f, 0.42f, 0.88f, 0.58f, 0.04f);                                                   // minus
    case 16:  // trash
      return rbox(p, 0.12f, 0.16f, 0.88f, 0.28f, 0.03f) || rbox(p, 0.38f, 0.06f, 0.62f, 0.2f, 0.03f) ||
             (poly(p, {{0.2f, 0.34f}, {0.8f, 0.34f}, {0.74f, 0.94f}, {0.26f, 0.94f}}) && !box(p, 0.36f, 0.44f, 0.44f, 0.84f) &&
              !box(p, 0.56f, 0.44f, 0.64f, 0.84f));
    case 17:  // copy
      return frame(p, 0.36f, 0.32f, 0.92f, 0.92f, 0.08f, 0.1f) ||
             (frame(p, 0.08f, 0.08f, 0.64f, 0.68f, 0.08f, 0.1f) && !box(p, 0.32f, 0.28f, 1, 1));
    case 18:  // save
      return rbox(p, 0.1f, 0.1f, 0.9f, 0.9f, 0.08f) && !box(p, 0.28f, 0.1f, 0.68f, 0.38f) &&
             !rbox(p, 0.24f, 0.54f, 0.76f, 0.9f, 0.04f);
    case 19:  // open: a folder with an arrow
      return (rbox(p, 0.06f, 0.3f, 0.94f, 0.86f, 0.07f) || rbox(p, 0.06f, 0.16f, 0.44f, 0.4f, 0.07f)) &&
             !tri(p, {0.32f, 0.62f}, {0.68f, 0.62f}, {0.5f, 0.4f}) && !box(p, 0.44f, 0.6f, 0.56f, 0.8f);
    case 20: return undoShape(p);
    case 21: return undoShape(mirror(p));
    case 22: return ring(p, 0.42f, 0.42f, 0.2f, 0.31f) || seg(p, 0.64f, 0.64f, 0.88f, 0.88f, 0.14f);  // search
    case 23: {                                                                                        // gear
      float d = std::sqrt((p.x - 0.5f) * (p.x - 0.5f) + (p.y - 0.5f) * (p.y - 0.5f));
      float a = std::atan2(p.y - 0.5f, p.x - 0.5f);
      float outer = std::cos(8 * a) > -0.1f ? 0.46f : 0.35f;
      return d <= outer && d >= 0.14f;
    }
    case 24: return tri(p, {0.5f, 0.12f}, {0.12f, 0.6f}, {0.88f, 0.6f}) || rbox(p, 0.38f, 0.56f, 0.62f, 0.9f, 0.03f);  // up
    case 25: return tri(p, {0.5f, 0.88f}, {0.12f, 0.4f}, {0.88f, 0.4f}) || rbox(p, 0.38f, 0.1f, 0.62f, 0.44f, 0.03f);  // down
    case 26:  // home
      return (tri(p, {0.5f, 0.06f}, {0.04f, 0.5f}, {0.96f, 0.5f}) || box(p, 0.18f, 0.46f, 0.82f, 0.92f)) &&
             !box(p, 0.41f, 0.62f, 0.59f, 0.92f);
    case 27: {  // star
      std::vector<P> v;
      for (int i = 0; i < 10; i++) {
        float a = (-90 + i * 36) * PI / 180, r = i % 2 ? 0.2f : 0.46f;
        v.push_back({0.5f + r * std::cos(a), 0.54f + r * std::sin(a)});
      }
      bool in = false;
      for (size_t i = 0, j = v.size() - 1; i < v.size(); j = i++)
        if ((v[i].y > p.y) != (v[j].y > p.y) && p.x < (v[j].x - v[i].x) * (p.y - v[i].y) / (v[j].y - v[i].y) + v[i].x) in = !in;
      return in;
    }
    case 28:  // speaker
      return poly(p, {{0.08f, 0.36f}, {0.28f, 0.36f}, {0.52f, 0.12f}, {0.52f, 0.88f}, {0.28f, 0.64f}, {0.08f, 0.64f}}) ||
             arc(p, 0.5f, 0.5f, 0.2f, 0.09f, 315, 405) || arc(p, 0.5f, 0.5f, 0.38f, 0.09f, 315, 405);
    case 29:  // sliders
      return box(p, 0.22f, 0.08f, 0.28f, 0.92f) || box(p, 0.47f, 0.08f, 0.53f, 0.92f) || box(p, 0.72f, 0.08f, 0.78f, 0.92f) ||
             rbox(p, 0.12f, 0.58f, 0.38f, 0.76f, 0.04f) || rbox(p, 0.37f, 0.22f, 0.63f, 0.4f, 0.04f) ||
             rbox(p, 0.62f, 0.46f, 0.88f, 0.64f, 0.04f);
    case 30:  // scope
      return frame(p, 0.04f, 0.14f, 0.96f, 0.86f, 0.1f, 0.09f) || sineStroke(p, 0.18f, 0.82f, 0.5f, 0.18f, 1.5f, 0.09f);
    case 31: {  // chip
      if (rbox(p, 0.24f, 0.24f, 0.76f, 0.76f, 0.06f)) return !disc(p, 0.36f, 0.36f, 0.05f);
      for (float c : {0.34f, 0.5f, 0.66f}) {
        if (std::abs(p.x - c) < 0.04f && (box(p, 0, 0.08f, 1, 0.26f) || box(p, 0, 0.74f, 1, 0.92f))) return true;
        if (std::abs(p.y - c) < 0.04f && (box(p, 0.08f, 0, 0.26f, 1) || box(p, 0.74f, 0, 0.92f, 1))) return true;
      }
      return false;
    }
    case 32:  // export
      return (rbox(p, 0.1f, 0.5f, 0.9f, 0.92f, 0.06f) && !box(p, 0.21f, 0.4f, 0.79f, 0.81f)) ||
             tri(p, {0.5f, 0.06f}, {0.24f, 0.36f}, {0.76f, 0.36f}) || box(p, 0.43f, 0.32f, 0.57f, 0.66f);
    case 33:  // grid
      for (float y : {0.08f, 0.38f, 0.68f})
        for (float x : {0.08f, 0.38f, 0.68f})
          if (rbox(p, x, y, x + 0.24f, y + 0.24f, 0.04f)) return true;
      return false;
    case 34:  // list
      for (float y : {0.22f, 0.5f, 0.78f})
        if (disc(p, 0.16f, y, 0.08f) || rbox(p, 0.32f, y - 0.06f, 0.9f, y + 0.06f, 0.03f)) return true;
      return false;
    case 35:  // book
      return poly(p, {{0.06f, 0.18f}, {0.46f, 0.26f}, {0.46f, 0.9f}, {0.06f, 0.82f}}) ||
             poly(p, {{0.94f, 0.18f}, {0.54f, 0.26f}, {0.54f, 0.9f}, {0.94f, 0.82f}});
    case 36:  // refresh
      return arc(p, 0.5f, 0.52f, 0.3f, 0.12f, 60, 320) || arrowHead(p, 0.5f, 0.52f, 0.3f, 320, 1, 0.24f, 0.18f);
    case 37: return seg(p, 0.2f, 0.2f, 0.8f, 0.8f, 0.15f) || seg(p, 0.8f, 0.2f, 0.2f, 0.8f, 0.15f);  // close
    case 38: return seg(p, 0.36f, 0.18f, 0.66f, 0.5f, 0.15f) || seg(p, 0.66f, 0.5f, 0.36f, 0.82f, 0.15f);  // chevron right
    case 39: return seg(p, 0.18f, 0.36f, 0.5f, 0.66f, 0.15f) || seg(p, 0.5f, 0.66f, 0.82f, 0.36f, 0.15f);  // chevron down
    case 40:  // clone
      return (rbox(p, 0.34f, 0.34f, 0.92f, 0.92f, 0.08f) && !box(p, 0.58f, 0.46f, 0.68f, 0.8f) && !box(p, 0.46f, 0.58f, 0.8f, 0.68f)) ||
             (frame(p, 0.08f, 0.08f, 0.66f, 0.66f, 0.08f, 0.1f) && !box(p, 0.28f, 0.28f, 1, 1));
    case 41:  // spark
      return poly(p, {{0.42f, 0.04f}, {0.52f, 0.36f}, {0.84f, 0.46f}, {0.52f, 0.56f}, {0.42f, 0.9f}, {0.32f, 0.56f}, {0.0f, 0.46f}, {0.32f, 0.36f}}) ||
             poly(p, {{0.8f, 0.06f}, {0.84f, 0.18f}, {0.96f, 0.22f}, {0.84f, 0.26f}, {0.8f, 0.38f}, {0.76f, 0.26f}, {0.64f, 0.22f}, {0.76f, 0.18f}});
    case 42:  // timeline
      return rbox(p, 0.06f, 0.12f, 0.58f, 0.32f, 0.05f) || rbox(p, 0.3f, 0.4f, 0.94f, 0.6f, 0.05f) ||
             rbox(p, 0.14f, 0.68f, 0.72f, 0.88f, 0.05f);
    case 43: {  // drum
      bool body = ellipse(p, 0.5f, 0.38f, 0.4f, 0.16f) || box(p, 0.1f, 0.38f, 0.9f, 0.74f) || ellipse(p, 0.5f, 0.74f, 0.4f, 0.16f);
      bool rim = p.y > 0.38f && ellipse(p, 0.5f, 0.38f, 0.4f, 0.16f, 1.9f) && !ellipse(p, 0.5f, 0.38f, 0.4f, 0.16f, 1.25f);
      return (body && !rim) || seg(p, 0.3f, 0.06f, 0.48f, 0.3f, 0.07f) || seg(p, 0.7f, 0.06f, 0.52f, 0.3f, 0.07f);
    }
    case 44:  // song: beamed notes
      return ellipse(p, 0.27f, 0.76f, 0.15f, 0.12f) || ellipse(p, 0.72f, 0.66f, 0.15f, 0.12f) || box(p, 0.33f, 0.2f, 0.42f, 0.76f) ||
             box(p, 0.78f, 0.1f, 0.87f, 0.66f) || poly(p, {{0.33f, 0.2f}, {0.87f, 0.1f}, {0.87f, 0.26f}, {0.33f, 0.36f}});
    case 45:  // fx: a knob
      return (disc(p, 0.5f, 0.54f, 0.34f) && !seg(p, 0.5f, 0.54f, 0.68f, 0.3f, 0.1f)) || arc(p, 0.5f, 0.54f, 0.45f, 0.06f, 135, 405);
    case 46: {  // keyboard
      if (!rbox(p, 0.02f, 0.22f, 0.98f, 0.8f, 0.08f)) return false;
      for (float y : {0.32f, 0.47f})
        for (float x = 0.12f; x < 0.86f; x += 0.16f)
          if (box(p, x, y, x + 0.1f, y + 0.09f)) return false;
      return !box(p, 0.28f, 0.62f, 0.72f, 0.7f);
    }
    case 47:  // info
      return disc(p, 0.5f, 0.5f, 0.46f) && !rbox(p, 0.43f, 0.42f, 0.57f, 0.78f, 0.03f) && !disc(p, 0.5f, 0.27f, 0.08f);
  }
  return false;
}

struct Pending {
  int rect, icon, size;
};
std::vector<Pending> pending;

}  // namespace

void addIconGlyphs(ImFontAtlas* atlas, ImFont* font, float size) {
  int box = std::max(6, (int)std::lround(size * 0.8f));
  float advance = (float)box + std::max(2.0f, std::round(size * 0.18f));
  ImVec2 offset(std::round(std::max(1.0f, size * 0.09f)), std::round((size - box) * 0.5f));
  for (int i = 0; i < ICON_COUNT; i++) {
    int id = atlas->AddCustomRectFontGlyph(font, (ImWchar)(ICON_FIRST + i), box, box, advance, offset);
    pending.push_back({id, i, box});
  }
}

void drawIconGlyphs(ImFontAtlas* atlas) {
  unsigned char* px = nullptr;
  int tw = 0, th = 0;
  atlas->GetTexDataAsAlpha8(&px, &tw, &th);
  const int SS = 4;  // 4 x 4 samples per pixel
  for (const Pending& g : pending) {
    const ImFontAtlasCustomRect* r = atlas->GetCustomRectByIndex(g.rect);
    if (!r || !px) continue;
    for (int y = 0; y < g.size; y++)
      for (int x = 0; x < g.size; x++) {
        int hits = 0;
        for (int sy = 0; sy < SS; sy++)
          for (int sx = 0; sx < SS; sx++)
            hits += inside(g.icon, P{(x + (sx + 0.5f) / SS) / g.size, (y + (sy + 0.5f) / SS) / g.size});
        px[(r->Y + y) * tw + r->X + x] = (unsigned char)(hits * 255 / (SS * SS));
      }
  }
  pending.clear();
}
