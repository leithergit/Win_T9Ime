#include "key_event.h"

namespace t9ime::tip {

namespace {

uint32_t TranslateVirtualKey(WPARAM vk, bool extended, UINT scan) {
  switch (vk) {
    case VK_BACK: return keysym::kBackSpace;
    case VK_TAB: return keysym::kTab;
    case VK_CLEAR: return 0xff0b;
    case VK_RETURN: return extended ? 0xff8d : keysym::kReturn;  // KP_Enter
    case VK_SHIFT: return scan == 0x36 ? keysym::kShiftR : keysym::kShiftL;
    case VK_CONTROL: return extended ? keysym::kControlR : keysym::kControlL;
    case VK_MENU: return extended ? keysym::kAltR : keysym::kAltL;
    case VK_LSHIFT: return keysym::kShiftL;
    case VK_RSHIFT: return keysym::kShiftR;
    case VK_LCONTROL: return keysym::kControlL;
    case VK_RCONTROL: return keysym::kControlR;
    case VK_LMENU: return keysym::kAltL;
    case VK_RMENU: return keysym::kAltR;
    case VK_PAUSE: return 0xff13;
    case VK_CAPITAL: return keysym::kCapsLock;
    case VK_ESCAPE: return keysym::kEscape;
    case VK_SPACE: return keysym::kSpace;
    case VK_PRIOR: return 0xff55;
    case VK_NEXT: return 0xff56;
    case VK_END: return 0xff57;
    case VK_HOME: return 0xff50;
    case VK_LEFT: return 0xff51;
    case VK_UP: return 0xff52;
    case VK_RIGHT: return 0xff53;
    case VK_DOWN: return 0xff54;
    case VK_SELECT: return 0xff60;
    case VK_INSERT: return 0xff63;
    case VK_DELETE: return 0xffff;
    case VK_HELP: return 0xff6a;
    case VK_LWIN: return keysym::kMetaL;
    case VK_RWIN: return keysym::kMetaR;
    case VK_MULTIPLY: return 0xffaa;
    case VK_ADD: return 0xffab;
    case VK_SEPARATOR: return 0xffac;
    case VK_SUBTRACT: return 0xffad;
    case VK_DECIMAL: return 0xffae;
    case VK_DIVIDE: return 0xffaf;
    case VK_NUMLOCK: return 0xff7f;
    case VK_SCROLL: return 0xff14;
    default: break;
  }
  if (vk >= VK_NUMPAD0 && vk <= VK_NUMPAD9) return 0xffb0 + static_cast<uint32_t>(vk - VK_NUMPAD0);
  if (vk >= VK_F1 && vk <= VK_F24) return 0xffbe + static_cast<uint32_t>(vk - VK_F1);
  return 0;
}

}  // namespace

bool ConvertKey(WPARAM vk, LPARAM lparam, const BYTE key_state[256], bool key_up, KeyEvent* out) {
  constexpr BYTE kDown = 0x80, kToggled = 0x01;
  out->mask = 0;
  if (key_state[VK_SHIFT] & kDown) out->mask |= mask::kShift;
  if (key_state[VK_CAPITAL] & kToggled) out->mask |= mask::kLock;
  if (key_state[VK_CONTROL] & kDown) out->mask |= mask::kControl;
  if (key_state[VK_MENU] & kDown) out->mask |= mask::kAlt;
  if (key_up) out->mask |= mask::kRelease;
  // librime expects Caps_Lock before the lock state changes; Windows has
  // already toggled it on key down.
  if (vk == VK_CAPITAL && !key_up) out->mask ^= mask::kLock;

  const UINT scan = (lparam >> 16) & 0xFF;
  const bool extended = (lparam >> 24) & 1;
  if (uint32_t code = TranslateVirtualKey(vk, extended, scan)) {
    out->keycode = code;
    return true;
  }

  // Printable keys: the character for the current layout, ignoring Ctrl/Alt
  // so that e.g. Ctrl+A reports 'a'.
  BYTE table[256];
  memcpy(table, key_state, sizeof(table));
  table[VK_CONTROL] = table[VK_LCONTROL] = table[VK_RCONTROL] = 0;
  table[VK_MENU] = table[VK_LMENU] = table[VK_RMENU] = 0;
  WCHAR buf[8];
  const int n = ToUnicodeEx(static_cast<UINT>(vk), scan, table, buf, ARRAYSIZE(buf), 0, GetKeyboardLayout(0));
  if (n == 1 && buf[0] >= 0x20) {
    out->keycode = buf[0];
    return true;
  }
  out->keycode = 0;
  return false;
}

}  // namespace t9ime::tip
