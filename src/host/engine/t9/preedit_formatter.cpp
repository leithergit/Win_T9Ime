#include "preedit_formatter.h"

#include <map>
#include <tuple>
#include <vector>

#include "t9_keys.h"

namespace t9 {

namespace {

struct Token {
  bool digits;       // run of 2-9, otherwise a run of letters
  std::string text;
};

std::vector<Token> Tokenize(std::string_view input) {
  std::vector<Token> tokens;
  for (size_t i = 0; i < input.size();) {
    const char c = input[i];
    const bool digit = IsT9Digit(c);
    if (!digit && !IsLower(c)) {  // "'" and anything else only separate tokens
      ++i;
      continue;
    }
    size_t j = i;
    while (j < input.size() && (digit ? IsT9Digit(input[j]) : IsLower(input[j]))) ++j;
    tokens.push_back({digit, std::string(input.substr(i, j - i))});
    i = j;
  }
  return tokens;
}

// Space separated lowercase syllables; empty if the comment is not pure pinyin
// (e.g. punctuation hints such as "〔半角〕").
std::vector<std::string> SplitComment(std::string_view comment) {
  std::vector<std::string> out;
  for (size_t i = 0; i < comment.size();) {
    if (comment[i] == ' ') {
      ++i;
      continue;
    }
    size_t j = i;
    while (j < comment.size() && comment[j] != ' ') {
      if (!IsLower(comment[j])) return {};
      ++j;
    }
    out.emplace_back(comment.substr(i, j - i));
    i = j;
  }
  return out;
}

// Alignment score, compared lexicographically: fewer unmatched digits, then more
// fully matched syllables, then more syllables used.
struct Score {
  int unmatched = 0, full = 0, used = 0;
  bool BetterThan(const Score& o) const {
    return std::tie(o.unmatched, full, used) > std::tie(unmatched, o.full, o.used);
  }
};

class Aligner {
 public:
  Aligner(std::vector<Token> tokens, std::vector<std::string> syllables)
      : tokens_(std::move(tokens)), syl_(std::move(syllables)) {
    for (const auto& s : syl_) codes_.push_back(ToDigits(s));
  }

  std::vector<std::string> Parts() {
    std::vector<std::string> parts;
    size_t t = 0, pos = 0, ci = 0;
    std::string unmatched;
    auto flush = [&] {
      if (!unmatched.empty()) parts.push_back(std::move(unmatched));
      unmatched.clear();
    };
    while (t < tokens_.size()) {
      const Token& tok = tokens_[t];
      if (tok.digits && pos == tok.text.size()) {
        flush();
        ++t, pos = 0;
        continue;
      }
      const Step step = Best(t, pos, ci).second;
      if (!tok.digits) {
        parts.push_back(tok.text);
        ci += step.take;
        ++t;
      } else if (step.take == 0) {  // skip one unmatched digit
        unmatched.push_back(LettersForDigit(tok.text[pos])[0]);
        ++pos;
      } else {
        flush();
        parts.push_back(syl_[ci].substr(0, step.take));
        pos += step.take;
        ++ci;
      }
    }
    flush();
    return parts;
  }

 private:
  // Letters token: `take` = syllables consumed. Digits token: `take` = digits
  // matched by syllable ci (0 = leave one digit unmatched).
  struct Step {
    size_t take = 0;
  };
  using Key = std::tuple<size_t, size_t, size_t>;

  std::pair<Score, Step> Best(size_t t, size_t pos, size_t ci) {
    if (t == tokens_.size()) return {};
    const Key key{t, pos, ci};
    if (auto it = memo_.find(key); it != memo_.end()) return it->second;

    const Token& tok = tokens_[t];
    std::pair<Score, Step> best;
    bool have = false;
    auto consider = [&](Score s, Step step) {
      if (!have || s.BetterThan(best.first)) best = {s, step}, have = true;
    };

    if (!tok.digits) {
      consider(Best(t + 1, 0, ci).first, {0});
      std::string joined;
      for (size_t k = 1; ci + k <= syl_.size() && joined.size() < tok.text.size(); ++k) {
        joined += syl_[ci + k - 1];
        if (joined == tok.text) {
          Score s = Best(t + 1, 0, ci + k).first;
          s.full += static_cast<int>(k), s.used += static_cast<int>(k);
          consider(s, {k});
        }
      }
    } else if (pos == tok.text.size()) {
      best = Best(t + 1, 0, ci);
      have = true;
    } else {
      Score skip = Best(t, pos + 1, ci).first;
      ++skip.unmatched;
      consider(skip, {0});
      if (ci < syl_.size()) {
        const std::string& code = codes_[ci];
        const std::string_view rest = std::string_view(tok.text).substr(pos);
        for (size_t k = 1; k <= code.size() && k <= rest.size() && code[k - 1] == rest[k - 1]; ++k) {
          Score s = Best(t, pos + k, ci + 1).first;
          s.full += k == code.size() ? 1 : 0;
          s.used += 1;
          consider(s, {k});
        }
      }
    }
    memo_[key] = best;
    return best;
  }

  std::vector<Token> tokens_;
  std::vector<std::string> syl_;
  std::vector<std::string> codes_;
  std::map<Key, std::pair<Score, Step>> memo_;
};

}  // namespace

std::string FormatPreedit(std::string_view input, std::string_view comment, char separator) {
  Aligner aligner(Tokenize(input), SplitComment(comment));
  std::string out;
  for (const std::string& part : aligner.Parts()) {
    if (!out.empty()) out.push_back(separator);
    out += part;
  }
  return out;
}

std::string EnterText(std::string_view input, std::string_view comment) {
  std::string out;
  for (char c : FormatPreedit(input, comment)) {
    if (c != '\'') out.push_back(c);
  }
  return out;
}

}  // namespace t9
