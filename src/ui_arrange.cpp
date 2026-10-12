// Arrangement view: the order list as a timeline (channels x orders), the
// subsongs, the loop range and the automation lanes.
#include <imgui.h>

#include <algorithm>
#include <cmath>
#include <cstdio>

#include "app.h"
#include "i18n.h"
#include "theme.h"

// ---------------------------------------------------------------------------
// Order list edits, shared with the Orders window. The automation follows.
// ---------------------------------------------------------------------------

int App::firstUnusedPattern(int c) const {
  int p = 0;
  while (p < MAX_PATTERNS - 1) {
    bool used = song_.pattern(c, p) != nullptr;
    for (auto& o : song_.orders) used |= o[c] == p;
    if (!used) break;
    p++;
  }
  return p;
}

void App::orderInsertNew(int at) {
  if ((int)song_.orders.size() >= MAX_ORDERS) return;
  at = std::clamp(at, 0, (int)song_.orders.size());
  std::array<uint8_t, MAX_CHANNELS> row{};
  for (int c = 0; c < song_.channelCount(); c++) row[c] = (uint8_t)firstUnusedPattern(c);
  song_.orders.insert(song_.orders.begin() + at, row);
  song_.insertOrderTime(at);
  curOrder_ = at;
  dirty_ = metaTouched_ = true;
}

void App::orderDuplicate(int first, int last) {
  first = std::clamp(first, 0, (int)song_.orders.size() - 1);
  last = std::clamp(last, first, (int)song_.orders.size() - 1);
  int n = last - first + 1;
  if ((int)song_.orders.size() + n > MAX_ORDERS) return;
  // The copied block goes right after the original, automation included.
  for (int i = 0; i < n; i++) {
    song_.orders.insert(song_.orders.begin() + last + 1 + i, song_.orders[first + i]);
    song_.insertOrderTime(last + 1 + i);
  }
  for (AutomationLane& lane : song_.automation) {
    std::vector<AutoPoint> add;
    for (const AutoPoint& p : lane.points)
      if (p.pos >= first && p.pos < last + 1) add.push_back({p.pos + n, p.value});
    lane.points.insert(lane.points.end(), add.begin(), add.end());
    lane.sort();
  }
  curOrder_ = last + 1;
  dirty_ = metaTouched_ = true;
}

void App::orderClone(int at) {
  if ((int)song_.orders.size() >= MAX_ORDERS) return;
  pushFullUndo();
  std::array<uint8_t, MAX_CHANNELS> row = song_.orders[at];
  for (int c = 0; c < song_.channelCount(); c++) {
    int p = firstUnusedPattern(c);
    const Pattern* src = song_.pattern(c, row[c]);
    if (src) *song_.pattern(c, p, true) = *src;
    row[c] = (uint8_t)p;
  }
  song_.orders.insert(song_.orders.begin() + at + 1, row);
  song_.insertOrderTime(at + 1);
  curOrder_ = at + 1;
  dirty_ = metaTouched_ = true;
}

void App::orderDelete(int first, int last) {
  first = std::clamp(first, 0, (int)song_.orders.size() - 1);
  last = std::clamp(last, first, (int)song_.orders.size() - 1);
  for (int o = last; o >= first && song_.orders.size() > 1; o--) {
    song_.orders.erase(song_.orders.begin() + o);
    song_.removeOrderTime(o);
  }
  curOrder_ = std::min(first, (int)song_.orders.size() - 1);
  dirty_ = metaTouched_ = true;
}

void App::orderSwap(int a, int b) {
  int n = (int)song_.orders.size();
  if (a < 0 || b < 0 || a >= n || b >= n || a == b) return;
  std::swap(song_.orders[a], song_.orders[b]);
  song_.swapOrderTime(a, b);
  dirty_ = metaTouched_ = true;
}

// ---------------------------------------------------------------------------
// Automation helpers
// ---------------------------------------------------------------------------

std::string App::laneLabel(const AutomationLane& lane) const {
  if (lane.isMaster()) return AUTO_TARGET_NAMES[lane.target];
  std::string ch = std::to_string(lane.channel + 1);
  if (lane.channel < song_.channelCount()) ch += " " + song_.channels[lane.channel].name;
  if (lane.target != AUTO_FX) return ch + ": " + AUTO_TARGET_NAMES[lane.target];
  const ChannelInfo* info = lane.channel < song_.channelCount() ? &song_.channels[lane.channel] : nullptr;
  if (info && lane.fxSlot < (int)info->effects.size()) {
    const ChanFxInfo& fx = CHAN_FX_INFO[info->effects[lane.fxSlot].type];
    const char* param = lane.fxParam < fx.count ? fx.params[lane.fxParam].name : "?";
    return ch + ": " + fx.name + " " + param;
  }
  return ch + ": effect " + std::to_string(lane.fxSlot + 1) + " (missing)";
}

