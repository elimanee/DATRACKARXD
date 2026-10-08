// Pattern editor: drawing, cursor, keyboard entry, selection, clipboard, undo.
#include <imgui.h>

#include <algorithm>
#include <cstdio>

#include "app.h"
#include "theme.h"

namespace {

// Character offset and width of each sub-column inside a channel.
int subColChar(int col) {
  if (col == 0) return 0;
  if (col <= 2) return 4 + (col - 1);
  if (col <= 4) return 7 + (col - 3);
  int e = (col - 5) / 4, k = (col - 5) % 4;
  return 10 + 5 * e + k;
}
int subColWidth(int col) { return col == 0 ? 3 : 1; }
int channelChars(int fxCols) { return 11 + 5 * fxCols; }

int charToSubCol(int c, int colCount) {
  int col;
  if (c < 4) col = 0;
  else if (c < 6) col = 1 + (c - 4);
  else if (c == 6) col = 2;
  else if (c < 10) col = std::min(3 + (c - 7), 4);
  else {
    int e = (c - 10) / 5, k = std::min((c - 10) % 5, 3);
    col = 5 + 4 * e + k;
  }
  return std::clamp(col, 0, colCount - 1);
}

// Fields: 0 note, 1 instrument, 2 volume, 3+e effect e.
int colToField(int col) {
  if (col == 0) return 0;
  if (col <= 2) return 1;
  if (col <= 4) return 2;
  return 3 + (col - 5) / 4;
}

void copyField(Cell& dst, const Cell& src, int field) {
  switch (field) {
    case 0: dst.note = src.note; break;
    case 1: dst.ins = src.ins; break;
    case 2: dst.vol = src.vol; break;
    default: dst.fx[field - 3] = src.fx[field - 3]; break;
  }
}

void clearField(Cell& c, int field) {
  static const Cell empty;
  copyField(c, empty, field);
}

void hex2(char* buf, int v) {
  if (v < 0)
    std::snprintf(buf, 3, "..");
  else
    std::snprintf(buf, 3, "%02X", v & 0xff);
}


}  // namespace

int scancodeToNote(SDL_Scancode sc) {
  switch (sc) {
    case SDL_SCANCODE_Z: return 0;
    case SDL_SCANCODE_S: return 1;
    case SDL_SCANCODE_X: return 2;
    case SDL_SCANCODE_D: return 3;
    case SDL_SCANCODE_C: return 4;
    case SDL_SCANCODE_V: return 5;
    case SDL_SCANCODE_G: return 6;
    case SDL_SCANCODE_B: return 7;
    case SDL_SCANCODE_H: return 8;
    case SDL_SCANCODE_N: return 9;
    case SDL_SCANCODE_J: return 10;
    case SDL_SCANCODE_M: return 11;
    case SDL_SCANCODE_COMMA: return 12;
    case SDL_SCANCODE_L: return 13;
    case SDL_SCANCODE_PERIOD: return 14;
    case SDL_SCANCODE_SEMICOLON: return 15;
    case SDL_SCANCODE_SLASH: return 16;
    case SDL_SCANCODE_Q: return 12;
    case SDL_SCANCODE_2: return 13;
    case SDL_SCANCODE_W: return 14;
    case SDL_SCANCODE_3: return 15;
    case SDL_SCANCODE_E: return 16;
    case SDL_SCANCODE_R: return 17;
    case SDL_SCANCODE_5: return 18;
    case SDL_SCANCODE_T: return 19;
    case SDL_SCANCODE_6: return 20;
    case SDL_SCANCODE_Y: return 21;
    case SDL_SCANCODE_7: return 22;
    case SDL_SCANCODE_U: return 23;
    case SDL_SCANCODE_I: return 24;
    case SDL_SCANCODE_9: return 25;
    case SDL_SCANCODE_O: return 26;
    case SDL_SCANCODE_0: return 27;
    case SDL_SCANCODE_P: return 28;
    case SDL_SCANCODE_LEFTBRACKET: return 29;
    case SDL_SCANCODE_EQUALS: return 30;
    case SDL_SCANCODE_RIGHTBRACKET: return 31;
    default: return -1;
  }
}

static int scancodeToHex(const KeyEvent& ev) {
  if (ev.sc >= SDL_SCANCODE_1 && ev.sc <= SDL_SCANCODE_9) return ev.sc - SDL_SCANCODE_1 + 1;
  if (ev.sc == SDL_SCANCODE_0 || ev.sc == SDL_SCANCODE_KP_0) return 0;
  if (ev.sc >= SDL_SCANCODE_KP_1 && ev.sc <= SDL_SCANCODE_KP_9) return ev.sc - SDL_SCANCODE_KP_1 + 1;
  // Letters follow the keyboard layout, so A-F are where they are printed.
  if (ev.key >= SDLK_a && ev.key <= SDLK_f) return 10 + (ev.key - SDLK_a);
  return -1;
}

