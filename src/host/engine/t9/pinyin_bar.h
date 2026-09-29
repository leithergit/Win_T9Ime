#pragma once
// Pinyin selection bar: syllables the user can pick for the first unconfirmed digit segment.

#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "syllable_index.h"

namespace t9 {

struct BarItem {
  enum class Kind { kSyllable, kInitial, kLetter };
  std::string spelling;  // what replaces the digits, e.g. "zhong", "zh", "z"
  size_t consumed = 0;   // number of leading digits of the segment it replaces
  Kind kind = Kind::kSyllable;
};

// Items for `digits` (a run of 2-9). Order: syllables listed in `preferred` (in
// that order, typically taken from the current top candidates) first; then longer
// match first; within a length, full syllables by frequency, then zh/ch/sh
// initials, then the single letters of the first key. Duplicates are removed.
std::vector<BarItem> BuildPinyinBar(const SyllableIndex& index, std::string_view digits,
                                    std::span<const std::string> preferred = {});

}  // namespace t9
