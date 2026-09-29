#pragma once
// Index of pinyin syllables by their nine-key digit code.

#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace t9 {

struct Syllable {
  std::string_view spelling;  // "zhong"
  std::string_view digits;    // "94664"
  uint32_t frequency;         // relative usage, higher is more common
};

class SyllableIndex {
 public:
  explicit SyllableIndex(std::span<const Syllable> syllables);

  // The table generated from rime-ice at build time.
  static const SyllableIndex& Builtin();

  // Syllables whose code equals `digits`, most frequent first.
  std::span<const Syllable* const> WithDigits(std::string_view digits) const;

  size_t size() const noexcept { return count_; }
  size_t max_code_length() const noexcept { return max_len_; }

 private:
  std::unordered_map<std::string, std::vector<const Syllable*>> by_digits_;
  size_t count_ = 0;
  size_t max_len_ = 0;
};

}  // namespace t9