int App::colCount(int ch) const { return 5 + 4 * song_.channels[ch].effectCols; }

Cell* App::cursorCell(bool create) { return song_.cell(curCh_, curOrder_, curRow_, create); }

void App::setCursorFromPlayback() {
  curOrder_ = engine_->order();
  curRow_ = engine_->row();
}

void App::moveRows(int d) {
  int len = song_.patternLength, numOrders = (int)song_.orders.size();
  curRow_ += d;
  while (curRow_ < 0) {
    if (curOrder_ > 0) {
      curOrder_--;
      curRow_ += len;
      hasSel_ = false;
    } else {
      curRow_ = 0;
    }
  }
  while (curRow_ >= len) {
    if (curOrder_ < numOrders - 1) {
      curOrder_++;
      curRow_ -= len;
      hasSel_ = false;
    } else {
      curRow_ = len - 1;
    }
  }
}

void App::moveCursor(int dRow, int dCol) {
  if (dRow) moveRows(dRow);
  if (dCol) {
    curCol_ += dCol;
    if (curCol_ < 0) {
      if (curCh_ > 0) {
        curCh_--;
        curCol_ = colCount(curCh_) - 1;
      } else {
        curCol_ = 0;
      }
    } else if (curCol_ >= colCount(curCh_)) {
      if (curCh_ < song_.channelCount() - 1) {
        curCh_++;
        curCol_ = 0;
      } else {
        curCol_ = colCount(curCh_) - 1;
      }
    }
  }
}

// ---------------------------------------------------------------------------
// Undo
// ---------------------------------------------------------------------------

void App::pushUndo(int ch0, int ch1, int order) {
  commitMetaEdit();
  if (order < 0) order = curOrder_;
  UndoStep step;
  step.order = order;
  step.row = curRow_;
  step.ch = curCh_;
  step.col = curCol_;
  for (int c = std::max(ch0, 0); c <= std::min(ch1, song_.channelCount() - 1); c++) {
    int pat = song_.orders[order][c];
    const Pattern* p = song_.pattern(c, pat);
    step.entries.push_back({c, pat, p ? p->rows : std::vector<Cell>()});
  }
  undo_.push_back(std::move(step));
  trimUndo();
  redo_.clear();
  dirty_ = true;
}

// Keeps the history bounded: 200 steps, at most 20 full song copies.
void App::trimUndo() {
  if (undo_.size() > 200) undo_.erase(undo_.begin(), undo_.begin() + (undo_.size() - 200));
  int fulls = 0;
  for (auto& u : undo_) fulls += u.kind == UndoStep::Full;
  while (fulls > 20) {
    auto it = std::find_if(undo_.begin(), undo_.end(), [](const UndoStep& u) { return u.kind == UndoStep::Full; });
    undo_.erase(undo_.begin(), it + 1);
    fulls--;
  }
}

// Song without pattern and sample data: what instrument, order and setting
// edits can change.
Song App::metaSnapshot() {
  Song m;
  m.name = song_.name;
  m.author = song_.author;
  m.comment = song_.comment;
  m.tickRate = song_.tickRate;
  m.speeds = song_.speeds;
  m.patternLength = song_.patternLength;
  m.highlight1 = song_.highlight1;
  m.highlight2 = song_.highlight2;
  m.insResetsVolume = song_.insResetsVolume;
  m.orders = song_.orders;
  m.instruments = song_.instruments;
  m.wavetables = song_.wavetables;
  m.fx = song_.fx;
  m.channels.clear();
  for (auto& c : song_.channels) {
    std::vector<Pattern> pats = std::move(c.patterns);
    m.channels.push_back(c);
    c.patterns = std::move(pats);
  }
  m.samples.clear();
  for (auto& smp : song_.samples) {
    std::vector<float> data = std::move(smp.data);
    m.samples.push_back(smp);
    smp.data = std::move(data);
  }
  return m;
}

