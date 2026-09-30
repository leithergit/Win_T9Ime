#pragma once
// When the panel shows or hides itself on focus changes (SPEC §6, §4 InputScope).
// Pure decision logic; the panel window applies it.

#include <cstdint>
#include <string>
#include <vector>

#include "panel_layout.h"

namespace t9ime::panel {

struct AutoShowSettings {
  bool auto_show = true;     // pop up when a field is focused by touch / pen
  bool always_show = false;  // pop up on every focus (devices without touch, testing)
};

struct FocusEvent {
  bool focus_in = false;
  std::wstring exe;             // lowercase not required
  std::vector<uint32_t> scopes;  // InputScope values
  bool touch = false;
  bool read_only = false;
};

enum class AutoAction { kNone, kShow, kHide };

struct AutoDecision {
  AutoAction action = AutoAction::kNone;
  Mode mode = Mode::kChinese;  // layout for kShow
};

// `text_mode`: the layout to use for ordinary text fields (last Chinese /
// English choice). `win8_or_later`: system overlays (Start, search) only
// exist there and cover the panel, so it never pops up for them.
AutoDecision DecideOnFocus(const FocusEvent& e, const AutoShowSettings& settings, Mode text_mode,
                           bool win8_or_later);

// Layout for a field: numbers / English / the text mode.
Mode ModeForScopes(const std::vector<uint32_t>& scopes, Mode text_mode);

}  // namespace t9ime::panel
