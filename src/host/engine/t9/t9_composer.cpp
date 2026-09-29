#include "t9_composer.h"

#include "t9_keys.h"

namespace t9 {

std::optional<Segment> T9Composer::PendingSegment(std::string_view input) const {
  size_t pos = ConfirmedEnd();
  while (pos < input.size() && !IsT9Digit(input[pos])) {
    if (input[pos] != '\'') return std::nullopt;  // letters typed directly: nothing to pick
    ++pos;
  }
  if (pos >= input.size()) return std::nullopt;
  size_t end = pos;
  while (end < input.size() && IsT9Digit(input[end])) ++end;
  return Segment{pos, end - pos};
}

std::optional<std::string> T9Composer::Pick(std::string_view input, const BarItem& item) {
  Sync(input);
  auto seg = PendingSegment(input);
  if (!seg || item.consumed == 0 || item.consumed > seg->length) return std::nullopt;

  const size_t after = seg->start + item.consumed;
  // Reuse a separator the user already typed right after the picked digits.
  const bool has_separator = after < input.size() && input[after] == '\'';
  Confirmed c{seg->start, std::string(input.substr(seg->start, item.consumed)),
              item.spelling + '\'', has_separator};
  std::string out;
  out.reserve(input.size() + item.spelling.size() + 1);
  out.append(input.substr(0, seg->start)).append(c.text);
  out.append(input.substr(has_separator ? after + 1 : after));
  confirmed_.push_back(std::move(c));
  return out;
}

std::optional<std::string> T9Composer::UndoLast(std::string_view input) {
  Sync(input);
  if (confirmed_.empty() || input.size() != confirmed_.back().end()) return std::nullopt;
  const Confirmed& c = confirmed_.back();
  std::string out(input.substr(0, c.start));
  out += c.digits;
  if (c.absorbed_separator) out += '\'';
  confirmed_.pop_back();
  return out;
}

void T9Composer::Sync(std::string_view input) {
  size_t keep = 0;
  for (const Confirmed& c : confirmed_) {
    if (c.end() > input.size() || input.substr(c.start, c.text.size()) != c.text) break;
    ++keep;
  }
  confirmed_.resize(keep);
}

}  // namespace t9