void App::restoreMeta(const Song& m) {
  song_.name = m.name;
  song_.author = m.author;
  song_.comment = m.comment;
  song_.tickRate = m.tickRate;
  song_.speeds = m.speeds;
  song_.patternLength = m.patternLength;
  song_.highlight1 = m.highlight1;
  song_.highlight2 = m.highlight2;
  song_.insResetsVolume = m.insResetsVolume;
  song_.orders = m.orders;
  song_.instruments = m.instruments;
  song_.wavetables = m.wavetables;
  song_.fx = m.fx;
  for (size_t c = 0; c < m.channels.size() && c < song_.channels.size(); c++) {
    std::vector<Pattern> pats = std::move(song_.channels[c].patterns);
    song_.channels[c] = m.channels[c];
    song_.channels[c].patterns = std::move(pats);
  }
  for (size_t i = 0; i < m.samples.size() && i < song_.samples.size(); i++) {
    std::vector<float> data = std::move(song_.samples[i].data);
    song_.samples[i] = m.samples[i];
    song_.samples[i].data = std::move(data);
  }
}

// Turns pending setting edits into one undo step (called when the edit gesture ends).
void App::commitMetaEdit() {
  if (shadowStale_) {
    metaShadow_ = metaSnapshot();
    shadowStale_ = false;
    metaTouched_ = false;
    return;
  }
  if (!metaTouched_) return;
  UndoStep step;
  step.kind = UndoStep::Meta;
  step.song = std::make_shared<Song>(std::move(metaShadow_));
  step.order = curOrder_;
  step.row = curRow_;
  step.ch = curCh_;
  step.col = curCol_;
  undo_.push_back(std::move(step));
  trimUndo();
  redo_.clear();
  metaShadow_ = metaSnapshot();
  metaTouched_ = false;
}

// Call before a structural change: snapshots the whole song.
void App::pushFullUndo() {
  commitMetaEdit();
  UndoStep step;
  step.kind = UndoStep::Full;
  step.song = std::make_shared<Song>(song_);
  step.order = curOrder_;
  step.row = curRow_;
  step.ch = curCh_;
  step.col = curCol_;
  undo_.push_back(std::move(step));
  trimUndo();
  redo_.clear();
  shadowStale_ = true;
  dirty_ = true;
}

void App::resetUndo() {
  undo_.clear();
  redo_.clear();
  metaShadow_ = metaSnapshot();
  metaTouched_ = false;
  shadowStale_ = false;
}

void App::applyUndo(std::vector<UndoStep>& from, std::vector<UndoStep>& to) {
  commitMetaEdit();
  if (from.empty()) return;
  UndoStep step = std::move(from.back());
  from.pop_back();
  UndoStep inverse = step;
  inverse.order = curOrder_;
  inverse.row = curRow_;
  inverse.ch = curCh_;
  inverse.col = curCol_;
  switch (step.kind) {
    case UndoStep::Patterns:
      for (size_t i = 0; i < step.entries.size(); i++) {
        auto& e = step.entries[i];
        if (e.ch >= song_.channelCount()) continue;
        Pattern& p = song_.channels[e.ch].patterns[e.pat];
        inverse.entries[i].rows = p.rows;
        p.rows = e.rows;
      }
      inverse.order = step.order;
      inverse.row = step.row;
      inverse.ch = step.ch;
      inverse.col = step.col;
      break;
    case UndoStep::Meta:
      inverse.song = std::make_shared<Song>(metaSnapshot());
      restoreMeta(*step.song);
      break;
    case UndoStep::Full:
      inverse.song = std::make_shared<Song>(song_);
      if (step.song->channelCount() != song_.channelCount()) engine_->stop();
      song_ = *step.song;
      break;
  }
  metaShadow_ = metaSnapshot();
  to.push_back(std::move(inverse));
  curIns_ = std::clamp(curIns_, 0, std::max(0, (int)song_.instruments.size() - 1));
  curSample_ = std::min(curSample_, (int)song_.samples.size() - 1);
  curOrder_ = std::min(step.order, (int)song_.orders.size() - 1);
  curRow_ = step.row;
  curCh_ = std::min(step.ch, song_.channelCount() - 1);
  curCol_ = std::min(step.col, colCount(curCh_) - 1);
  hasSel_ = false;
  dirty_ = true;
}

// ---------------------------------------------------------------------------
// Editing
// ---------------------------------------------------------------------------

void App::enterNote(int note) {
  if (!editMode_) return;
  pushUndo(curCh_, curCh_);
  Cell* c = cursorCell(true);
  c->note = (int16_t)note;
  if (note >= 0) c->ins = (int16_t)curIns_;
  if (note == NOTE_OFF) c->ins = -1;
  moveRows(editStep_);
}

