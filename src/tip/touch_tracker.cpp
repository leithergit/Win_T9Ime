#include "touch_tracker.h"

#include "win_compat.h"

namespace t9ime::tip {

namespace {

constexpr ULONGLONG kRecentMs = 800;
constexpr UINT kDeviceTouch = 4, kDevicePen = 8;  // INPUT_MESSAGE_DEVICE_TYPE

struct InputMessageSource {
  UINT deviceType;
  UINT originId;
};
using GetSourceFn = BOOL(WINAPI*)(InputMessageSource*);

struct LastPress {
  ULONGLONG tick = 0;
  bool touch = false;
  ULONG_PTR extra = 0;    // dwExtraInfo of the mouse press
  HWND target = nullptr;  // window pressed (nullptr: a key)
};
thread_local LastPress g_last;
thread_local HWND g_notify = nullptr;
thread_local UINT g_notify_message = 0;

void Record(HWND target, bool touch, ULONG_PTR extra = 0) {
  g_last = {GetTickCount64(), touch, extra, target};
  // Any press on the focused window (no focus change follows): report the
  // focus again; the host decides with the touch flag and its settings.
  if (!g_notify) return;
  HWND focus = GetFocus();
  if (focus && (target == focus || IsChild(focus, target))) PostMessageW(g_notify, g_notify_message, 0, 0);
}

LRESULT CALLBACK GetMessageHook(int code, WPARAM wp, LPARAM lp) {
  if (code == HC_ACTION && wp == PM_REMOVE) {
    const MSG* m = reinterpret_cast<const MSG*>(lp);
    switch (m->message) {
      case compat::kWmPointerDown:
      case compat::kWmNcPointerDown: {  // title bar, borders
        DWORD type = 0;
        compat::GetPointerType(compat::PointerId(m->wParam), &type);
        Record(m->hwnd, type == compat::kPointerTypeTouch || type == compat::kPointerTypePen);
        break;
      }
      case WM_KEYDOWN:
        g_last = {GetTickCount64(), false, 0, nullptr};
        break;
    }
  }
  return CallNextHookEx(nullptr, code, wp, lp);
}

LRESULT CALLBACK MouseHook(int code, WPARAM wp, LPARAM lp) {
  if (code == HC_ACTION && (wp == WM_LBUTTONDOWN || wp == WM_RBUTTONDOWN || wp == WM_NCLBUTTONDOWN)) {
    const auto* m = reinterpret_cast<const MOUSEHOOKSTRUCT*>(lp);
    Record(m->hwnd, (m->dwExtraInfo & 0xFFFFFF00) == 0xFF515700, m->dwExtraInfo);
  }
  return CallNextHookEx(nullptr, code, wp, lp);
}

// The press landed on the focused window: on it, inside it, or on a control
// containing it (a combo box and its edit). Not a press on a top-level window
// (title bar, dialog background) whose handler then focuses a field.
bool PressedOn(HWND target, HWND focus) {
  if (!target || !focus) return false;
  if (target == focus || IsChild(focus, target)) return true;
  return IsChild(target, focus) && GetAncestor(target, GA_ROOT) != target;
}

GetSourceFn Load(const char* name) {
  HMODULE user32 = GetModuleHandleW(L"user32.dll");
  return user32 ? reinterpret_cast<GetSourceFn>(GetProcAddress(user32, name)) : nullptr;
}

// Windows 8+ only; false when unavailable.
bool SourceIsTouch(GetSourceFn fn) {
  InputMessageSource src = {};
  return fn && fn(&src) && (src.deviceType == kDeviceTouch || src.deviceType == kDevicePen);
}

}  // namespace

void TouchTracker::Install(HWND notify, UINT message) {
  g_notify = notify;
  g_notify_message = message;
  if (!message_hook_) message_hook_ = SetWindowsHookExW(WH_GETMESSAGE, GetMessageHook, nullptr, GetCurrentThreadId());
  if (!mouse_hook_) mouse_hook_ = SetWindowsHookExW(WH_MOUSE, MouseHook, nullptr, GetCurrentThreadId());
}

void TouchTracker::Uninstall() {
  for (HHOOK* h : {&message_hook_, &mouse_hook_}) {
    if (*h) UnhookWindowsHookEx(*h);
    *h = nullptr;
  }
  g_notify = nullptr;
}

bool TouchTracker::FocusFromTouch(HWND focus) const {
  static const GetSourceFn current = Load("GetCurrentInputMessageSource");
  static const GetSourceFn cimssm = Load("GetCIMSSM");
  const bool recent = g_last.tick && GetTickCount64() - g_last.tick < kRecentMs;
  // The message being handled right now (a press that moved the focus, or the
  // click handler of a button that gives the focus back to a text box).
  const bool now_touch = SourceIsTouch(current) || SourceIsTouch(cimssm) || compat::IsMouseFromTouchOrPen();
  if (!now_touch && !(recent && g_last.touch)) return false;
  // A press seen on this thread must have been on the focused field: a finger
  // on a button whose handler calls SetFocus(edit) did not touch the field.
  if (recent && g_last.target) return PressedOn(g_last.target, focus);
  return now_touch;  // no press seen here (e.g. input handled on another thread)
}

std::wstring TouchTracker::Describe() const {
  static const GetSourceFn current = Load("GetCurrentInputMessageSource");
  InputMessageSource src = {};
  const bool have_source = current && current(&src);
  wchar_t cls[32] = L"-";
  if (g_last.target) GetClassNameW(g_last.target, cls, 32);
  wchar_t buf[224];
  swprintf_s(buf,
             L"source=%ls extra_now=%08llx last_press=%llums ago extra=%08llx touch=%d on=%ls%ls hooks=%d%d",
             have_source ? std::to_wstring(src.deviceType).c_str() : L"n/a",
             static_cast<unsigned long long>(GetMessageExtraInfo()),
             g_last.tick ? GetTickCount64() - g_last.tick : 0ULL, static_cast<unsigned long long>(g_last.extra),
             g_last.touch ? 1 : 0, cls, PressedOn(g_last.target, GetFocus()) ? L"(focus)" : L"",
             message_hook_ ? 1 : 0, mouse_hook_ ? 1 : 0);
  return buf;
}

}  // namespace t9ime::tip
