#pragma once
// Nine-key (T9) letter <-> digit mapping. 2=abc 3=def 4=ghi 5=jkl 6=mno 7=pqrs 8=tuv 9=wxyz.

#include <string>
#include <string_view>

namespace t9 {

constexpr bool IsT9Digit(char c) noexcept { return c >= '2' && c <= '9'; }
constexpr bool IsLower(char c) noexcept { return c >= 'a' && c <= 'z'; }

// Digit for a lowercase letter; '\0' for anything else.
constexpr char DigitForLetter(char c) noexcept {
  constexpr std::string_view kMap = "22233344455566677778889999";
  return IsLower(c) ? kMap[c - 'a'] : '\0';
}

// Letters on a digit key, e.g. '7' -> "pqrs"; empty for non-T9 digits.
constexpr std::string_view LettersForDigit(char d) noexcept {
  constexpr std::string_view kLetters[] = {"abc", "def", "ghi", "jkl",
                                           "mno", "pqrs", "tuv", "wxyz"};
  return IsT9Digit(d) ? kLetters[d - '2'] : std::string_view{};
}

// "zhong" -> "94664". Non-letters are dropped.
inline std::string ToDigits(std::string_view spelling) {
  std::string out;
  out.reserve(spelling.size());
  for (char c : spelling) {
    if (char d = DigitForLetter(c)) out.push_back(d);
  }
  return out;
}

}  // namespace t9