void App::enterHex(int digit) {
  if (!editMode_ || curCol_ == 0) return;
  pushUndo(curCh_, curCh_);
  Cell* c = cursorCell(true);
  int16_t* target;
  int max = 0xff;
  bool high;
  if (curCol_ <= 2) {
    target = &c->ins;
    high = curCol_ == 1;
  } else if (curCol_ <= 4) {
    target = &c->vol;
    max = 0x7f;
    high = curCol_ == 3;
  } else {
    int e = (curCol_ - 5) / 4, k = (curCol_ - 5) % 4;
    target = k < 2 ? &c->fx[e].cmd : &c->fx[e].val;
    high = k % 2 == 0;
  }
  int v = std::max<int>(*target, 0);
  v = high ? (digit << 4) | (v & 15) : (v & 0xf0) | digit;
  *target = (int16_t)std::min(v, max);
  if (target == &c->ins && *target < (int)song_.instruments.size()) curIns_ = *target;
  if (high) {
    curCol_++;
  } else {
    curCol_--;
    moveRows(editStep_);
  }
}

void App::deleteAtCursor() {
  if (!editMode_) return;
  if (hasSel_) {
    clearSelection();
    return;
  }
  Cell* c = cursorCell(false);
  if (c) {
    pushUndo(curCh_, curCh_);
    int field = colToField(curCol_);
    if (field == 0) {
      clearField(*c, 0);
      clearField(*c, 1);
      clearField(*c, 2);
    } else {
      clearField(*c, field);
    }
  }
  moveRows(editStep_);
}

void App::insertRow(bool insert) {
  if (!editMode_) return;
  Pattern* p = song_.pattern(curCh_, song_.orders[curOrder_][curCh_], false);
  if (!p) return;
  pushUndo(curCh_, curCh_);
  int last = song_.patternLength - 1;
  if (insert) {
    for (int r = last; r > curRow_; r--) p->rows[r] = p->rows[r - 1];
    p->rows[curRow_] = Cell();
  } else {
    for (int r = curRow_; r < last; r++) p->rows[r] = p->rows[r + 1];
    p->rows[last] = Cell();
  }
}

void App::startSelection() {
  if (!hasSel_) {
    hasSel_ = true;
    selCh0_ = curCh_;
    selCol0_ = curCol_;
    selRow0_ = curRow_;
  }
}

void App::selectionBounds(int& ch0, int& f0, int& row0, int& ch1, int& f1, int& row1) const {
  ch0 = selCh0_;
  f0 = colToField(selCol0_);
  ch1 = selCh1_;
  f1 = colToField(selCol1_);
  if (ch0 > ch1 || (ch0 == ch1 && f0 > f1)) {
    std::swap(ch0, ch1);
    std::swap(f0, f1);
  }
  row0 = std::min(selRow0_, selRow1_);
  row1 = std::max(selRow0_, selRow1_);
}

// Calls fn(ch, row, field) for every field inside the selection.
template <typename Fn>
static void forEachSelected(int ch0, int f0, int row0, int ch1, int f1, int row1, const Song& song, Fn fn) {
  for (int c = ch0; c <= ch1 && c < song.channelCount(); c++) {
    int maxField = 2 + song.channels[c].effectCols;
    int first = c == ch0 ? f0 : 0;
    int last = c == ch1 ? std::min(f1, maxField) : maxField;
    for (int r = row0; r <= row1; r++)
      for (int f = first; f <= last; f++) fn(c, r, f);
  }
}

void App::copySelection(bool cut) {
  if (!hasSel_) {
    // Nothing selected: copy the current cell.
    selCh0_ = selCh1_ = curCh_;
    selCol0_ = selCol1_ = curCol_;
    selRow0_ = selRow1_ = curRow_;
  }
  int ch0, f0, row0, ch1, f1, row1;
  selectionBounds(ch0, f0, row0, ch1, f1, row1);
  clipboard_.startCol = f0;
  clipboard_.endCol = f1;
  clipboard_.cells.assign(ch1 - ch0 + 1, std::vector<Cell>(row1 - row0 + 1));
  forEachSelected(ch0, f0, row0, ch1, f1, row1, song_, [&](int c, int r, int f) {
    const Cell* src = song_.cell(c, curOrder_, r);
    if (src) copyField(clipboard_.cells[c - ch0][r - row0], *src, f);
  });
  if (cut) clearSelection();
  setStatus(cut ? "Cut" : "Copied");
}

