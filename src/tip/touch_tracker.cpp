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
};
thread_local LastPress g_last;
thread_local HWND g_notify = nullptr;
thread_local UINT g_notify_message = 0;

void Record(HWND target, bool touch) {
  g_last = {GetTickCount64(), touch};
  if (!touch || !g_notify) return;
  HWND focus = GetFocus();
  if (focus && (target == focus || IsChild(focus, target))) PostMessageW(g_notify, g_notify_message, 0, 0);
}

LRESULT CALLBACK GetMessageHook(int code, WPARAM wp, LPARAM lp) {
  if (code == HC_ACTION && wp == PM_REMOVE) {
    const MSG* m = reinterpret_cast<const MSG*>(lp);
    switch (m->message) {
      case WM_LBUTTONDOWN:
      case WM_RBUTTONDOWN:
      case WM_NCLBUTTONDOWN:
        Record(m->hwnd, compat::IsMouseFromTouchOrPen());
        break;
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
  if (!hook_) hook_ = SetWindowsHookExW(WH_GETMESSAGE, GetMessageHook, nullptr, GetCurrentThreadId());
}

void TouchTracker::Uninstall() {
  if (hook_) {
    UnhookWindowsHookEx(hook_);
    hook_ = nullptr;
  }
  g_notify = nullptr;
}

bool TouchTracker::FocusFromTouch() const {
  static const GetSourceFn current = Load("GetCurrentInputMessageSource");
  static const GetSourceFn cimssm = Load("GetCIMSSM");
  if (SourceIsTouch(current) || SourceIsTouch(cimssm)) return true;
  return g_last.touch && GetTickCount64() - g_last.tick < kRecentMs;
}

}  // namespace t9ime::tip
