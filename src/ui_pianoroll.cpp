// Piano roll: edits the current channel's pattern as note bars, FL Studio style.
// A tracker channel plays one note at a time, so overlapping notes are cut
// where the next one starts.
#include <imgui.h>

#include <algorithm>
#include <cmath>
#include <cstdio>

#include "app.h"
#include "theme.h"

namespace {

// Notes of a pattern: each lasts until the next note or note off.
std::vector<PRNote> readNotes(const Pattern* p, int len) {
  std::vector<PRNote> out;
  if (!p) return out;
  for (int r = 0; r < len; r++) {
    const Cell& c = p->rows[r];
    if (c.note == NOTE_EMPTY) continue;
    if (!out.empty() && out.back().start + out.back().len > r) out.back().len = r - out.back().start;
    if (c.note >= 0) out.push_back({r, len - r, c.note, c.ins, c.vol});
  }
  return out;
}

// Rewrites the note column. Instrument and volume move with their note;
// effects stay on their rows. On equal start rows the later note wins.
void writeNotes(Pattern* p, int len, std::vector<PRNote> notes) {
  for (int r = 0; r < len; r++) {
    Cell& c = p->rows[r];
    if (c.note != NOTE_EMPTY) {
      c.note = NOTE_EMPTY;
      c.ins = -1;
      c.vol = -1;
    }
  }
  std::stable_sort(notes.begin(), notes.end(), [](const PRNote& a, const PRNote& b) { return a.start < b.start; });
  std::vector<PRNote> uniq;
  for (const PRNote& n : notes) {
    if (n.start < 0 || n.start >= len) continue;
    if (!uniq.empty() && uniq.back().start == n.start) uniq.back() = n;
    else uniq.push_back(n);
  }
  for (size_t i = 0; i < uniq.size(); i++) {
    const PRNote& n = uniq[i];
    Cell& c = p->rows[n.start];
    c.note = (int16_t)std::clamp(n.note, 0, NOTE_COUNT - 1);
    c.ins = (int16_t)n.ins;
    c.vol = (int16_t)n.vol;
    int end = n.start + std::max(1, n.len);
    int next = i + 1 < uniq.size() ? uniq[i + 1].start : len;
    if (end < next && end < len) p->rows[end].note = NOTE_OFF;
  }
}

ImU32 insColor(int ins, int alpha) {
  float h = ins < 0 ? 0.55f : std::fmod(ins * 0.137f + 0.55f, 1.0f);
  float r, g, b;
  ImGui::ColorConvertHSVtoRGB(h, 0.55f, 0.95f, r, g, b);
  return IM_COL32((int)(r * 255), (int)(g * 255), (int)(b * 255), alpha);
}

bool isBlack(int note) {
  int k = note % 12;
  return k == 1 || k == 3 || k == 6 || k == 8 || k == 10;
}

}  // namespace