std::string App::laneValueText(const AutomationLane& lane, float v) const {
  char buf[64];
  switch (lane.target) {
    case AUTO_VOLUME: std::snprintf(buf, sizeof(buf), "%.0f%%", v * 100); break;
    case AUTO_PAN: {
      int p = (int)std::lround(v * 255);
      if (p == 128) std::snprintf(buf, sizeof(buf), "center");
      else std::snprintf(buf, sizeof(buf), "%s %d", p < 128 ? "L" : "R", std::abs(p - 128));
      break;
    }
    case AUTO_CUTOFF: std::snprintf(buf, sizeof(buf), "cutoff %d", (int)std::lround(v * 255)); break;
    case AUTO_MASTER_VOLUME: std::snprintf(buf, sizeof(buf), "x%.2f", v * 2); break;
    case AUTO_MASTER_CUTOFF: std::snprintf(buf, sizeof(buf), "%.0f Hz", 20.0f * std::pow(1000.0f, v)); break;
    case AUTO_FX: {
      const ChannelInfo* info = lane.channel < song_.channelCount() ? &song_.channels[lane.channel] : nullptr;
      if (info && lane.fxSlot < (int)info->effects.size()) {
        const ChanFxInfo& fx = CHAN_FX_INFO[info->effects[lane.fxSlot].type];
        if (lane.fxParam < fx.count) {
          const ChanFxParam& prm = fx.params[lane.fxParam];
          float x = prm.log && prm.min > 0 ? prm.min * std::pow(prm.max / prm.min, v) : prm.min + (prm.max - prm.min) * v;
          std::snprintf(buf, sizeof(buf), prm.format ? prm.format : "%.0f", x);
          break;
        }
      }
      std::snprintf(buf, sizeof(buf), "%.2f", v);
      break;
    }
    default: std::snprintf(buf, sizeof(buf), "%.2f", v); break;
  }
  return buf;
}

// The setting's current (unautomated) value, so a new lane starts there.
float App::laneDefault(const AutomationLane& lane) const {
  const ChannelInfo* info = lane.channel < song_.channelCount() ? &song_.channels[lane.channel] : nullptr;
  switch (lane.target) {
    case AUTO_VOLUME: return 1;
    case AUTO_PAN: return info ? info->pan / 255.0f : 0.5f;
    case AUTO_CUTOFF: return 0.5f;
    case AUTO_REVERB: return info ? info->reverbSend : 0;
    case AUTO_DELAY: return info ? info->delaySend : 0;
    case AUTO_MASTER_VOLUME: return std::clamp(song_.fx.volume / 2, 0.0f, 1.0f);
    case AUTO_MASTER_CUTOFF: return std::clamp(std::log(song_.fx.cutoff / 20.0f) / std::log(1000.0f), 0.0f, 1.0f);
    case AUTO_FX:
      if (info && lane.fxSlot < (int)info->effects.size()) {
        const ChannelEffect& e = info->effects[lane.fxSlot];
        const ChanFxInfo& fx = CHAN_FX_INFO[e.type];
        if (lane.fxParam < fx.count) {
          const ChanFxParam& prm = fx.params[lane.fxParam];
          float x = e.p[lane.fxParam];
          float v = prm.log && prm.min > 0 ? std::log(x / prm.min) / std::log(prm.max / prm.min) : (x - prm.min) / (prm.max - prm.min);
          return std::clamp(v, 0.0f, 1.0f);
        }
      }
      return 0.5f;
    default: return 0.5f;
  }
}

// ---------------------------------------------------------------------------
// The window
// ---------------------------------------------------------------------------

