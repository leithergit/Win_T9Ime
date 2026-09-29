#pragma once
// Turns a nine-key input such as "94664486" into readable pinyin ("zhong'guo").
//
// librime only returns the digits as preedit. The highlighted candidate's comment
// holds its full pinyin ("zhong guo", from translator/spelling_hints), so the
// digits are aligned against those syllables. Syllables may be matched partially
// (abbreviations: "74" for 是个 -> "s'g"). Digits that cannot be aligned are shown
// as the first letter of their key.

#include <string>
#include <string_view>

namespace t9 {

// `input`: Rime input (digits 2-9, lowercase confirmed syllables, "'").
// `comment`: space separated pinyin of the highlighted candidate; may be empty.
// Result parts are joined with `separator`.
std::string FormatPreedit(std::string_view input, std::string_view comment, char separator = '\'');

// Text committed by the Enter key: the readable pinyin without separators.
std::string EnterText(std::string_view input, std::string_view comment);

}  // namespace t9
