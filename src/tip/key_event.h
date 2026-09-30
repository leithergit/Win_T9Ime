#pragma once
// Windows virtual keys -> X11 keysyms + modifier masks, as librime expects.
// Adapted from Weasel WeaselTSF/KeyEvent.cpp.

#include <windows.h>

#include <cstdint>

namespace t9ime::tip {

namespace keysym {
constexpr uint32_t kBackSpace = 0xff08, kTab = 0xff09, kReturn = 0xff0d, kEscape = 0xff1b, kSpace = 0x20;
constexpr uint32_t kShiftL = 0xffe1, kShiftR = 0xffe2, kControlL = 0xffe3, kControlR = 0xffe4;
constexpr uint32_t kCapsLock = 0xffe5, kMetaL = 0xffe7, kMetaR = 0xffe8, kAltL = 0xffe9, kAltR = 0xffea;
}  // namespace keysym

namespace mask {
constexpr uint32_t kShift = 1 << 0, kLock = 1 << 1, kControl = 1 << 2, kAlt = 1 << 3, kRelease = 1u << 30;
}

struct KeyEvent {
  uint32_t keycode = 0;
  uint32_t mask = 0;
};

// False for keys librime has no keysym for (they are passed through).
bool ConvertKey(WPARAM vk, LPARAM lparam, const BYTE key_state[256], bool key_up, KeyEvent* out);

inline bool IsModifier(uint32_t keycode) {
  return keycode >= keysym::kShiftL && keycode <= keysym::kAltR;
}

}  // namespace t9ime::tip