void App::arrangementWindow() {
  if (!ImGui::Begin(TW("Arrangement").c_str(), &showArrangement_)) {
    ImGui::End();
    return;
  }
  ImGuiIO& io = ImGui::GetIO();
  int nch = song_.channelCount();

  // Subsongs.
  {
    char label[160];
    auto subLabel = [&](int i) {
      std::string n = song_.subsongNameAt(i);
      std::snprintf(label, sizeof(label), "%d: %s", i + 1, n.empty() ? "(untitled)" : n.c_str());
      return label;
    };
    ImGui::SetNextItemWidth(S(180));
    if (ImGui::BeginCombo("##subsong", subLabel(song_.curSubsong))) {
      for (int i = 0; i < song_.subsongCount(); i++)
        if (ImGui::Selectable(subLabel(i), i == song_.curSubsong) && i != song_.curSubsong) {
          pushFullUndo();
          engine_->stop();
          song_.selectSubsong(i);
          curOrder_ = curRow_ = 0;
          hasSel_ = false;
          arrSelA_ = arrSelB_ = 0;
          engine_->setLoopRange(-1, -1);
          loopRange_ = false;
          engine_->reset();
        }
      ImGui::EndCombo();
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip(T("Subsongs: each has its own orders, patterns, tempo and automation"));
    ImGui::SameLine();
    if (ImGui::Button("+##sub") && song_.subsongCount() < 256) {
      pushFullUndo();
      engine_->stop();
      int i = song_.addSubsong();
      song_.selectSubsong(i);
      song_.subsongName = "Subsong " + std::to_string(i + 1);
      curOrder_ = curRow_ = 0;
      arrSelA_ = arrSelB_ = 0;
      loopRange_ = false;
      engine_->setLoopRange(-1, -1);
      engine_->reset();
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip(T("New subsong"));
    ImGui::SameLine();
    ImGui::BeginDisabled(song_.subsongCount() <= 1);
    if (ImGui::Button("-##sub")) {
      pushFullUndo();
      engine_->stop();
      song_.removeSubsong(song_.curSubsong);
      curOrder_ = curRow_ = 0;
      arrSelA_ = arrSelB_ = 0;
      loopRange_ = false;
      engine_->setLoopRange(-1, -1);
      engine_->reset();
    }
    ImGui::EndDisabled();
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip(T("Delete this subsong (Ctrl+Z brings it back)"));
    ImGui::SameLine();
    char name[128];
    std::snprintf(name, sizeof(name), "%s", song_.subsongName.c_str());
    ImGui::SetNextItemWidth(S(150));
    if (ImGui::InputTextWithHint("##subname", "subsong name", name, sizeof(name))) {
      song_.subsongName = name;
      dirty_ = metaTouched_ = true;
    }
  }

  // Order tools: they act on the selected range of orders.
  int numOrders = (int)song_.orders.size();
  arrSelA_ = std::clamp(arrSelA_, 0, numOrders - 1);
  arrSelB_ = std::clamp(arrSelB_, 0, numOrders - 1);
  int selA = std::min(arrSelA_, arrSelB_), selB = std::max(arrSelA_, arrSelB_);
  ImGui::SameLine();
  ImGui::TextDisabled("|");
  ImGui::SameLine();
  if (ImGui::SmallButton(T("New"))) {
    orderInsertNew(selB + 1);
    arrSelA_ = arrSelB_ = curOrder_;
  }
  if (ImGui::IsItemHovered()) ImGui::SetTooltip(T("Insert an order with new empty patterns after the selection"));
  ImGui::SameLine();
  if (ImGui::SmallButton(T("Dup"))) {
    orderDuplicate(selA, selB);
    arrSelA_ = selB + 1;
    arrSelB_ = selB + 1 + (selB - selA);
  }
  if (ImGui::IsItemHovered()) ImGui::SetTooltip(T("Repeat the selected orders (same patterns) after them"));
  ImGui::SameLine();
  if (ImGui::SmallButton(T("Clone"))) {
    orderClone(selA);
    arrSelA_ = arrSelB_ = curOrder_;
  }
  if (ImGui::IsItemHovered()) ImGui::SetTooltip(T("Copy the first selected order into new patterns"));
  ImGui::SameLine();
  if (ImGui::SmallButton(T("Del")) && numOrders > 1) {
    orderDelete(selA, selB);
    arrSelA_ = arrSelB_ = curOrder_;
  }
  ImGui::SameLine();
  if (ImGui::ArrowButton("##arrl", ImGuiDir_Left) && selA > 0) {
    for (int o = selA; o <= selB; o++) orderSwap(o - 1, o);
    arrSelA_ = selA - 1;
    arrSelB_ = selB - 1;
  }
  ImGui::SameLine();
  if (ImGui::ArrowButton("##arrr", ImGuiDir_Right) && selB < numOrders - 1) {
    for (int o = selB; o >= selA; o--) orderSwap(o, o + 1);
    arrSelA_ = selA + 1;
    arrSelB_ = selB + 1;
  }
  ImGui::SameLine();
  ImGui::Checkbox(T("Loop"), &loopRange_);
  if (ImGui::IsItemHovered()) ImGui::SetTooltip(T("Playback loops over the selected orders"));
  numOrders = (int)song_.orders.size();
  selA = std::clamp(std::min(arrSelA_, arrSelB_), 0, numOrders - 1);
  selB = std::clamp(std::max(arrSelA_, arrSelB_), 0, numOrders - 1);
  engine_->setLoopRange(loopRange_ ? selA : -1, loopRange_ ? selB : -1);
  ImGui::SameLine();
  ImGui::SetNextItemWidth(S(90));
  ImGui::SliderFloat("##zoom", &arrZoom_, 0.25f, 8.0f, "zoom %.2f", ImGuiSliderFlags_Logarithmic);

  // Layout.
  const float leftW = S(130), headerH = S(18), rowH = S(22);
  float orderW = std::max(6.0f, song_.patternLength * arrZoom_);
  float avail = ImGui::GetContentRegionAvail().y;
  float autoH = std::max(120.0f, avail * 0.4f);
  float gridH = std::max(80.0f, std::min(headerH + nch * rowH + 20, avail - autoH));

  // Playing position, in orders.
  bool playing = engine_->playing() && !engine_->countingIn();
  float playPos = -1;
  if (playing)
    playPos = engine_->order() + (engine_->row() + engine_->currentTick() / (float)std::max(1, engine_->speed())) /
                                     (float)std::max(1, song_.patternLength);

  // --- Pattern grid ---
  ImGui::BeginChild("arrgrid", ImVec2(0, gridH), ImGuiChildFlags_Borders, ImGuiWindowFlags_HorizontalScrollbar);
  if (arrScrollReq_ >= 0) {
    ImGui::SetScrollX(arrScrollReq_);
    arrScrollReq_ = -1;
  }
  float contentW = leftW + numOrders * orderW + 60;
  float contentH = headerH + nch * rowH;
  ImVec2 base = ImGui::GetCursorScreenPos();
  ImVec2 wpos = ImGui::GetWindowPos();
  float scrollX = ImGui::GetScrollX();
  arrScrollX_ = scrollX;
  ImGui::InvisibleButton("##grid", ImVec2(contentW, contentH), ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight);
  bool hovered = ImGui::IsItemHovered();
  ImDrawList* dl = ImGui::GetWindowDrawList();
  float gx = base.x + leftW;  // x of order 0
  auto xOf = [&](float pos) { return gx + pos * orderW; };
  float mx = io.MousePos.x, my = io.MousePos.y;
  int mo = (int)std::floor((mx - gx) / orderW);
  int mc = (int)std::floor((my - base.y - headerH) / rowH);
  bool overNames = mx < wpos.x + leftW;
  bool overCell = hovered && !overNames && mo >= 0 && mo < numOrders && mc >= 0 && mc < nch;

  if (hovered && io.MouseWheel != 0) {
    if (io.KeyCtrl) {
      // Zoom around the mouse.
      float at = (mx - gx) / orderW;
      arrZoom_ = std::clamp(arrZoom_ * std::pow(1.25f, io.MouseWheel), 0.25f, 8.0f);
      float nw = std::max(6.0f, song_.patternLength * arrZoom_);
      arrScrollReq_ = std::max(0.0f, scrollX + at * (nw - orderW));
    } else if (io.KeyAlt && overCell) {
      int v = song_.orders[mo][mc] + (io.MouseWheel > 0 ? 1 : -1);
      song_.orders[mo][mc] = (uint8_t)std::clamp(v, 0, MAX_PATTERNS - 1);
      dirty_ = metaTouched_ = true;
    }
  }

  // Order columns.
  int o0 = std::max(0, (int)std::floor((scrollX) / orderW) - 1);
  int o1 = std::min(numOrders - 1, (int)std::ceil((scrollX + ImGui::GetWindowWidth()) / orderW) + 1);
  if (selA <= selB)
    dl->AddRectFilled(ImVec2(xOf((float)selA), base.y), ImVec2(xOf((float)selB + 1), base.y + contentH), IM_COL32(255, 255, 255, 22));
  for (int o = o0; o <= o1; o++) {
    float x0 = xOf((float)o), x1 = xOf((float)o + 1);
    char num[16];
    std::snprintf(num, sizeof(num), "%02X", o);
    bool inSel = o >= selA && o <= selB;
    dl->AddRectFilled(ImVec2(x0, base.y), ImVec2(x1, base.y + headerH), inSel ? IM_COL32(80, 80, 130, 255) : IM_COL32(35, 35, 50, 255));
    dl->AddText(ImVec2(x0 + 3, base.y + 2), IM_COL32(200, 200, 220, 255), num);
    dl->AddLine(ImVec2(x0, base.y), ImVec2(x0, base.y + contentH), IM_COL32(60, 60, 80, 255));
    for (int c = 0; c < nch; c++) {
      int pat = song_.orders[o][c];
      const Pattern* p = song_.pattern(c, pat);
      bool empty = !p || p->isEmpty();
      float y0 = base.y + headerH + c * rowH + 1, y1 = y0 + rowH - 2;
      ImU32 col = empty ? IM_COL32(28, 28, 38, 255) : (ImU32)ImColor::HSV(std::fmod(pat * 0.618f, 1.0f), 0.45f, 0.42f);
      dl->AddRectFilled(ImVec2(x0 + 1, y0), ImVec2(x1 - 1, y1), col, 3);
      if (!empty) {
        // Mini preview: one dash per note, by pitch.
        int lo = 999, hi = -1;
        for (int r = 0; r < song_.patternLength; r++)
          if (p->rows[r].note >= 0) {
            lo = std::min<int>(lo, p->rows[r].note);
            hi = std::max<int>(hi, p->rows[r].note);
          }
        float rw = orderW / song_.patternLength;
        for (int r = 0; r < song_.patternLength && hi >= 0; r++) {
          int n = p->rows[r].note;
          if (n < 0) continue;
          float t = hi > lo ? (n - lo) / (float)(hi - lo) : 0.5f;
          float y = y1 - 3 - t * (rowH - 8);
          dl->AddLine(ImVec2(x0 + 1 + r * rw, y), ImVec2(x0 + 1 + r * rw + std::max(2.0f, rw), y), IM_COL32(230, 240, 255, 200), 2);
        }
      }
      if (orderW > 22) {
        std::snprintf(num, sizeof(num), "%02X", pat);
        dl->AddText(ImVec2(x0 + 4, y0 + 3), empty ? IM_COL32(110, 110, 130, 255) : IM_COL32(255, 255, 255, 230), num);
      }
      if (o == curOrder_ && c == curCh_) dl->AddRect(ImVec2(x0 + 1, y0), ImVec2(x1 - 1, y1), IM_COL32(255, 230, 120, 255), 3, 0, 2);
    }
  }
  // Loop range and playhead.
  if (engine_->loopFirst() >= 0) {
    float x0 = xOf((float)engine_->loopFirst()), x1 = xOf((float)engine_->loopLast() + 1);
    dl->AddRectFilled(ImVec2(x0, base.y), ImVec2(x1, base.y + 4), IM_COL32(90, 200, 120, 255));
  }
  if (playPos >= 0) {
    float x = xOf(playPos);
    dl->AddLine(ImVec2(x, base.y), ImVec2(x, base.y + contentH), IM_COL32(120, 255, 150, 255), 2);
  }
  // Channel names, fixed on the left.
  dl->AddRectFilled(ImVec2(wpos.x, base.y), ImVec2(wpos.x + leftW, base.y + contentH), IM_COL32(22, 22, 32, 255));
  for (int c = 0; c < nch; c++) {
    float y0 = base.y + headerH + c * rowH;
    char buf[64];
    std::snprintf(buf, sizeof(buf), "%d %s", c + 1, song_.channels[c].name.c_str());
    ImU32 col = song_.channels[c].muted ? IM_COL32(120, 120, 120, 255) : c == curCh_ ? IM_COL32(255, 230, 120, 255) : IM_COL32(210, 210, 230, 255);
    dl->PushClipRect(ImVec2(wpos.x, y0), ImVec2(wpos.x + leftW - 4, y0 + rowH), true);
    dl->AddText(ImVec2(wpos.x + 6, y0 + 4), col, buf);
    dl->PopClipRect();
  }

  // Mouse on the grid.
  if (ImGui::IsItemActivated() && io.MouseClicked[0]) {
    if (overNames && mc >= 0 && mc < nch) {
      curCh_ = mc;
    } else if (mo >= 0 && mo < numOrders) {
      if (io.KeyShift) {
        arrSelB_ = mo;
      } else {
        arrSelA_ = arrSelB_ = mo;
      }
      curOrder_ = mo;
      if (mc >= 0 && mc < nch) curCh_ = mc;
      hasSel_ = false;
      arrDragging_ = true;
    }
  }
  if (arrDragging_ && ImGui::IsMouseDown(ImGuiMouseButton_Left) && mo >= 0) arrSelB_ = std::clamp(mo, 0, numOrders - 1);
  if (!ImGui::IsMouseDown(ImGuiMouseButton_Left)) arrDragging_ = false;
  if (overCell && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
    curOrder_ = mo;
    curCh_ = mc;
    showPattern_ = true;
    ImGui::SetWindowFocus("###Pattern");
  }
  if (overCell && ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
    arrMenuOrder_ = mo;
    arrMenuCh_ = mc;
    ImGui::OpenPopup(T("arrcell"));
  }
  if (overCell && !ImGui::IsPopupOpen("arrcell")) {
    const Pattern* p = song_.pattern(mc, song_.orders[mo][mc]);
    int notes = 0;
    if (p)
      for (int r = 0; r < song_.patternLength; r++) notes += p->rows[r].note >= 0;
    ImGui::SetTooltip(T("Order %02X, channel %d: pattern %02X (%d notes)\nClick: select, Shift: extend, double-click: edit\n"
                      "Right-click: pattern menu, Alt+wheel: pattern number, Ctrl+wheel: zoom"),
                      mo, mc + 1, song_.orders[mo][mc], notes);
  }
  if (ImGui::BeginPopup(T("arrcell"))) {
    int o = std::clamp(arrMenuOrder_, 0, (int)song_.orders.size() - 1), c = std::clamp(arrMenuCh_, 0, nch - 1);
    uint8_t& pat = song_.orders[o][c];
    ImGui::TextDisabled(T("Order %02X, channel %d, pattern %02X"), o, c + 1, pat);
    if (ImGui::MenuItem(T("Next pattern number")) && pat < MAX_PATTERNS - 1) {
      pat++;
      dirty_ = metaTouched_ = true;
    }
    if (ImGui::MenuItem(T("Previous pattern number")) && pat > 0) {
      pat--;
      dirty_ = metaTouched_ = true;
    }
    if (ImGui::MenuItem(T("New empty pattern"))) {
      pat = (uint8_t)firstUnusedPattern(c);
      dirty_ = metaTouched_ = true;
    }
    if (ImGui::MenuItem(T("Make unique (copy)"))) {
      pushFullUndo();
      int np = firstUnusedPattern(c);
      if (const Pattern* src = song_.pattern(c, pat)) *song_.pattern(c, np, true) = *src;
      pat = (uint8_t)np;
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip(T("Copy the pattern so editing it here leaves the other orders alone"));
    if (ImGui::MenuItem(T("Clear pattern"))) {
      pushUndo(c, c, o);
      if (Pattern* p = song_.pattern(c, pat, false)) p->rows.assign(MAX_ROWS, Cell());
      dirty_ = true;
    }
    if (ImGui::MenuItem(T("Edit in the pattern editor"))) {
      curOrder_ = o;
      curCh_ = c;
      showPattern_ = true;
      ImGui::SetWindowFocus("###Pattern");
    }
    ImGui::EndPopup();
  }
  ImGui::EndChild();

  automationEditor(orderW, leftW, playPos);
  ImGui::End();
}

// The selected automation lane, on the same time axis as the grid above.
void App::automationEditor(float orderW, float leftW, float playPos) {
  ImGuiIO& io = ImGui::GetIO();
  int nch = song_.channelCount();
  auto& lanes = song_.automation;
  autoLane_ = lanes.empty() ? -1 : std::clamp(autoLane_, 0, (int)lanes.size() - 1);

  ImGui::TextUnformatted(T("Automation"));
  ImGui::SameLine();
  ImGui::SetNextItemWidth(S(220));
  std::string cur = autoLane_ >= 0 ? laneLabel(lanes[autoLane_]) : std::string("(no lane)");
  if (ImGui::BeginCombo("##lane", cur.c_str())) {
    for (int i = 0; i < (int)lanes.size(); i++) {
      std::string l = laneLabel(lanes[i]) + (lanes[i].on ? "" : " (off)") + "##l" + std::to_string(i);
      if (ImGui::Selectable(l.c_str(), i == autoLane_)) autoLane_ = i;
    }
    ImGui::EndCombo();
  }
  ImGui::SameLine();
  if (ImGui::SmallButton(T("Add lane"))) ImGui::OpenPopup(T("addlane"));
  if (ImGui::BeginPopup(T("addlane"))) {
    AutomationLane& n = newLane_;
    ImGui::SetNextItemWidth(S(160));
    ImGui::Combo(T("Target"), &n.target, AUTO_TARGET_NAMES, AUTO_TARGET_COUNT);
    if (!n.isMaster()) {
      n.channel = std::clamp(n.channel, 0, nch - 1);
      char buf[64];
      std::snprintf(buf, sizeof(buf), "%d %s", n.channel + 1, song_.channels[n.channel].name.c_str());
      ImGui::SetNextItemWidth(S(160));
      if (ImGui::BeginCombo(T("Channel"), buf)) {
        for (int c = 0; c < nch; c++) {
          std::snprintf(buf, sizeof(buf), "%d %s", c + 1, song_.channels[c].name.c_str());
          if (ImGui::Selectable(buf, c == n.channel)) n.channel = c;
        }
        ImGui::EndCombo();
      }
    }
    bool canAdd = true;
    if (n.target == AUTO_FX) {
      const auto& effects = song_.channels[n.channel].effects;
      if (effects.empty()) {
        ImGui::TextDisabled(T("This channel has no insert effect (Channel FX)."));
        canAdd = false;
      } else {
        n.fxSlot = std::clamp(n.fxSlot, 0, (int)effects.size() - 1);
        const ChanFxInfo& fx = CHAN_FX_INFO[effects[n.fxSlot].type];
        char buf[64];
        std::snprintf(buf, sizeof(buf), "%d %s", n.fxSlot + 1, fx.name);
        ImGui::SetNextItemWidth(S(160));
        if (ImGui::BeginCombo(T("Effect"), buf)) {
          for (int k = 0; k < (int)effects.size(); k++) {
            std::snprintf(buf, sizeof(buf), "%d %s", k + 1, CHAN_FX_INFO[effects[k].type].name);
            if (ImGui::Selectable(buf, k == n.fxSlot)) n.fxSlot = k;
          }
          ImGui::EndCombo();
        }
        const ChanFxInfo& fx2 = CHAN_FX_INFO[effects[n.fxSlot].type];
        n.fxParam = std::clamp(n.fxParam, 0, fx2.count - 1);
        ImGui::SetNextItemWidth(S(160));
        if (ImGui::BeginCombo(T("Parameter"), fx2.params[n.fxParam].name)) {
          for (int i = 0; i < fx2.count; i++)
            if (ImGui::Selectable(fx2.params[i].name, i == n.fxParam)) n.fxParam = i;
          ImGui::EndCombo();
        }
      }
    }
    if (n.target == AUTO_CUTOFF) ImGui::TextDisabled(T("Moves the instrument filter (turn it on in the SID section)."));
    if (n.target == AUTO_MASTER_CUTOFF) ImGui::TextDisabled(T("Moves the master filter (turn it on in the Mixer)."));
    ImGui::BeginDisabled(!canAdd || lanes.size() >= 256);
    if (ImGui::Button(T("Add"))) {
      AutomationLane lane = n;
      lane.points.clear();
      lane.points.push_back({0.0f, laneDefault(lane)});
      lanes.push_back(lane);
      autoLane_ = (int)lanes.size() - 1;
      dirty_ = metaTouched_ = true;
      ImGui::CloseCurrentPopup();
    }
    ImGui::EndDisabled();
    ImGui::EndPopup();
  }
  if (autoLane_ >= 0) {
    AutomationLane& lane = lanes[autoLane_];
    ImGui::SameLine();
    if (ImGui::Checkbox(T("On##lane"), &lane.on)) dirty_ = metaTouched_ = true;
    ImGui::SameLine();
    if (ImGui::SmallButton(T("Clear"))) {
      lane.points.clear();
      dirty_ = metaTouched_ = true;
    }
    ImGui::SameLine();
    if (ImGui::SmallButton(T("Delete lane"))) {
      lanes.erase(lanes.begin() + autoLane_);
      autoLane_ = std::min(autoLane_, (int)lanes.size() - 1);
      dirty_ = metaTouched_ = true;
    }
  }
  ImGui::SameLine();
  ImGui::TextDisabled("(?)");
  if (ImGui::IsItemHovered())
    ImGui::SetTooltip(T("Click: add a point, drag: move it, right-click: delete it.\n"
                      "Points snap to rows (Alt: free). Shift+wheel: scroll, Ctrl+wheel: zoom.\n"
                      "The value holds before the first point and after the last one."));

  ImVec2 pos = ImGui::GetCursorScreenPos();
  ImVec2 size(std::max(ImGui::GetContentRegionAvail().x, 100.0f), std::max(ImGui::GetContentRegionAvail().y, 60.0f));
  ImGui::InvisibleButton("##auto", size, ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight);
  bool hovered = ImGui::IsItemHovered();
  ImDrawList* dl = ImGui::GetWindowDrawList();
  dl->PushClipRect(pos, ImVec2(pos.x + size.x, pos.y + size.y), true);
  dl->AddRectFilled(pos, ImVec2(pos.x + size.x, pos.y + size.y), IM_COL32(15, 15, 22, 255));
  // Same horizontal mapping as the grid (its window padding included).
  float gx = pos.x + leftW + ImGui::GetStyle().WindowPadding.x - arrScrollX_;
  auto xOf = [&](float p) { return gx + p * orderW; };
  float top = pos.y + 6, bottom = pos.y + size.y - 6;
  auto yOf = [&](float v) { return bottom - v * (bottom - top); };
  int numOrders = (int)song_.orders.size();
  for (int o = 0; o <= numOrders; o++) {
    float x = xOf((float)o);
    if (x < pos.x + leftW || x > pos.x + size.x) continue;
    dl->AddLine(ImVec2(x, pos.y), ImVec2(x, pos.y + size.y), IM_COL32(50, 50, 70, 255));
  }
  dl->AddLine(ImVec2(pos.x + leftW, yOf(0.5f)), ImVec2(pos.x + size.x, yOf(0.5f)), IM_COL32(40, 40, 55, 255));
  if (playPos >= 0) dl->AddLine(ImVec2(xOf(playPos), pos.y), ImVec2(xOf(playPos), pos.y + size.y), IM_COL32(120, 255, 150, 255), 2);

  if (hovered && io.MouseWheel != 0 && (io.KeyShift || io.KeyCtrl)) {
    if (io.KeyCtrl) {
      float at = (io.MousePos.x - gx) / orderW;
      arrZoom_ = std::clamp(arrZoom_ * std::pow(1.25f, io.MouseWheel), 0.25f, 8.0f);
      float nw = std::max(6.0f, song_.patternLength * arrZoom_);
      arrScrollReq_ = std::max(0.0f, arrScrollX_ + at * (nw - orderW));
    } else {
      arrScrollReq_ = std::max(0.0f, arrScrollX_ - io.MouseWheel * orderW);
    }
  }

  if (autoLane_ >= 0) {
    AutomationLane& lane = lanes[autoLane_];
    ImU32 col = lane.on ? IM_COL32(255, 170, 80, 255) : IM_COL32(140, 120, 100, 255);
    // The curve.
    if (!lane.points.empty()) {
      std::vector<ImVec2> pts;
      pts.push_back(ImVec2(pos.x + leftW, yOf(lane.points.front().value)));
      for (const AutoPoint& p : lane.points) pts.push_back(ImVec2(xOf(p.pos), yOf(p.value)));
      pts.push_back(ImVec2(pos.x + size.x, yOf(lane.points.back().value)));
      dl->AddPolyline(pts.data(), (int)pts.size(), col, 0, 2);
      for (const AutoPoint& p : lane.points) dl->AddCircleFilled(ImVec2(xOf(p.pos), yOf(p.value)), 4, col);
    }
    // Mouse: nearest point within 7 pixels.
    int near = -1;
    float best = 49;
    for (int i = 0; i < (int)lane.points.size(); i++) {
      float dx = xOf(lane.points[i].pos) - io.MousePos.x, dy = yOf(lane.points[i].value) - io.MousePos.y;
      if (dx * dx + dy * dy < best) {
        best = dx * dx + dy * dy;
        near = i;
      }
    }
    auto mousePoint = [&]() {
      float p = std::clamp((io.MousePos.x - gx) / orderW, 0.0f, (float)numOrders);
      if (!io.KeyAlt) p = std::round(p * song_.patternLength) / song_.patternLength;
      float v = std::clamp((bottom - io.MousePos.y) / (bottom - top), 0.0f, 1.0f);
      return AutoPoint{p, v};
    };
    bool inArea = io.MousePos.x > pos.x + leftW;
    if (ImGui::IsItemActivated() && io.MouseClicked[0] && inArea) {
      if (near >= 0) {
        autoDrag_ = near;
      } else {
        lane.points.push_back(mousePoint());
        lane.sort();
        AutoPoint mp = mousePoint();
        autoDrag_ = -1;
        for (int i = 0; i < (int)lane.points.size(); i++)
          if (lane.points[i].pos == mp.pos && lane.points[i].value == mp.value) autoDrag_ = i;
      }
      dirty_ = metaTouched_ = true;
    }
    if (ImGui::IsItemActive() && ImGui::IsMouseDown(ImGuiMouseButton_Left) && autoDrag_ >= 0 && autoDrag_ < (int)lane.points.size()) {
      AutoPoint mp = mousePoint();
      AutoPoint& p = lane.points[autoDrag_];
      if (p.pos != mp.pos || p.value != mp.value) {
        p = mp;
        // Keep the points sorted while the dragged one passes others.
        while (autoDrag_ > 0 && lane.points[autoDrag_ - 1].pos > lane.points[autoDrag_].pos) {
          std::swap(lane.points[autoDrag_ - 1], lane.points[autoDrag_]);
          autoDrag_--;
        }
        while (autoDrag_ + 1 < (int)lane.points.size() && lane.points[autoDrag_ + 1].pos < lane.points[autoDrag_].pos) {
          std::swap(lane.points[autoDrag_ + 1], lane.points[autoDrag_]);
          autoDrag_++;
        }
        dirty_ = metaTouched_ = true;
      }
    }
    if (!ImGui::IsMouseDown(ImGuiMouseButton_Left)) autoDrag_ = -1;
    if (hovered && near >= 0 && ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
      lane.points.erase(lane.points.begin() + near);
      dirty_ = metaTouched_ = true;
      near = -1;
    }
    if (hovered && inArea) {
      AutoPoint mp = near >= 0 ? lane.points[near] : mousePoint();
      int o = (int)mp.pos;
      int row = (int)std::lround((mp.pos - o) * song_.patternLength);
      ImGui::SetTooltip(T("%s\nOrder %02X row %02X: %s"), laneLabel(lane).c_str(), o, row, laneValueText(lane, mp.value).c_str());
    }
  } else {
    dl->AddText(ImVec2(pos.x + leftW + 8, pos.y + 8), IM_COL32(140, 140, 160, 255),
                "No automation lane. \"Add lane\" draws a volume, pan, cutoff, send, effect or master curve.");
  }
  // Labels on the left.
  dl->AddRectFilled(pos, ImVec2(pos.x + leftW, pos.y + size.y), IM_COL32(22, 22, 32, 255));
  if (autoLane_ >= 0) {
    const AutomationLane& lane = lanes[autoLane_];
    dl->AddText(ImVec2(pos.x + 6, top - 2), IM_COL32(170, 170, 190, 255), laneValueText(lane, 1).c_str());
    dl->AddText(ImVec2(pos.x + 6, bottom - 14), IM_COL32(170, 170, 190, 255), laneValueText(lane, 0).c_str());
  }
  dl->PopClipRect();
}
