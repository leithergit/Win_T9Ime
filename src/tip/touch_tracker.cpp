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
  ULONG_PTR extra = 0;  // dwExtraInfo of the mouse press
};
thread_local LastPress g_last;
thread_local HWND g_notify = nullptr;
thread_local UINT g_notify_message = 0;

void Record(HWND target, bool touch, ULONG_PTR extra = 0) {
  g_last = {GetTickCount64(), touch, extra};
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
      case compat::kWmPointerDown: {
        DWORD type = 0;
        compat::GetPointerType(compat::PointerId(m->wParam), &type);
        Record(m->hwnd, type == compat::kPointerTypeTouch || type == compat::kPointerTypePen);
        break;
      }
      case WM_KEYDOWN:
        g_last = {GetTickCount64(), false};
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

bool TouchTracker::FocusFromTouch() const {
  static const GetSourceFn current = Load("GetCurrentInputMessageSource");
  static const GetSourceFn cimssm = Load("GetCIMSSM");
  if (SourceIsTouch(current) || SourceIsTouch(cimssm)) return true;
  // The mouse message being handled right now (a click that moved the focus).
  if (compat::IsMouseFromTouchOrPen()) return true;
  return g_last.touch && GetTickCount64() - g_last.tick < kRecentMs;
}

std::wstring TouchTracker::Describe() const {
  static const GetSourceFn current = Load("GetCurrentInputMessageSource");
  InputMessageSource src = {};
  const bool have_source = current && current(&src);
  wchar_t buf[160];
  swprintf_s(buf, L"source=%ls extra_now=%08llx last_press=%llums ago extra=%08llx touch=%d hooks=%d%d",
             have_source ? std::to_wstring(src.deviceType).c_str() : L"n/a",
             static_cast<unsigned long long>(GetMessageExtraInfo()),
             g_last.tick ? GetTickCount64() - g_last.tick : 0ULL, static_cast<unsigned long long>(g_last.extra),
             g_last.touch ? 1 : 0, message_hook_ ? 1 : 0, mouse_hook_ ? 1 : 0);
  return buf;
}

}  // namespace t9ime::tip
