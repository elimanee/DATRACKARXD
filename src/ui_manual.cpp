// Built-in manual: README.md (French) and docs/manual_en.md, embedded at
// build time, shown with a small Markdown renderer (headings, paragraphs,
// lists, tables, code blocks).
#include <imgui.h>

#include <algorithm>
#include <cctype>
#include <cstring>
#include <string>
#include <vector>

#include "app.h"
#include "i18n.h"
#include "theme.h"

extern const unsigned char MANUAL_FR[];
extern const unsigned long MANUAL_FR_size;
extern const unsigned char MANUAL_EN[];
extern const unsigned long MANUAL_EN_size;

namespace {

struct Section {
  std::string title;
  std::vector<std::string> lines;
  std::string lower;  // for the search
};

std::string lowerCase(std::string s) {
  for (char& c : s) c = (char)std::tolower((unsigned char)c);
  return s;
}

// The font only has Latin-1: a few typographic characters become ASCII.
std::string latin1Only(const std::string& in) {
  static const std::pair<const char*, const char*> MAP[] = {
      {"\xE2\x80\xA6", "..."}, {"\xE2\x86\x92", "->"}, {"\xE2\x86\x90", "<-"}, {"\xE2\x80\x94", "-"},
      {"\xE2\x80\x93", "-"},   {"\xE2\x89\xA5", ">="}, {"\xE2\x89\xA4", "<="}, {"\xE2\x86\x91", "Up"},
      {"\xE2\x86\x93", "Down"}, {"\xE2\x97\x80", "<"},  {"\xE2\x96\xB6", ">"},  {"\xE2\x80\x99", "'"},
      {"\xE2\x80\x9C", "\""},  {"\xE2\x80\x9D", "\""}, {"\xC5\x93", "oe"}};
  std::string out;
  for (size_t i = 0; i < in.size();) {
    unsigned char c = (unsigned char)in[i];
    if (c < 0x80) {
      out += in[i++];
      continue;
    }
    bool done = false;
    for (const auto& m : MAP) {
      size_t n = std::strlen(m.first);
      if (in.compare(i, n, m.first) == 0) {
        out += m.second;
        i += n;
        done = true;
        break;
      }
    }
    if (done) continue;
    size_t len = c >= 0xF0 ? 4 : c >= 0xE0 ? 3 : 2;
    if (c >= 0xC2 && c <= 0xC3) out.append(in, i, 2);  // Latin-1 range
    else out += '?';
    i += len;
  }
  return out;
}

// Inline Markdown: **bold** and `code` lose their markers, [links](url)
// keep their text, images go.
std::string plain(const std::string& s) {
  std::string out;
  for (size_t i = 0; i < s.size(); i++) {
    if (s[i] == '[' || (s[i] == '!' && i + 1 < s.size() && s[i + 1] == '[')) {
      bool image = s[i] == '!';
      size_t open = image ? i + 1 : i;
      size_t close = s.find("](", open);
      size_t end = close == std::string::npos ? close : s.find(')', close);
      if (end != std::string::npos && s.find('\n', open) > end) {
        if (!image) out += plain(s.substr(open + 1, close - open - 1));
        i = end;
        continue;
      }
    }
    if (s.compare(i, 2, "**") == 0) {
      i++;
      continue;
    }
    if (s[i] == '`') continue;
    if (s[i] == '\\' && i + 1 < s.size() && std::strchr("|*`_\\", s[i + 1])) {
      out += s[++i];
      continue;
    }
    out += s[i];
  }
  return out;
}

std::vector<Section> parse(const unsigned char* data, unsigned long size) {
  std::string text = latin1Only(std::string((const char*)data, size));
  std::vector<Section> out(1);
  size_t pos = 0;
  bool code = false;
  while (pos <= text.size()) {
    size_t nl = text.find('\n', pos);
    if (nl == std::string::npos) nl = text.size();
    std::string line = text.substr(pos, nl - pos);
    if (!line.empty() && line.back() == '\r') line.pop_back();
    pos = nl + 1;
    if (line.rfind("```", 0) == 0) code = !code;
    if (!code && line.rfind("## ", 0) == 0) {
      out.push_back(Section{line.substr(3), {}, ""});
      continue;
    }
    if (!code && line.rfind("# ", 0) == 0 && out.size() == 1 && out[0].title.empty()) {
      out[0].title = line.substr(2);
      continue;
    }
    out.back().lines.push_back(line);
  }
  for (Section& s : out) {
    s.lower = lowerCase(s.title);
    for (const std::string& l : s.lines) s.lower += "\n" + lowerCase(l);
  }
  return out;
}

std::vector<std::string> tableCells(const std::string& line) {
  std::vector<std::string> cells;
  std::string cur;
  size_t start = line.find('|') + 1, end = line.rfind('|');
  for (size_t i = start; i < end; i++) {
    if (line[i] == '\\' && i + 1 < end && line[i + 1] == '|') {
      cur += "\\|";
      i++;
    } else if (line[i] == '|') {
      cells.push_back(cur);
      cur.clear();
    } else {
      cur += line[i];
    }
  }
  cells.push_back(cur);
  for (std::string& c : cells) {
    size_t a = c.find_first_not_of(' '), b = c.find_last_not_of(' ');
    c = a == std::string::npos ? "" : plain(c.substr(a, b - a + 1));
  }
  return cells;
}

bool isTableLine(const std::string& l) { return l.size() > 1 && l[0] == '|'; }

void renderSection(const Section& sec) {
  ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_CheckMark));
  ImGui::SetWindowFontScale(1.35f);
  ImGui::TextWrapped("%s", plain(sec.title).c_str());
  ImGui::SetWindowFontScale(1.0f);
  ImGui::PopStyleColor();
  ImGui::Separator();
  const std::vector<std::string>& L = sec.lines;
  std::string para;
  auto flush = [&] {
    if (!para.empty()) ImGui::TextWrapped("%s", plain(para).c_str());
    para.clear();
  };
  int tableId = 0;
  for (size_t i = 0; i < L.size(); i++) {
    const std::string& line = L[i];
    if (line.rfind("```", 0) == 0) {
      flush();
      ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.55f, 0.85f, 0.65f, 1));
      ImGui::Indent();
      for (i++; i < L.size() && L[i].rfind("```", 0) != 0; i++) ImGui::TextUnformatted(L[i].c_str());
      ImGui::Unindent();
      ImGui::PopStyleColor();
      continue;
    }
    if (line.rfind("### ", 0) == 0) {
      flush();
      ImGui::Spacing();
      ImGui::SeparatorText(plain(line.substr(4)).c_str());
      continue;
    }
    if (isTableLine(line)) {
      flush();
      std::vector<std::vector<std::string>> rows;
      for (; i < L.size() && isTableLine(L[i]); i++) {
        if (L[i].find("---") != std::string::npos && L[i].find_first_not_of("|-: ") == std::string::npos) continue;
        rows.push_back(tableCells(L[i]));
      }
      i--;
      int cols = rows.empty() ? 0 : (int)rows[0].size();
      if (cols > 0 && cols <= 16 &&
          ImGui::BeginTable(("tbl" + std::to_string(tableId++)).c_str(), cols,
                            ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp)) {
        for (size_t r = 0; r < rows.size(); r++) {
          ImGui::TableNextRow(r == 0 ? ImGuiTableRowFlags_Headers : 0);
          for (int c = 0; c < cols; c++) {
            ImGui::TableSetColumnIndex(c);
            std::string cell = c < (int)rows[r].size() ? rows[r][c] : "";
            size_t esc;
            while ((esc = cell.find("\\|")) != std::string::npos) cell.replace(esc, 2, "|");
            ImGui::TextWrapped("%s", cell.c_str());
          }
        }
        ImGui::EndTable();
      }
      continue;
    }
    size_t indent = line.find_first_not_of(' ');
    if (indent == std::string::npos) {
      flush();
      ImGui::Spacing();
      continue;
    }
    std::string body = line.substr(indent);
    bool bullet = body.rfind("- ", 0) == 0 || body.rfind("* ", 0) == 0;
    size_t dot = body.find(". ");
    bool numbered = dot != std::string::npos && dot > 0 && dot <= 3 &&
                    std::all_of(body.begin(), body.begin() + (long)dot, [](char ch) { return std::isdigit((unsigned char)ch); });
    if (bullet || numbered) {
      flush();
      float step = ImGui::GetFontSize() * (1.0f + (float)(indent / 2));
      ImGui::Indent(step);
      if (bullet) {
        ImGui::Bullet();
        body = body.substr(2);
      } else {
        ImGui::TextUnformatted(body.substr(0, dot + 1).c_str());
        body = body.substr(dot + 2);
      }
      ImGui::SameLine();
      // The item's continuation lines (indented, no marker) belong to it.
      while (i + 1 < L.size()) {
        const std::string& next = L[i + 1];
        size_t ni = next.find_first_not_of(' ');
        if (ni == std::string::npos || ni == 0 || isTableLine(next)) break;
        std::string nb = next.substr(ni);
        size_t nd = nb.find(". ");
        bool nnum = nd != std::string::npos && nd > 0 && nd <= 3 &&
                    std::all_of(nb.begin(), nb.begin() + (long)nd, [](char ch) { return std::isdigit((unsigned char)ch); });
        if (nb.rfind("- ", 0) == 0 || nb.rfind("* ", 0) == 0 || nnum || nb.rfind("```", 0) == 0) break;
        body += " " + nb;
        i++;
      }
      ImGui::TextWrapped("%s", plain(body).c_str());
      ImGui::Unindent(step);
      continue;
    }
    para += para.empty() ? body : " " + body;
  }
  flush();
}

}  // namespace