void App::clearSelection() {
  if (!editMode_ || !hasSel_) return;
  int ch0, f0, row0, ch1, f1, row1;
  selectionBounds(ch0, f0, row0, ch1, f1, row1);
  pushUndo(ch0, ch1);
  forEachSelected(ch0, f0, row0, ch1, f1, row1, song_, [&](int c, int r, int f) {
    Cell* cell = song_.cell(c, curOrder_, r, false);
    if (cell) clearField(*cell, f);
  });
}

void App::paste() {
  if (!editMode_ || clipboard_.cells.empty()) return;
  int nch = (int)clipboard_.cells.size(), nrows = (int)clipboard_.cells[0].size();
  int ch1 = std::min(curCh_ + nch - 1, song_.channelCount() - 1);
  pushUndo(curCh_, ch1);
  int row1 = std::min(curRow_ + nrows - 1, song_.patternLength - 1);
  forEachSelected(curCh_, clipboard_.startCol, curRow_, curCh_ + nch - 1, clipboard_.endCol, row1, song_,
                  [&](int c, int r, int f) {
                    Cell* dst = song_.cell(c, curOrder_, r, true);
                    if (dst) copyField(*dst, clipboard_.cells[c - curCh_][r - curRow_], f);
                  });
}

void App::transpose(int semitones) {
  if (!editMode_) return;
  int ch0 = curCh_, ch1 = curCh_, row0 = curRow_, row1 = curRow_, f0 = 0, f1 = 0;
  if (hasSel_) selectionBounds(ch0, f0, row0, ch1, f1, row1);
  pushUndo(ch0, ch1);
  for (int c = ch0; c <= ch1; c++)
    for (int r = row0; r <= row1; r++) {
      Cell* cell = song_.cell(c, curOrder_, r, false);
      if (cell && cell->note >= 0) cell->note = (int16_t)std::clamp(cell->note + semitones, 0, 119);
    }
}

// ---------------------------------------------------------------------------
// Keyboard
// ---------------------------------------------------------------------------

void App::patternKey(const KeyEvent& ev) {
  bool ctrl = ev.mod & KMOD_CTRL, shift = ev.mod & KMOD_SHIFT;

  if (ctrl) {
    switch (ev.sc) {
      case SDL_SCANCODE_UP: transpose(shift ? 12 : 1); return;
      case SDL_SCANCODE_DOWN: transpose(shift ? -12 : -1); return;
      default: break;
    }
    switch (ev.key) {
      case SDLK_c: copySelection(false); return;
      case SDLK_x: copySelection(true); return;
      case SDLK_v: paste(); return;
      case SDLK_a: {
        bool wholeChannel = hasSel_ && selCh0_ == selCh1_ && selRow0_ == 0 && selRow1_ == song_.patternLength - 1 &&
                            colToField(std::min(selCol0_, selCol1_)) == 0;
        hasSel_ = true;
        selRow0_ = 0;
        selRow1_ = song_.patternLength - 1;
        selCh0_ = wholeChannel ? 0 : curCh_;
        selCh1_ = wholeChannel ? song_.channelCount() - 1 : curCh_;
        selCol0_ = 0;
        selCol1_ = colCount(selCh1_) - 1;
        return;
      }
      default: break;
    }
    globalShortcut(ev);
    return;
  }

  // Navigation (Shift extends the selection).
  int dRow = 0, dCol = 0;
  bool nav = true;
  switch (ev.sc) {
    case SDL_SCANCODE_UP: dRow = -1; break;
    case SDL_SCANCODE_DOWN: dRow = 1; break;
    case SDL_SCANCODE_LEFT: dCol = -1; break;
    case SDL_SCANCODE_RIGHT: dCol = 1; break;
    case SDL_SCANCODE_PAGEUP: dRow = -16; break;
    case SDL_SCANCODE_PAGEDOWN: dRow = 16; break;
    case SDL_SCANCODE_HOME: dRow = -curRow_; break;
    case SDL_SCANCODE_END: dRow = song_.patternLength - 1 - curRow_; break;
    default: nav = false; break;
  }
  if (nav) {
    if (shift) startSelection();
    else hasSel_ = false;
    moveCursor(dRow, dCol);
    if (shift) {
      selCh1_ = curCh_;
      selCol1_ = curCol_;
      selRow1_ = curRow_;
    }
    return;
  }

  switch (ev.sc) {
    case SDL_SCANCODE_TAB:
      if (shift) curCh_ = std::max(0, curCh_ - 1);
      else curCh_ = std::min(song_.channelCount() - 1, curCh_ + 1);
      curCol_ = 0;
      return;
    case SDL_SCANCODE_SPACE: editMode_ = !editMode_; return;
    case SDL_SCANCODE_ESCAPE: hasSel_ = false; return;
    case SDL_SCANCODE_DELETE: deleteAtCursor(); return;
    case SDL_SCANCODE_BACKSPACE: insertRow(false); return;
    case SDL_SCANCODE_INSERT: insertRow(true); return;
    default: break;
  }

  if (curCol_ == 0) {
    if (ev.sc == SDL_SCANCODE_1 || ev.sc == SDL_SCANCODE_GRAVE) {
      if (!ev.repeat) {
        enterNote(NOTE_OFF);
        engine_->noteOff(curCh_);
      }
      return;
    }
    int n = scancodeToNote(ev.sc);
    if (n >= 0) {
      if (ev.repeat) return;
      int note = std::clamp(octave_ * 12 + n, 0, 119);
      engine_->noteOn(curCh_, note, curIns_);
      jamming_[ev.sc] = curCh_;
      enterNote(note);
      return;
    }
  } else {
    int h = scancodeToHex(ev);
    if (h >= 0) {
      enterHex(h);
      return;
    }
    if (scancodeToNote(ev.sc) >= 0) return;
  }
  globalShortcut(ev);
}

