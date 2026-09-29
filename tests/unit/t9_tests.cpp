#include <doctest/doctest.h>

#include <algorithm>

#include "t9/pinyin_bar.h"
#include "t9/preedit_formatter.h"
#include "t9/syllable_index.h"
#include "t9/t9_composer.h"
#include "t9/t9_keys.h"

using namespace t9;

namespace {
std::vector<std::string> Spellings(const std::vector<BarItem>& items, size_t n = 100) {
  std::vector<std::string> out;
  for (size_t i = 0; i < items.size() && i < n; ++i) out.push_back(items[i].spelling);
  return out;
}
bool Contains(const std::vector<std::string>& v, std::string_view s) {
  return std::find(v.begin(), v.end(), s) != v.end();
}
}  // namespace

TEST_CASE("key mapping") {
  CHECK(ToDigits("zhong") == "94664");
  CHECK(ToDigits("shi") == "744");
  CHECK(ToDigits("lve") == "583");
  CHECK(ToDigits("zhong'guo") == "94664486");
  CHECK(LettersForDigit('7') == "pqrs");
  CHECK(LettersForDigit('9') == "wxyz");
  CHECK(LettersForDigit('1').empty());
  CHECK(IsT9Digit('2'));
  CHECK_FALSE(IsT9Digit('1'));
  CHECK_FALSE(IsT9Digit('0'));
}

TEST_CASE("builtin syllable table") {
  const SyllableIndex& idx = SyllableIndex::Builtin();
  CHECK(idx.size() > 400);
  CHECK(idx.max_code_length() == 6);  // zhuang / chuang / shuang
  auto s = idx.WithDigits("744");
  REQUIRE(!s.empty());
  CHECK(s[0]->spelling == "shi");  // most frequent reading of 744
  CHECK(idx.WithDigits("94664").size() >= 1);
  CHECK(idx.WithDigits("1").empty());
}

TEST_CASE("pinyin bar ordering") {
  const Syllable table[] = {{"a", "2", 5},    {"ba", "22", 9},  {"ca", "22", 3},
                            {"bai", "224", 7}, {"cai", "224", 8}, {"zhi", "944", 1},
                            {"xi", "94", 6},  {"yi", "94", 9}};
  SyllableIndex idx(table);

  SUBCASE("longest match first, then frequency, then letters") {
    auto items = BuildPinyinBar(idx, "2245");
    CHECK(Spellings(items) == std::vector<std::string>{"cai", "bai", "ba", "ca", "a", "b", "c"});
    CHECK(items[0].consumed == 3);
    CHECK(items[2].consumed == 2);
    CHECK(items.back().kind == BarItem::Kind::kLetter);
  }
  SUBCASE("zh/ch/sh initials follow full syllables of the same length") {
    auto items = BuildPinyinBar(idx, "946");
    CHECK(Spellings(items) == std::vector<std::string>{"yi", "xi", "zh", "w", "x", "y", "z"});
    CHECK(items[2].kind == BarItem::Kind::kInitial);
  }
  SUBCASE("empty input") { CHECK(BuildPinyinBar(idx, "").empty()); }
}

TEST_CASE("pinyin bar on builtin table") {
  auto items = Spellings(BuildPinyinBar(SyllableIndex::Builtin(), "94664486"));
  REQUIRE(!items.empty());
  CHECK(items[0] == "zhong");
  CHECK(Contains(items, "xiong"));
  CHECK(Contains(items, "zh"));
}

TEST_CASE("composer pick and undo") {
  T9Composer c;
  std::string input = "94664486";

  auto seg = c.PendingSegment(input);
  REQUIRE(seg);
  CHECK(seg->start == 0);
  CHECK(seg->length == 8);

  auto next = c.Pick(input, {"zhong", 5, BarItem::Kind::kSyllable});
  REQUIRE(next);
  CHECK(*next == "zhong'486");
  input = *next;

  seg = c.PendingSegment(input);
  REQUIRE(seg);
  CHECK(seg->start == 6);
  CHECK(seg->length == 3);

  next = c.Pick(input, {"guo", 3, BarItem::Kind::kSyllable});
  REQUIRE(next);
  CHECK(*next == "zhong'guo'");
  input = *next;
  CHECK_FALSE(c.PendingSegment(input));

  // Undo pops back one syllable at a time.
  next = c.UndoLast(input);
  REQUIRE(next);
  CHECK(*next == "zhong'486");
  input = *next;
  CHECK_FALSE(c.UndoLast(input));  // digits after the confirmed syllable: normal BackSpace
  input = "zhong'";                // engine deleted the digits
  next = c.UndoLast(input);
  REQUIRE(next);
  CHECK(*next == "94664");
  CHECK(c.confirmed_count() == 0);
}

TEST_CASE("composer reuses a typed separator and restores it on undo") {
  T9Composer c;
  auto next = c.Pick("94'4486", {"xi", 2, BarItem::Kind::kSyllable});
  REQUIRE(next);
  CHECK(*next == "xi'4486");
  CHECK(*c.UndoLast("xi'") == "94'");
}

TEST_CASE("composer rejects picks longer than the segment") {
  T9Composer c;
  CHECK_FALSE(c.Pick("94", {"zhong", 5, BarItem::Kind::kSyllable}));
  CHECK_FALSE(c.Pick("", {"a", 1, BarItem::Kind::kSyllable}));
}

TEST_CASE("composer drops stale confirmations") {
  T9Composer c;
  REQUIRE(c.Pick("744", {"shi", 3, BarItem::Kind::kSyllable}));
  c.Sync("");  // committed
  CHECK(c.confirmed_count() == 0);
  auto seg = c.PendingSegment("744");
  REQUIRE(seg);
  CHECK(seg->start == 0);
}

TEST_CASE("preedit formatting") {
  CHECK(FormatPreedit("94664486", "zhong guo") == "zhong'guo");
  CHECK(FormatPreedit("744", "shi") == "shi");
  CHECK(FormatPreedit("zhong'4486", "zhong hua hun") == "zhong'h'hun");  // 4486 = h + hun
  CHECK(FormatPreedit("zhong'486", "zhong guo") == "zhong'guo");
  CHECK(FormatPreedit("zhong'guo'", "zhong guo") == "zhong'guo");
  // Abbreviation: two syllables from two digits.
  CHECK(FormatPreedit("74", "shi ge") == "s'g");
  // Incomplete last syllable.
  CHECK(FormatPreedit("9466", "zhong") == "zhon");
  // Candidate shorter than the input: leftover digits become key letters.
  CHECK(FormatPreedit("94664486", "zhong") == "zhong'gtm");
  // Candidate longer than the input (completion).
  CHECK(FormatPreedit("94664", "zhong guo") == "zhong");
  // No usable comment (emoji, punctuation hints).
  CHECK(FormatPreedit("744", "") == "pgg");
  CHECK(FormatPreedit("744", "〔半角〕") == "pgg");
  // User typed separator.
  CHECK(FormatPreedit("94'482", "xi gua") == "xi'gua");
  CHECK(EnterText("94664486", "zhong guo") == "zhongguo");
}

TEST_CASE("pinyin bar prefers syllables of the top candidates") {
  const Syllable table[] = {{"huo", "486", 9}, {"guo", "486", 5}, {"gu", "48", 1}};
  SyllableIndex idx(table);
  const std::string preferred[] = {"guo", "gu", "zhong", "gou"};  // zhong/gou do not fit or exist
  auto items = BuildPinyinBar(idx, "486", preferred);
  CHECK(Spellings(items, 4) == std::vector<std::string>{"guo", "gu", "huo", "g"});
}
