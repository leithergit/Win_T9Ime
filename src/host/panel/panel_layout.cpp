#include "panel_layout.h"

#include <algorithm>
#include <cmath>

namespace t9ime::panel {

namespace {

constexpr const wchar_t* kChineseKeys[9] = {L"分词", L"ABC", L"DEF", L"GHI", L"JKL",
                                            L"MNO", L"PQRS", L"TUV", L"WXYZ"};
constexpr const wchar_t* kEnglishKeys[9] = {L".,?!", L"abc", L"def", L"ghi", L"jkl",
                                            L"mno", L"pqrs", L"tuv", L"wxyz"};

RectF Inset(RectF r, float d) { return {r.x + d, r.y + d, r.w - 2 * d, r.h - 2 * d}; }

std::string Utf8(const std::wstring& w) {
  std::string out;
  for (wchar_t c : w) {  // only ASCII keys go through here
    out.push_back(static_cast<char>(c));
  }
  return out;
}

struct Grid {
  float side_w, key_w, func_w, top, row_h;
  RectF Cell(int col, int row) const {  // col: 0 = side, 1..3 = keys, 4 = function
    const float x = col == 0 ? 0 : col <= 3 ? side_w + (col - 1) * key_w : side_w + 3 * key_w;
    const float w = col == 0 ? side_w : col <= 3 ? key_w : func_w;
    return Inset({x, top + row * row_h, w, row_h}, Metrics::kGap);
  }
};

}  // namespace

Layout BuildLayout(const LayoutInput& in) {
  Layout out;
  auto add = [&](Action a, RectF r, std::wstring label, std::wstring sub = {}, std::string text = {},
                 bool function_key = false) -> Element& {
    Element e;
    e.action = a;
    e.rect = r;
    e.label = std::move(label);
    e.sublabel = std::move(sub);
    e.text = std::move(text);
    e.function_key = function_key;
    out.elements.push_back(std::move(e));
    return out.elements.back();
  };

  // Handle strip (drag the window; shows the preedit).
  out.handle = {0, 0, in.width, Metrics::kHandle};
  add(Action::kHandle, out.handle, in.preedit);

  // Candidate bar.
  const float bar_y = Metrics::kHandle;
  const bool nine_key = in.mode == Mode::kChinese || in.mode == Mode::kEnglish;
  const bool show_expand = nine_key && !in.candidates.empty();
  const RectF bar{0, bar_y, in.width - (show_expand ? Metrics::kExpandButton : 0), Metrics::kCandidateBar};
  if (nine_key && !in.expanded) {
    out.regions.push_back({Region::kCandidateBar, bar, true});
    float x = 0;
    for (size_t i = 0; i < in.candidates.size(); ++i) {
      const float w = (in.measure ? in.measure(in.candidates[i]) : 16.f * in.candidates[i].size()) +
                      2 * Metrics::kCandidatePad;
      const RectF r{bar.x + x - in.candidate_scroll, bar.y, w, bar.h};
      if (r.Right() > bar.x && r.x < bar.Right()) {
        Element& e = add(Action::kCandidate, r, in.candidates[i]);
        e.index = static_cast<int>(i);
        e.region = Region::kCandidateBar;
        e.selected = static_cast<int>(i) == in.highlighted;
      }
      x += w;
    }
    out.candidate_content = x;
  }
  if (show_expand) {
    add(Action::kExpand, Inset({in.width - Metrics::kExpandButton, bar_y, Metrics::kExpandButton,
                                Metrics::kCandidateBar}, Metrics::kGap),
        in.expanded ? L"▲" : L"▼", {}, {}, true);
  }

  // Body grid: 4 rows.
  const float top = bar_y + Metrics::kCandidateBar;
  Grid g;
  g.side_w = in.width * Metrics::kSideRatio;
  g.func_w = in.width * Metrics::kFunctionRatio;
  g.key_w = (in.width - g.side_w - g.func_w) / 3;
  g.top = top;
  g.row_h = std::max(0.f, (in.height - top) / 4);

  // Side list (rows 0-2): pinyin bar, quick punctuation or symbol categories.
  const bool expanded = nine_key && in.expanded;
  if (!expanded) {
    const RectF side{0, top, g.side_w, 3 * g.row_h};
    out.regions.push_back({Region::kSideList, side, false});
    Action side_action = Action::kText;
    if (in.mode == Mode::kChinese && in.composing) side_action = Action::kPinyin;
    if (in.mode == Mode::kSymbol) side_action = Action::kSymbolCategory;
    for (size_t i = 0; i < in.side_items.size(); ++i) {
      const RectF r{side.x, side.y + i * Metrics::kSideItem - in.side_scroll, side.w, Metrics::kSideItem};
      if (r.Bottom() > side.y && r.y < side.Bottom()) {
        Element& e = add(side_action, Inset(r, 1), in.side_items[i]);
        e.index = static_cast<int>(i);
        e.region = Region::kSideList;
        e.selected = static_cast<int>(i) == in.side_selected;
      }
    }
    out.side_content = in.side_items.size() * Metrics::kSideItem;
  }

  // Scrollable grid: expanded candidates, or the symbol table.
  auto flow_grid = [&](const RectF& area, const std::vector<std::wstring>& items, Action action,
                       bool measure_width) {
    out.regions.push_back({Region::kGrid, area, false});
    const int columns = 4;
    const float min_w = area.w / columns;
    float x = 0, y = 0;
    for (size_t i = 0; i < items.size(); ++i) {
      float w = min_w;
      if (measure_width && in.measure) {
        const float natural = in.measure(items[i]) + 2 * Metrics::kCandidatePad;
        w = std::min(area.w, std::ceil(natural / min_w) * min_w);
      }
      if (x > 0 && x + w > area.w + 0.5f) {
        x = 0;
        y += Metrics::kGridCell;
      }
      const RectF r{area.x + x, area.y + y - in.grid_scroll, w, Metrics::kGridCell};
      if (r.Bottom() > area.y && r.y < area.Bottom()) {
        Element& e = add(action, Inset(r, 1), items[i]);
        e.index = static_cast<int>(i);
        e.region = Region::kGrid;
        e.selected = action == Action::kCandidate && static_cast<int>(i) == in.highlighted;
      }
      x += w;
    }
    out.grid_content = items.empty() ? 0 : y + Metrics::kGridCell;
  };

  if (expanded) {
    flow_grid({0, top, in.width, 3 * g.row_h}, in.candidates, Action::kCandidate, true);
  } else if (in.mode == Mode::kSymbol) {
    const RectF area{g.side_w, top, 3 * g.key_w + g.func_w, 3 * g.row_h};
    flow_grid(area, in.symbols, Action::kText, false);
  } else if (in.mode == Mode::kNumber) {
    for (int k = 0; k < 9; ++k) {
      const std::wstring digit(1, static_cast<wchar_t>(L'1' + k));
      add(Action::kText, g.Cell(1 + k % 3, k / 3), digit, {}, Utf8(digit));
    }
    add(Action::kBackspace, g.Cell(4, 0), L"⌫", {}, {}, true);
    add(Action::kSpace, g.Cell(4, 1), L"空格", {}, {}, true);
    add(Action::kText, g.Cell(4, 2), L"@", {}, "@", true);
  } else {  // nine-key Chinese / English
    const bool chinese = in.mode == Mode::kChinese;
    for (int k = 0; k < 9; ++k) {
      const std::wstring digit(1, static_cast<wchar_t>(L'1' + k));
      add(Action::kKey, g.Cell(1 + k % 3, k / 3), chinese ? kChineseKeys[k] : kEnglishKeys[k], digit,
          Utf8(digit));
    }
    add(Action::kBackspace, g.Cell(4, 0), L"⌫", {}, {}, true);
    add(Action::kClear, g.Cell(4, 1), L"重输", {}, {}, true);
    add(Action::kSpace, g.Cell(4, 2), L"空格", L"0", "0", true);
  }

  // Bottom row.
  if (in.mode == Mode::kNumber) {
    add(Action::kBack, g.Cell(0, 3), L"返回", {}, {}, true);
    add(Action::kText, g.Cell(1, 3), L".", {}, ".");
    add(Action::kText, g.Cell(2, 3), L"0", {}, "0");
    add(Action::kText, g.Cell(3, 3), L",", {}, ",");
  } else if (in.mode == Mode::kSymbol) {
    add(Action::kBack, g.Cell(0, 3), L"返回", {}, {}, true);
    add(Action::kNumbers, g.Cell(1, 3), L"123", {}, {}, true);
    add(Action::kSpace, g.Cell(2, 3), L"空格", {}, {}, true);
    add(Action::kBackspace, g.Cell(3, 3), L"⌫", {}, {}, true);
  } else {
    add(Action::kSymbols, g.Cell(0, 3), L"符号", {}, {}, true);
    add(Action::kNumbers, g.Cell(1, 3), L"123", {}, {}, true);
    add(Action::kToggleLanguage, g.Cell(2, 3), in.mode == Mode::kChinese ? L"中" : L"英", L"中/英", {},
        true);
    add(Action::kHide, g.Cell(3, 3), L"⌨▾", {}, {}, true);
  }
  add(Action::kEnter, g.Cell(4, 3), L"回车", {}, {}, true);
  return out;
}

const Element* HitTest(const Layout& layout, float x, float y) {
  for (auto it = layout.elements.rbegin(); it != layout.elements.rend(); ++it) {
    if (!it->rect.Contains(x, y)) continue;
    if (it->region != Region::kNone) {
      bool inside = false;
      for (const RegionRect& r : layout.regions) {
        if (r.region == it->region && r.rect.Contains(x, y)) inside = true;
      }
      if (!inside) continue;
    }
    return &*it;
  }
  return nullptr;
}

Region RegionAt(const Layout& layout, float x, float y) {
  for (const RegionRect& r : layout.regions) {
    if (r.rect.Contains(x, y)) return r.region;
  }
  return Region::kNone;
}

}  // namespace t9ime::panel
