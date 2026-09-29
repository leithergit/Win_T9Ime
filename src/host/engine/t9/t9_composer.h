#pragma once
// Front-end state of a nine-key composition: which digit segments the user has
// confirmed through the pinyin bar, and the input rewrites that go with it.
//
// The Rime input string looks like  "zhong'guo'4486"  -- confirmed syllables are
// lowercase pinyin followed by an apostrophe, unconfirmed input is digits 2-9,
// and "'" may also come from the 1 key (syllable separator).

#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "pinyin_bar.h"

namespace t9 {

struct Segment {
  size_t start = 0;
  size_t length = 0;
};

class T9Composer {
 public:
  // First run of digits after the confirmed prefix, or nullopt when none.
  std::optional<Segment> PendingSegment(std::string_view input) const;

  // Input after replacing the pending segment's first `item.consumed` digits
  // with `item.spelling` + "'". Records the step so it can be undone.
  // Returns nullopt when there is no pending segment long enough.
  std::optional<std::string> Pick(std::string_view input, const BarItem& item);

  // If the input ends exactly at the last confirmed syllable, restores that
  // syllable to its digits and returns the new input; otherwise nullopt
  // (the caller should send a normal BackSpace).
  std::optional<std::string> UndoLast(std::string_view input);

  // Drops confirmations that no longer match `input` (after commit, clear, or
  // edits made by the engine). Call whenever the input may have changed.
  void Sync(std::string_view input);

  void Reset() { confirmed_.clear(); }
  size_t confirmed_count() const noexcept { return confirmed_.size(); }

 private:
  struct Confirmed {
    size_t start;         // offset in the input
    std::string digits;   // digits that were replaced
    std::string text;     // replacement, spelling + "'"
    bool absorbed_separator = false;  // a user-typed "'" after the digits was reused
    size_t end() const { return start + text.size(); }
  };
  size_t ConfirmedEnd() const { return confirmed_.empty() ? 0 : confirmed_.back().end(); }

  std::vector<Confirmed> confirmed_;
};

}  // namespace t9
