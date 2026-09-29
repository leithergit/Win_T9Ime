#include "syllable_index.h"

#include <algorithm>

namespace t9 {

namespace {
constexpr Syllable kBuiltin[] = {
#include "syllable_table.gen.inc"
};
}  // namespace

SyllableIndex::SyllableIndex(std::span<const Syllable> syllables) : count_(syllables.size()) {
  for (const Syllable& s : syllables) {
    by_digits_[std::string(s.digits)].push_back(&s);
    max_len_ = std::max(max_len_, s.digits.size());
  }
  for (auto& [digits, list] : by_digits_) {
    std::stable_sort(list.begin(), list.end(), [](const Syllable* a, const Syllable* b) {
      return a->frequency > b->frequency;
    });
  }
}

const SyllableIndex& SyllableIndex::Builtin() {
  static const SyllableIndex index{std::span<const Syllable>(kBuiltin)};
  return index;
}

std::span<const Syllable* const> SyllableIndex::WithDigits(std::string_view digits) const {
  auto it = by_digits_.find(std::string(digits));
  if (it == by_digits_.end()) return {};
  return it->second;
}

}  // namespace t9