void App::manualWindow() {
  ImGui::SetNextWindowSize(ImVec2(S(900), S(620)), ImGuiCond_FirstUseEver);
  if (!ImGui::Begin(TW("Manual").c_str(), &showManual_)) {
    ImGui::End();
    return;
  }
  static std::vector<Section> sections[2];
  int lang = language() == 1 ? 1 : 0;
  if (sections[lang].empty())
    sections[lang] = lang == 1 ? parse(MANUAL_FR, MANUAL_FR_size) : parse(MANUAL_EN, MANUAL_EN_size);
  const std::vector<Section>& secs = sections[lang];
  static int current = 0;
  static char search[128] = {};
  current = std::clamp(current, 0, (int)secs.size() - 1);

  ImGui::BeginChild("toc", ImVec2(S(230), 0), ImGuiChildFlags_Borders | ImGuiChildFlags_ResizeX);
  ImGui::SetNextItemWidth(-1);
  ImGui::InputTextWithHint("##search", T("Search..."), search, sizeof(search));
  std::string q = lowerCase(search);
  for (int i = 0; i < (int)secs.size(); i++) {
    if (!q.empty() && secs[i].lower.find(q) == std::string::npos) continue;
    std::string label = (secs[i].title.empty() ? std::string("DATRACKARXD") : plain(secs[i].title)) + "##s" + std::to_string(i);
    if (ImGui::Selectable(label.c_str(), i == current)) current = i;
  }
  ImGui::EndChild();
  ImGui::SameLine();
  ImGui::BeginChild("page", ImVec2(0, 0), ImGuiChildFlags_Borders);
  static int shown = -1;
  if (shown != current) ImGui::SetScrollY(0);
  shown = current;
  renderSection(secs[current]);
  ImGui::EndChild();
  ImGui::End();
}