// ---------------------------------------------------------------------------
// Drawing
// ---------------------------------------------------------------------------

void App::patternWindow() {
  bool open = ImGui::Begin("Pattern", &showPattern_);
  patternDockId_ = ImGui::GetWindowDockID();
  if (!open) {
    patternFocused_ = false;
    ImGui::End();
    return;
  }
  // Keep the cursor valid if the song changed under it.
  curOrder_ = std::clamp(curOrder_, 0, (int)song_.orders.size() - 1);
  curRow_ = std::clamp(curRow_, 0, song_.patternLength - 1);
  curCh_ = std::clamp(curCh_, 0, song_.channelCount() - 1);
  curCol_ = std::clamp(curCol_, 0, colCount(curCh_) - 1);
  curIns_ = std::clamp(curIns_, 0, std::max(0, (int)song_.instruments.size() - 1));

  // Toolbar.
  ImGui::SetNextItemWidth(80);
  ImGui::InputInt("Octave", &octave_);
  octave_ = std::clamp(octave_, 0, 8);
  ImGui::SameLine();
  ImGui::SetNextItemWidth(80);
  ImGui::InputInt("Step", &editStep_);
  editStep_ = std::clamp(editStep_, 0, 64);
  ImGui::SameLine();
  ImGui::SetNextItemWidth(200);
  char insLabel[128];
  std::snprintf(insLabel, sizeof(insLabel), "%02X: %s", curIns_,
                song_.instruments.empty() ? "" : song_.instruments[curIns_].name.c_str());
  if (ImGui::BeginCombo("Instrument", insLabel)) {
    for (int i = 0; i < (int)song_.instruments.size(); i++) {
      char buf[128];
      std::snprintf(buf, sizeof(buf), "%02X: %s", i, song_.instruments[i].name.c_str());
      if (ImGui::Selectable(buf, i == curIns_)) curIns_ = i;
    }
    ImGui::EndCombo();
  }
  ImGui::SameLine();
  ImGui::Checkbox("Record", &editMode_);
  ImGui::SameLine();
  ImGui::TextDisabled("Order %02X / pattern %02X", curOrder_, song_.orders[curOrder_][curCh_]);

  ImGuiIO& io = ImGui::GetIO();
  const Theme& T = theme();
  ImDrawList* dl = ImGui::GetWindowDrawList();
  float cw = ImGui::CalcTextSize("0").x;
  float rh = ImGui::GetTextLineHeight() + 2.0f;
  ImVec2 origin = ImGui::GetCursorScreenPos();
  ImVec2 avail = ImGui::GetContentRegionAvail();
  if (avail.x < 50 || avail.y < rh * 3) {
    patternFocused_ = ImGui::IsWindowFocused(ImGuiFocusedFlags_ChildWindows);
    ImGui::End();
    return;
  }

  ImGui::InvisibleButton("grid", avail, ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight);
  bool hovered = ImGui::IsItemHovered();
  patternFocused_ = ImGui::IsWindowFocused(ImGuiFocusedFlags_ChildWindows);

  float rowNumW = cw * 3.5f;
  float headerH = rh * 1.3f;
  float gridTop = origin.y + headerH;
  int visibleRows = std::max(1, (int)((avail.y - headerH) / rh));
  int centerLine = visibleRows / 2;
  float chPad = cw;

  // Horizontal scroll so the cursor channel is visible.
  int nch = song_.channelCount();
  firstVisibleCh_ = std::clamp(firstVisibleCh_, 0, nch - 1);
  if (curCh_ < firstVisibleCh_) firstVisibleCh_ = curCh_;
  auto chWidth = [&](int c) { return channelChars(song_.channels[c].effectCols) * cw + chPad; };
  while (firstVisibleCh_ < curCh_) {
    float x = rowNumW;
    for (int c = firstVisibleCh_; c <= curCh_; c++) x += chWidth(c);
    if (x <= avail.x) break;
    firstVisibleCh_++;
  }
  std::vector<float> chX;
  std::vector<int> chIdx;
  {
    float x = origin.x + rowNumW;
    for (int c = firstVisibleCh_; c < nch && x < origin.x + avail.x; c++) {
      chX.push_back(x);
      chIdx.push_back(c);
      x += chWidth(c);
    }
  }

  dl->PushClipRect(origin, ImVec2(origin.x + avail.x, origin.y + avail.y), true);
  dl->AddRectFilled(origin, ImVec2(origin.x + avail.x, origin.y + avail.y), T.patBg);

  // Channel headers: click to mute, right-click to solo.
  for (size_t i = 0; i < chIdx.size(); i++) {
    int c = chIdx[i];
    ChannelInfo& info = song_.channels[c];
    ImVec2 a(chX[i], origin.y), b(chX[i] + chWidth(c) - chPad * 0.5f, origin.y + headerH - 2);
    bool hov = hovered && io.MousePos.x >= a.x && io.MousePos.x < b.x && io.MousePos.y >= a.y && io.MousePos.y < b.y;
    dl->AddRectFilled(a, b, info.muted ? T.headerMuted : hov ? T.headerHover : T.header, 3);
    // Level meter from the scope buffer.
    const ChannelState& st = engine_->channel(c);
    float peak = 0;
    for (int k = 0; k < SCOPE_LEN; k += 8) peak = std::max(peak, std::abs(st.scope[k]));
    dl->AddRectFilled(ImVec2(a.x, b.y - 3), ImVec2(a.x + (b.x - a.x) * std::min(peak * 1.5f, 1.0f), b.y), IM_COL32(100, 220, 120, 200));
    std::string label = info.muted ? info.name + " (M)" : info.name;
    dl->PushClipRect(a, b, true);
    dl->AddText(ImVec2(a.x + 4, a.y + 2), info.muted ? IM_COL32(200, 120, 120, 255) : T.headerText, label.c_str());
    dl->PopClipRect();
    if (hov && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) info.muted = !info.muted;
    if (hov && ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
      bool soloed = !info.muted;
      for (int o = 0; o < nch; o++)
        if (o != c && !song_.channels[o].muted) soloed = false;
      for (int o = 0; o < nch; o++) song_.channels[o].muted = soloed ? false : (o != c);
    }
  }

  bool playing = engine_->playing();
  int playOrder = engine_->order(), playRow = engine_->row();
  int sch0 = 0, sf0 = 0, srow0 = 0, sch1 = 0, sf1 = 0, srow1 = 0;
  if (hasSel_) selectionBounds(sch0, sf0, srow0, sch1, sf1, srow1);

  float rightEdge = chX.empty() ? origin.x + avail.x : chX.back() + chWidth(chIdx.back());
  for (int line = 0; line < visibleRows; line++) {
    int row = curRow_ + line - centerLine;
    if (row < 0 || row >= song_.patternLength) continue;
    float y = gridTop + line * rh;
    ImVec2 ra(origin.x, y), rb(rightEdge, y + rh);

    if (song_.highlight2 > 0 && row % song_.highlight2 == 0)
      dl->AddRectFilled(ra, rb, T.rowHi2);
    else if (song_.highlight1 > 0 && row % song_.highlight1 == 0)
      dl->AddRectFilled(ra, rb, T.rowHi1);
    if (playing && playOrder == curOrder_ && playRow == row) dl->AddRectFilled(ra, rb, T.playRow);
    if (row == curRow_) dl->AddRectFilled(ra, rb, editMode_ ? T.editRow : T.cursorRow);

    char buf[16];
    std::snprintf(buf, sizeof(buf), "%02X", row);
    bool hiRow = song_.highlight1 > 0 && row % song_.highlight1 == 0;
    dl->AddText(ImVec2(origin.x + cw * 0.5f, y + 1), hiRow ? T.rowNumHi : T.rowNum, buf);

    for (size_t i = 0; i < chIdx.size(); i++) {
      int c = chIdx[i];
      float x = chX[i];
      const ChannelInfo& info = song_.channels[c];
      const Cell* cell = song_.cell(c, curOrder_, row);
      static const Cell emptyCell;
      const Cell& cl = cell ? *cell : emptyCell;

      // Selection highlight.
      if (hasSel_ && c >= sch0 && c <= sch1 && row >= srow0 && row <= srow1) {
        int maxField = 2 + info.effectCols;
        int f0 = c == sch0 ? sf0 : 0, f1 = c == sch1 ? std::min(sf1, maxField) : maxField;
        auto fieldX0 = [&](int f) { return f == 0 ? 0 : f == 1 ? 4 : f == 2 ? 7 : 10 + 5 * (f - 3); };
        auto fieldX1 = [&](int f) { return f == 0 ? 3 : f == 1 ? 6 : f == 2 ? 9 : 14 + 5 * (f - 3); };
        if (f0 <= f1)
          dl->AddRectFilled(ImVec2(x + fieldX0(f0) * cw - 1, y), ImVec2(x + fieldX1(f1) * cw + 1, y + rh), T.selection);
      }

      std::string nn = cl.note == NOTE_EMPTY ? "..." : noteName(cl.note);
      dl->AddText(ImVec2(x, y + 1), cl.note == NOTE_EMPTY ? T.empty : cl.note == NOTE_OFF ? T.noteOff : T.note, nn.c_str());
      hex2(buf, cl.ins);
      dl->AddText(ImVec2(x + 4 * cw, y + 1), cl.ins < 0 ? T.empty : T.ins, buf);
      hex2(buf, cl.vol);
      dl->AddText(ImVec2(x + 7 * cw, y + 1), cl.vol < 0 ? T.empty : T.vol, buf);
      for (int e = 0; e < info.effectCols; e++) {
        hex2(buf, cl.fx[e].cmd);
        dl->AddText(ImVec2(x + (10 + 5 * e) * cw, y + 1), cl.fx[e].cmd < 0 ? T.empty : T.fxCmd, buf);
        hex2(buf, cl.fx[e].val);
        dl->AddText(ImVec2(x + (12 + 5 * e) * cw, y + 1), cl.fx[e].val < 0 ? T.empty : T.fxVal, buf);
      }
      if (c == curCh_ && row == curRow_) {
        float cx = x + subColChar(curCol_) * cw;
        dl->AddRect(ImVec2(cx - 1, y), ImVec2(cx + subColWidth(curCol_) * cw + 1, y + rh), T.cursorBox, 0, 0, 1.5f);
      }
      // Channel separator.
      dl->AddLine(ImVec2(x + chWidth(c) - chPad * 0.5f, y), ImVec2(x + chWidth(c) - chPad * 0.5f, y + rh), T.separator);
    }
  }
  dl->PopClipRect();

  // Mouse: click to move the cursor, drag to select, wheel to scroll.
  auto cellAt = [&](ImVec2 p, int& ch, int& col, int& row) {
    if (p.y < gridTop || chIdx.empty()) return false;
    row = curRow_ + (int)((p.y - gridTop) / rh) - centerLine;
    if (row < 0 || row >= song_.patternLength) return false;
    for (size_t i = 0; i < chIdx.size(); i++) {
      int c = chIdx[i];
      if (p.x >= chX[i] && p.x < chX[i] + chWidth(c)) {
        ch = c;
        col = charToSubCol((int)((p.x - chX[i]) / cw), colCount(c));
        return true;
      }
    }
    return false;
  };
  int mch, mcol, mrow;
  if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left) && cellAt(io.MousePos, mch, mcol, mrow)) {
    if (io.KeyShift) {
      startSelection();
    } else {
      hasSel_ = false;
      selCh0_ = mch;
      selCol0_ = mcol;
      selRow0_ = mrow;
    }
    curCh_ = mch;
    curCol_ = mcol;
    curRow_ = mrow;
    mouseSelecting_ = true;
    if (io.KeyShift) {
      selCh1_ = mch;
      selCol1_ = mcol;
      selRow1_ = mrow;
    }
  }
  if (mouseSelecting_ && ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
    if (cellAt(io.MousePos, mch, mcol, mrow) && (mch != selCh0_ || mcol != selCol0_ || mrow != selRow0_)) {
      hasSel_ = true;
      selCh1_ = mch;
      selCol1_ = mcol;
      selRow1_ = mrow;
    }
  } else {
    mouseSelecting_ = false;
  }
  if (hovered && io.MouseWheel != 0 && !(playing && follow_)) moveRows(-(int)io.MouseWheel * 4);

  ImGui::End();
}
