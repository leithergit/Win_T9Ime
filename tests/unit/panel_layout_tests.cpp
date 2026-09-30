#include <doctest/doctest.h>

#include "panel_layout.h"

using namespace t9ime::panel;

namespace {
const Element* Find(const Layout& l, Action a, const std::wstring& label = {}) {
  for (const Element& e : l.elements) {
    if (e.action == a && (label.empty() || e.label == label)) return &e;
  }
  return nullptr;
}
float Measure(const std::wstring& s) { return 18.f * s.size(); }
}  // namespace

TEST_CASE("chinese layout has all keys inside the panel") {
  LayoutInput in;
  in.width = 400;
  in.height = 300;
  in.measure = Measure;
  Layout l = BuildLayout(in);
  int keys = 0;
  for (const Element& e : l.elements) {
    CHECK(e.rect.x >= 0);
    CHECK(e.rect.y >= 0);
    CHECK(e.rect.Right() <= in.width + 0.01f);
    CHECK(e.rect.Bottom() <= in.height + 0.01f);
    if (e.action == Action::kKey) ++keys;
  }
  CHECK(keys == 9);
  for (Action a : {Action::kBackspace, Action::kClear, Action::kSpace, Action::kEnter, Action::kSymbols,
                   Action::kNumbers, Action::kToggleLanguage, Action::kHide}) {
    CHECK(Find(l, a) != nullptr);
  }
  CHECK(Find(l, Action::kExpand) == nullptr);  // no candidates yet
  const Element* nine = Find(l, Action::kKey, L"WXYZ");
  REQUIRE(nine);
  CHECK(nine->text == "9");
  CHECK(HitTest(l, nine->rect.x + 5, nine->rect.y + 5) == nine);
}

TEST_CASE("candidate bar scrolls and clips") {
  LayoutInput in;
  in.measure = Measure;
  in.composing = true;
  for (int i = 0; i < 30; ++i) in.candidates.push_back(L"候选" + std::to_wstring(i));
  in.side_items = {L"zhong", L"xiong"};
  Layout l = BuildLayout(in);
  CHECK(Find(l, Action::kExpand) != nullptr);
  CHECK(l.candidate_content > in.width);
  const Element* first = Find(l, Action::kCandidate, L"候选0");
  REQUIRE(first);
  CHECK(first->region == Region::kCandidateBar);
  CHECK(Find(l, Action::kPinyin, L"zhong") != nullptr);

  in.candidate_scroll = 200;
  Layout scrolled = BuildLayout(in);
  CHECK(Find(scrolled, Action::kCandidate, L"候选0") == nullptr);  // scrolled out and culled
  // Items partly outside the region are not hit outside it.
  for (const Element& e : scrolled.elements) {
    if (e.action == Action::kCandidate && e.rect.Right() > scrolled.regions[0].rect.Right()) {
      CHECK(HitTest(scrolled, e.rect.Right() - 1, e.rect.y + 5) != &e);
    }
  }
}

TEST_CASE("expanded candidates replace the keys") {
  LayoutInput in;
  in.measure = Measure;
  in.composing = true;
  in.expanded = true;
  for (int i = 0; i < 40; ++i) in.candidates.push_back(L"词" + std::to_wstring(i));
  Layout l = BuildLayout(in);
  CHECK(Find(l, Action::kKey) == nullptr);
  CHECK(Find(l, Action::kEnter) != nullptr);
  CHECK(l.grid_content > 0);
  const Element* c = Find(l, Action::kCandidate, L"词0");
  REQUIRE(c);
  CHECK(c->region == Region::kGrid);
}

TEST_CASE("number and symbol layouts") {
  LayoutInput in;
  in.measure = Measure;
  in.mode = Mode::kNumber;
  Layout n = BuildLayout(in);
  CHECK(Find(n, Action::kKey) == nullptr);
  CHECK(Find(n, Action::kText, L"7") != nullptr);
  CHECK(Find(n, Action::kText, L"0") != nullptr);
  CHECK(Find(n, Action::kBack) != nullptr);

  in.mode = Mode::kSymbol;
  in.side_items = {L"中文", L"英文"};
  in.side_selected = 1;
  in.symbols = {L",", L".", L"?"};
  Layout s = BuildLayout(in);
  const Element* cat = Find(s, Action::kSymbolCategory, L"英文");
  REQUIRE(cat);
  CHECK(cat->selected);
  const Element* q = Find(s, Action::kText, L"?");
  REQUIRE(q);
  CHECK(q->region == Region::kGrid);
}

TEST_CASE("letters layout for passwords") {
  LayoutInput in;
  in.measure = Measure;
  in.mode = Mode::kLetters;
  Layout l = BuildLayout(in);
  const Element* two = Find(l, Action::kLetter, L"abc");
  REQUIRE(two);
  CHECK(two->text == "2");
  CHECK(Find(l, Action::kKey) == nullptr);
  CHECK(Find(l, Action::kShift) != nullptr);
  CHECK(Find(l, Action::kBack) != nullptr);
  in.shift = true;
  Layout upper = BuildLayout(in);
  CHECK(Find(upper, Action::kLetter, L"ABC") != nullptr);
  CHECK(Find(upper, Action::kShift)->selected);
}
