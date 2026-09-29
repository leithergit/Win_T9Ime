#include "pinyin_bar.h"

#include <algorithm>
#include <unordered_set>

#include "t9_keys.h"

namespace t9 {

namespace {
constexpr std::string_view kCompoundInitials[] = {"zh", "ch", "sh"};
}

std::vector<BarItem> BuildPinyinBar(const SyllableIndex& index, std::string_view digits,
                                    std::span<const std::string> preferred) {
  std::vector<BarItem> items;
  std::unordered_set<std::string> seen;
  auto add = [&](std::string_view spelling, size_t consumed, BarItem::Kind kind) {
    if (seen.emplace(spelling).second) items.push_back({std::string(spelling), consumed, kind});
  };

  for (const std::string& p : preferred) {
    const std::string code = ToDigits(p);
    if (p.empty() || code.size() != p.size() || !digits.starts_with(code)) continue;
    for (const Syllable* s : index.WithDigits(code)) {
      if (s->spelling == p) add(p, code.size(), BarItem::Kind::kSyllable);
    }
  }

  const size_t longest = std::min(digits.size(), index.max_code_length());
  for (size_t len = longest; len >= 1; --len) {
    const std::string_view prefix = digits.substr(0, len);
    for (const Syllable* s : index.WithDigits(prefix)) add(s->spelling, len, BarItem::Kind::kSyllable);
    if (len == 2) {
      for (std::string_view initial : kCompoundInitials) {
        if (ToDigits(initial) == prefix) add(initial, len, BarItem::Kind::kInitial);
      }
    }
    if (len == 1) {
      for (char c : LettersForDigit(prefix[0])) add(std::string_view(&c, 1), 1, BarItem::Kind::kLetter);
    }
  }
  return items;
}

}  // namespace t9