void App::pianoRollWindow() {
  // First time ever (no saved layout for it): open as a tab next to Pattern.
  if (patternDockId_) ImGui::SetNextWindowDockID(patternDockId_, ImGuiCond_FirstUseEver);
  if (!ImGui::Begin("Piano roll", &showPianoRoll_)) {
    ImGui::End();
    return;
  }
  ImGuiIO& io = ImGui::GetIO();
  const Theme& T = theme();
  int nch = song_.channelCount();
  curCh_ = std::clamp(curCh_, 0, nch - 1);
  curOrder_ = std::clamp(curOrder_, 0, (int)song_.orders.size() - 1);
  int ch = curCh_;
  int len = song_.patternLength;
  if (ch != prViewCh_ || curOrder_ != prViewOrder_) {
    prSel_.clear();
    prViewCh_ = ch;
    prViewOrder_ = curOrder_;
  }

  // Toolbar.
  ImGui::SetNextItemWidth(160);
  if (ImGui::BeginCombo("Channel", song_.channels[ch].name.c_str())) {
    for (int c = 0; c < nch; c++) {
      char buf[96];
      std::snprintf(buf, sizeof(buf), "%d: %s", c + 1, song_.channels[c].name.c_str());
      if (ImGui::Selectable(buf, c == ch)) {
        curCh_ = c;
        curCol_ = 0;
      }
    }
    ImGui::EndCombo();
  }
  ImGui::SameLine();
  ImGui::SetNextItemWidth(80);
  ImGui::InputInt("Length", &prLength_);
  prLength_ = std::clamp(prLength_, 1, MAX_ROWS);
  ImGui::SameLine();
  ImGui::Checkbox("Ghosts", &prGhosts_);
  ImGui::SameLine();
  curIns_ = std::clamp(curIns_, 0, std::max(0, (int)song_.instruments.size() - 1));
  char insLabel[128];
  std::snprintf(insLabel, sizeof(insLabel), "%02X: %s", curIns_, song_.instruments.empty() ? "" : song_.instruments[curIns_].name.c_str());
  ImGui::SetNextItemWidth(170);
  if (ImGui::BeginCombo("Instrument##pr", insLabel)) {
    for (int i = 0; i < (int)song_.instruments.size(); i++) {
      char buf[128];
      std::snprintf(buf, sizeof(buf), "%02X: %s", i, song_.instruments[i].name.c_str());
      if (ImGui::Selectable(buf, i == curIns_)) curIns_ = i;
    }
    ImGui::EndCombo();
  }
  ImGui::SameLine();
  ImGui::TextDisabled("Order %02X  |  Ctrl+wheel: zoom, Shift+wheel: scroll", curOrder_);

  Pattern* pat = song_.pattern(ch, song_.orders[curOrder_][ch], false);
  std::vector<PRNote> notes = readNotes(pat, len);

  // Layout.
  ImVec2 p0 = ImGui::GetCursorScreenPos();
  ImVec2 avail = ImGui::GetContentRegionAvail();
  if (avail.x < 120 || avail.y < 120) {
    ImGui::End();
    return;
  }
  const float keyW = 46, rulerH = 18, velH = std::min(70.0f, avail.y * 0.2f), gap = 4;
  ImVec2 g0(p0.x + keyW, p0.y + rulerH), g1(p0.x + avail.x, p0.y + avail.y - velH - gap);
  ImVec2 v0(g0.x, g1.y + gap), v1(g1.x, p0.y + avail.y);
  float zx = prZoomX_, zy = prZoomY_;
  float visRows = (g1.x - g0.x) / zx, visKeys = (g1.y - g0.y) / zy;

  if (prTop_ < 0) {
    // Start around the notes, or around the current octave.
    int lo = 127, hi = -1;
    for (auto& n : notes) {
      lo = std::min(lo, n.note);
      hi = std::max(hi, n.note);
    }
    float center = hi >= 0 ? (lo + hi) / 2.0f : octave_ * 12 + 6;
    prTop_ = center + visKeys / 2;
  }
  prTop_ = std::clamp(prTop_, std::min(visKeys, 120.0f), 120.0f);
  prScrollX_ = std::clamp(prScrollX_, 0.0f, std::max(0.0f, len - visRows + 1));

  auto rowX = [&](float row) { return g0.x + (row - prScrollX_) * zx; };
  auto noteY = [&](float note) { return g0.y + (prTop_ - note - 1) * zy; };
  auto xRow = [&](float x) { return (int)std::floor(prScrollX_ + (x - g0.x) / zx); };
  auto yNote = [&](float y) { return (int)std::floor(prTop_ - (y - g0.y) / zy); };

  bool playing = engine_->playing();
  if (playing && follow_ && engine_->order() == curOrder_) {
    float pr = (float)engine_->row();
    if (pr < prScrollX_ || pr > prScrollX_ + visRows - 2) prScrollX_ = std::clamp(pr - 2.0f, 0.0f, std::max(0.0f, len - visRows + 1));
  }

  ImGui::InvisibleButton("roll", avail, ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight);
  bool hovered = ImGui::IsItemHovered();
  bool focused = ImGui::IsWindowFocused();
  ImDrawList* dl = ImGui::GetWindowDrawList();
  dl->PushClipRect(p0, ImVec2(p0.x + avail.x, p0.y + avail.y), true);
  dl->AddRectFilled(p0, ImVec2(p0.x + avail.x, p0.y + avail.y), T.patBg);

  // Grid: key lanes, then row lines.
  dl->PushClipRect(g0, g1, true);
  int topNote = (int)std::ceil(prTop_), botNote = (int)std::floor(prTop_ - visKeys) - 1;
  for (int n = std::max(botNote, 0); n <= std::min(topNote, NOTE_COUNT - 1); n++) {
    float y = noteY((float)n);
    if (isBlack(n)) dl->AddRectFilled(ImVec2(g0.x, y), ImVec2(g1.x, y + zy), T.rowHi1);
    if (n % 12 == 0) dl->AddLine(ImVec2(g0.x, y + zy), ImVec2(g1.x, y + zy), T.separator);
  }
  for (int r = std::max(0, (int)prScrollX_); r <= len && rowX((float)r) <= g1.x; r++) {
    float x = rowX((float)r);
    ImU32 col = (song_.highlight2 > 0 && r % song_.highlight2 == 0) ? T.rowNum
                : (song_.highlight1 > 0 && r % song_.highlight1 == 0) ? T.separator
                                                                       : (T.rowHi2 & 0x80FFFFFF);
    dl->AddLine(ImVec2(x, g0.y), ImVec2(x, g1.y), col);
  }
  // Area past the end of the pattern.
  if (rowX((float)len) < g1.x) dl->AddRectFilled(ImVec2(rowX((float)len), g0.y), g1, IM_COL32(0, 0, 0, 110));

  // Ghost notes from the other channels.
  if (prGhosts_) {
    for (int c = 0; c < nch; c++) {
      if (c == ch) continue;
      for (const PRNote& n : readNotes(song_.pattern(c, song_.orders[curOrder_][c]), len)) {
        float x0 = rowX((float)n.start), x1 = rowX((float)(n.start + n.len)), y = noteY((float)n.note);
        if (x1 < g0.x || x0 > g1.x) continue;
        dl->AddRect(ImVec2(x0 + 1, y + 1), ImVec2(x1 - 1, y + zy - 1), IM_COL32(255, 255, 255, 45), 2);
      }
    }
  }

  // This channel's notes.
  int hoverNote = -1;
  bool hoverEdge = false;
  ImVec2 m = io.MousePos;
  bool inGrid = hovered && m.x >= g0.x && m.x < g1.x && m.y >= g0.y && m.y < g1.y;
  for (size_t i = 0; i < notes.size(); i++) {
    const PRNote& n = notes[i];
    float x0 = rowX((float)n.start), x1 = rowX((float)(n.start + n.len)), y = noteY((float)n.note);
    bool sel = prSel_.count(n.start) > 0;
    if (x1 >= g0.x && x0 <= g1.x) {
      dl->AddRectFilled(ImVec2(x0 + 1, y + 1), ImVec2(x1 - 1, y + zy - 1), insColor(n.ins, sel ? 255 : 210), 3);
      dl->AddRect(ImVec2(x0 + 1, y + 1), ImVec2(x1 - 1, y + zy - 1), sel ? IM_COL32(255, 255, 255, 255) : IM_COL32(0, 0, 0, 120), 3, 0,
                  sel ? 2.0f : 1.0f);
      if (x1 - x0 > 30 && zy >= 10) {
        std::string label = noteName(n.note);
        dl->AddText(ImVec2(x0 + 4, y + (zy - ImGui::GetFontSize()) / 2), IM_COL32(20, 20, 30, 255), label.c_str());
      }
    }
    if (inGrid && m.x >= x0 && m.x < x1 && m.y >= y && m.y < y + zy) {
      hoverNote = (int)i;
      hoverEdge = m.x > x1 - std::clamp((x1 - x0) / 3, 3.0f, 8.0f);
    }
  }

  // Playback position and tracker cursor.
  if (playing && engine_->order() == curOrder_) {
    float x = rowX((float)engine_->row());
    dl->AddRectFilled(ImVec2(x, g0.y), ImVec2(x + zx, g1.y), T.playRow);
  }
  float cx = rowX((float)curRow_);
  dl->AddLine(ImVec2(cx, g0.y), ImVec2(cx, g1.y), T.cursorBox, 1.0f);

  // Selection rectangle.
  if (prDrag_ == PRDrag::Select)
    dl->AddRect(ImVec2(prMx0_, prMy0_), m, IM_COL32(255, 255, 255, 200), 0, 0, 1.0f);
  dl->PopClipRect();

  // Ruler.
  dl->AddRectFilled(ImVec2(g0.x, p0.y), ImVec2(g1.x, g0.y), T.header);
  dl->PushClipRect(ImVec2(g0.x, p0.y), ImVec2(g1.x, g0.y), true);
  int bar = std::max(1, song_.highlight2), beat = std::max(1, song_.highlight1);
  for (int r = std::max(0, (int)prScrollX_); r < len && rowX((float)r) <= g1.x; r++) {
    float x = rowX((float)r);
    if (r % bar == 0) {
      char buf[16];
      std::snprintf(buf, sizeof(buf), "%d", r / bar + 1);
      dl->AddLine(ImVec2(x, p0.y), ImVec2(x, g0.y), T.headerText);
      dl->AddText(ImVec2(x + 3, p0.y + 1), T.headerText, buf);
    } else if (r % beat == 0) {
      dl->AddLine(ImVec2(x, g0.y - 6), ImVec2(x, g0.y), T.headerText);
    }
  }
  dl->PopClipRect();

  // Keyboard.
  dl->PushClipRect(ImVec2(p0.x, g0.y), ImVec2(g0.x, g1.y), true);
  for (int n = std::max(botNote, 0); n <= std::min(topNote, NOTE_COUNT - 1); n++) {
    float y = noteY((float)n);
    bool black = isBlack(n);
    bool down = prPreview_ == n;
    ImU32 col = down ? T.cursorBox : black ? IM_COL32(25, 25, 30, 255) : IM_COL32(225, 225, 230, 255);
    dl->AddRectFilled(ImVec2(p0.x, y), ImVec2(black ? g0.x - 14 : g0.x - 1, y + zy), col);
    dl->AddLine(ImVec2(p0.x, y + zy), ImVec2(g0.x - 1, y + zy), IM_COL32(90, 90, 100, 255));
    if (n % 12 == 0 && zy >= 9) {
      std::string label = noteName(n);
      dl->AddText(ImVec2(g0.x - 26, y + (zy - ImGui::GetFontSize()) / 2), IM_COL32(40, 40, 50, 255), label.c_str());
    }
  }
  dl->PopClipRect();

  // Velocity lane (the volume column).
  dl->AddRectFilled(v0, v1, T.scopeBg);
  dl->AddText(ImVec2(p0.x + 4, v0.y + 2), T.rowNum, "Vol");
  dl->PushClipRect(v0, v1, true);
  float vh = v1.y - v0.y - 4;
  for (const PRNote& n : notes) {
    float x = rowX((float)n.start) + 2;
    if (x > v1.x || x < v0.x - zx) continue;
    int v = n.vol < 0 ? 0x7f : n.vol;
    float top = v1.y - 2 - vh * v / 127.0f;
    ImU32 col = insColor(n.ins, n.vol < 0 ? 110 : 255);
    dl->AddLine(ImVec2(x, v1.y - 2), ImVec2(x, top), col, 2.0f);
    dl->AddCircleFilled(ImVec2(x, top), 3.5f, col);
  }
  dl->PopClipRect();
  dl->PopClipRect();

  // --- Interaction ------------------------------------------------------------

  auto writeBack = [&](std::vector<PRNote> out) {
    if (!prUndone_) {
      pushUndo(ch, ch);
      prUndone_ = true;
    }
    Pattern* p = song_.pattern(ch, song_.orders[curOrder_][ch], true);
    writeNotes(p, len, std::move(out));
    dirty_ = true;
  };
  auto preview = [&](int note) {
    if (note == prPreview_) return;
    if (prPreview_ >= 0) engine_->noteOff(ch);
    prPreview_ = note;
    if (note >= 0 && !engine_->playing()) engine_->noteOn(ch, note, curIns_);
  };
  auto beginDrag = [&](PRDrag kind, const std::vector<PRNote>& base, int grab) {
    prDrag_ = kind;
    prOrig_ = base;
    prOrigSel_.assign(base.size(), false);
    for (size_t i = 0; i < base.size(); i++) prOrigSel_[i] = prSel_.count(base[i].start) > 0;
    prGrab_ = grab;
    prMx0_ = m.x;
    prMy0_ = m.y;
    prUndone_ = false;
  };

  bool inKeys = hovered && m.x < g0.x && m.y >= g0.y && m.y < g1.y;
  bool inVel = hovered && m.x >= v0.x && m.y >= v0.y;
  bool inRuler = hovered && m.x >= g0.x && m.y < g0.y;

  if (hovered && io.MouseWheel != 0) {
    if (io.KeyCtrl) {
      float anchor = prScrollX_ + (m.x - g0.x) / zx;
      prZoomX_ = std::clamp(prZoomX_ * (io.MouseWheel > 0 ? 1.2f : 1 / 1.2f), 4.0f, 80.0f);
      prScrollX_ = anchor - (m.x - g0.x) / prZoomX_;
    } else if (io.KeyAlt) {
      prZoomY_ = std::clamp(prZoomY_ * (io.MouseWheel > 0 ? 1.15f : 1 / 1.15f), 6.0f, 30.0f);
    } else if (io.KeyShift) {
      prScrollX_ -= io.MouseWheel * 4;
    } else {
      prTop_ += io.MouseWheel * 3;
    }
  }
  if (hovered && io.MouseWheelH != 0) prScrollX_ -= io.MouseWheelH * 4;

  if (ImGui::IsMouseClicked(ImGuiMouseButton_Left) && hovered) {
    if (inKeys) {
      prDrag_ = PRDrag::Key;
      preview(yNote(m.y));
    } else if (inRuler) {
      curRow_ = std::clamp(xRow(m.x), 0, len - 1);
      if (playing) {
        engine_->stop();
        engine_->play(curOrder_, curRow_);
      }
    } else if (inVel) {
      beginDrag(PRDrag::Velocity, notes, -1);
    } else if (inGrid) {
      if (hoverNote >= 0) {
        int start = notes[hoverNote].start;
        if (io.KeyShift) {
          prSel_.insert(start);
        } else if (!prSel_.count(start)) {
          prSel_ = {start};
        }
        beginDrag(hoverEdge ? PRDrag::Resize : PRDrag::Move, notes, hoverNote);
        if (!hoverEdge) preview(notes[hoverNote].note);
        prLength_ = notes[hoverNote].len;
        curRow_ = start;
      } else if (io.KeyCtrl) {
        if (!io.KeyShift) prSel_.clear();
        beginDrag(PRDrag::Select, notes, -1);
      } else {
        // New note, then drag it around.
        int row = std::clamp(xRow(m.x), 0, len - 1), note = std::clamp(yNote(m.y), 0, NOTE_COUNT - 1);
        std::vector<PRNote> base = notes;
        base.push_back({row, prLength_, note, curIns_, -1});
        prUndone_ = false;
        writeBack(base);
        bool undone = prUndone_;
        notes = readNotes(song_.pattern(ch, song_.orders[curOrder_][ch]), len);
        prSel_ = {row};
        int grab = -1;
        for (size_t i = 0; i < notes.size(); i++)
          if (notes[i].start == row) grab = (int)i;
        beginDrag(PRDrag::Move, notes, grab);
        prUndone_ = undone;
        preview(note);
        curRow_ = row;
      }
    }
  }
  if (ImGui::IsMouseClicked(ImGuiMouseButton_Right) && inGrid) {
    prDrag_ = PRDrag::Erase;
    prUndone_ = false;
  }

  if (prDrag_ != PRDrag::None && (ImGui::IsMouseDown(ImGuiMouseButton_Left) || ImGui::IsMouseDown(ImGuiMouseButton_Right))) {
    int dRow = (int)std::lround((m.x - prMx0_) / zx);
    int dNote = (int)std::lround(-(m.y - prMy0_) / zy);
    switch (prDrag_) {
      case PRDrag::Move: {
        if (dRow == 0 && dNote == 0 && !prUndone_) break;
        // Keep the whole selection inside the pattern and the note range.
        int minS = len, maxS = 0, minN = NOTE_COUNT, maxN = 0;
        for (size_t i = 0; i < prOrig_.size(); i++)
          if (prOrigSel_[i]) {
            minS = std::min(minS, prOrig_[i].start);
            maxS = std::max(maxS, prOrig_[i].start);
            minN = std::min(minN, prOrig_[i].note);
            maxN = std::max(maxN, prOrig_[i].note);
          }
        dRow = std::clamp(dRow, -minS, len - 1 - maxS);
        dNote = std::clamp(dNote, -minN, NOTE_COUNT - 1 - maxN);
        std::vector<PRNote> out, moved;
        std::set<int> sel;
        for (size_t i = 0; i < prOrig_.size(); i++) {
          PRNote n = prOrig_[i];
          if (prOrigSel_[i]) {
            n.start += dRow;
            n.note += dNote;
            moved.push_back(n);
            sel.insert(n.start);
          } else {
            out.push_back(n);
          }
        }
        out.insert(out.end(), moved.begin(), moved.end());  // moved notes win ties
        writeBack(out);
        prSel_ = sel;
        if (prGrab_ >= 0) preview(prOrig_[prGrab_].note + dNote);
        break;
      }
      case PRDrag::Resize: {
        std::vector<PRNote> out = prOrig_;
        for (size_t i = 0; i < out.size(); i++)
          if (prOrigSel_[i]) out[i].len = std::clamp(prOrig_[i].len + dRow, 1, len - prOrig_[i].start);
        if (dRow != 0 || prUndone_) writeBack(out);
        if (prGrab_ >= 0) prLength_ = out[prGrab_].len;
        break;
      }
      case PRDrag::Erase:
        if (hoverNote >= 0) {
          std::vector<PRNote> out = notes;
          prSel_.erase(out[hoverNote].start);
          out.erase(out.begin() + hoverNote);
          writeBack(out);
        }
        break;
      case PRDrag::Velocity: {
        int row = xRow(m.x);
        int v = std::clamp((int)std::lround((v1.y - 2 - m.y) / vh * 127), 0, 127);
        std::vector<PRNote> out = notes;
        bool hit = false;
        for (PRNote& n : out)
          if (row >= n.start && row < n.start + std::max(1, std::min(n.len, (int)std::ceil(8 / zx))) && n.vol != v) {
            n.vol = v;
            hit = true;
          }
        if (hit) writeBack(out);
        break;
      }
      case PRDrag::Select: {
        float x0 = std::min(prMx0_, m.x), x1 = std::max(prMx0_, m.x), y0 = std::min(prMy0_, m.y), y1 = std::max(prMy0_, m.y);
        std::set<int> sel;
        if (io.KeyShift)
          for (size_t i = 0; i < prOrig_.size(); i++)
            if (prOrigSel_[i]) sel.insert(prOrig_[i].start);
        for (const PRNote& n : notes) {
          float nx0 = rowX((float)n.start), nx1 = rowX((float)(n.start + n.len)), ny = noteY((float)n.note);
          if (nx1 > x0 && nx0 < x1 && ny + zy > y0 && ny < y1) sel.insert(n.start);
        }
        prSel_ = sel;
        break;
      }
      case PRDrag::Key:
        if (inKeys) preview(yNote(m.y));
        break;
      default:
        break;
    }
  } else if (prDrag_ != PRDrag::None) {
    prDrag_ = PRDrag::None;
    preview(-1);
  }

  // Keyboard shortcuts while the piano roll has focus.
  if (focused && !io.WantTextInput) {
    auto transform = [&](int dRow, int dNote, bool erase) {
      if (prSel_.empty()) return;
      std::vector<PRNote> out, moved;
      std::set<int> sel;
      for (const PRNote& n : notes) {
        if (!prSel_.count(n.start)) {
          out.push_back(n);
          continue;
        }
        if (erase) continue;
        PRNote k = n;
        k.start = std::clamp(n.start + dRow, 0, len - 1);
        k.note = std::clamp(n.note + dNote, 0, NOTE_COUNT - 1);
        moved.push_back(k);
        sel.insert(k.start);
      }
      out.insert(out.end(), moved.begin(), moved.end());
      prUndone_ = false;
      writeBack(out);
      prSel_ = sel;
    };
    if (ImGui::IsKeyPressed(ImGuiKey_Delete) || ImGui::IsKeyPressed(ImGuiKey_Backspace)) transform(0, 0, true);
    if (ImGui::IsKeyPressed(ImGuiKey_UpArrow)) transform(0, io.KeyShift ? 12 : 1, false);
    if (ImGui::IsKeyPressed(ImGuiKey_DownArrow)) transform(0, io.KeyShift ? -12 : -1, false);
    if (ImGui::IsKeyPressed(ImGuiKey_LeftArrow)) transform(-1, 0, false);
    if (ImGui::IsKeyPressed(ImGuiKey_RightArrow)) transform(1, 0, false);
    if (ImGui::IsKeyPressed(ImGuiKey_Escape)) prSel_.clear();
    if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_A)) {
      prSel_.clear();
      for (const PRNote& n : notes) prSel_.insert(n.start);
    }
  }

  // Cursor feedback.
  if (inGrid && prDrag_ == PRDrag::None) ImGui::SetMouseCursor(hoverEdge ? ImGuiMouseCursor_ResizeEW : hoverNote >= 0 ? ImGuiMouseCursor_Hand : ImGuiMouseCursor_Arrow);
  ImGui::End();
}
