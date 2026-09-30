#include "win_compat.h"

#include <initializer_list>

namespace t9ime::compat {

namespace {

template <typename Fn>
Fn Load(const wchar_t* dll, const char* name) {
  HMODULE m = GetModuleHandleW(dll);
  if (!m) m = LoadLibraryExW(dll, nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
  if (!m) m = LoadLibraryW(dll);  // Windows 7 without KB2533623
  return m ? reinterpret_cast<Fn>(GetProcAddress(m, name)) : nullptr;
}

using GetPointerTypeFn = BOOL(WINAPI*)(UINT32, DWORD*);
using SetWindowFeedbackSettingFn = BOOL(WINAPI*)(HWND, int, DWORD, UINT32, const void*);
using GetDpiForWindowFn = UINT(WINAPI*)(HWND);
using GetDpiForMonitorFn = HRESULT(WINAPI*)(HMONITOR, int, UINT*, UINT*);
using RtlGetVersionFn = LONG(WINAPI*)(OSVERSIONINFOW*);

UINT SystemDpi() {
  HDC dc = GetDC(nullptr);
  const UINT dpi = dc ? static_cast<UINT>(GetDeviceCaps(dc, LOGPIXELSX)) : 96;
  if (dc) ReleaseDC(nullptr, dc);
  return dpi ? dpi : 96;
}

UINT DpiForMonitor(HMONITOR monitor) {
  static const auto fn = Load<GetDpiForMonitorFn>(L"shcore.dll", "GetDpiForMonitor");
  UINT x = 0, y = 0;
  if (fn && monitor && SUCCEEDED(fn(monitor, 0 /*MDT_EFFECTIVE_DPI*/, &x, &y)) && x) return x;
  return SystemDpi();
}

}  // namespace

const OsVersion& Os() {
  static const OsVersion v = [] {
    OsVersion r;
    OSVERSIONINFOW info = {sizeof(info)};
    if (auto fn = Load<RtlGetVersionFn>(L"ntdll.dll", "RtlGetVersion"); fn && fn(&info) == 0) {
      r.major = info.dwMajorVersion;
      r.minor = info.dwMinorVersion;
      r.build = info.dwBuildNumber;
    }
    return r;
  }();
  return v;
}

bool HasPointerInput() {
  static const bool has = Load<GetPointerTypeFn>(L"user32.dll", "GetPointerType") != nullptr;
  return has;
}

bool GetPointerType(UINT32 pointer_id, DWORD* type) {
  static const auto fn = Load<GetPointerTypeFn>(L"user32.dll", "GetPointerType");
  return fn && fn(pointer_id, type);
}

void DisableTouchFeedback(HWND hwnd) {
  static const auto fn = Load<SetWindowFeedbackSettingFn>(L"user32.dll", "SetWindowFeedbackSetting");
  if (fn) {
    const BOOL off = FALSE;
    // FEEDBACK_TOUCH_CONTACTVISUALIZATION .. FEEDBACK_GESTURE_PRESSANDTAP
    for (int type : {1, 3, 4, 5, 6, 7, 8, 9, 10, 11}) fn(hwnd, type, 0, sizeof(off), &off);
  }
  // Windows 7 (and older touch stacks): no press-and-hold right click, no
  // flicks, no tap feedback (the window also answers WM_TABLET_QUERYSYSTEMGESTURESTATUS).
  SetPropW(hwnd, L"MicrosoftTabletPenServiceProperty",
           reinterpret_cast<HANDLE>(static_cast<ULONG_PTR>(kTabletGestureOff)));
}

UINT DpiForWindow(HWND hwnd) {
  static const auto fn = Load<GetDpiForWindowFn>(L"user32.dll", "GetDpiForWindow");
  if (fn && hwnd) {
    if (UINT dpi = fn(hwnd)) return dpi;
  }
  return DpiForMonitor(MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST));
}

UINT DpiForPoint(POINT pt) { return DpiForMonitor(MonitorFromPoint(pt, MONITOR_DEFAULTTONEAREST)); }

bool SystemPrefersDark() {
  if (!Os().AtLeastWin10()) return false;
  DWORD value = 1, size = sizeof(value);
  if (RegGetValueW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
                   L"AppsUseLightTheme", RRF_RT_REG_DWORD, nullptr, &value, &size) != ERROR_SUCCESS) {
    return false;
  }
  return value == 0;
}

}  // namespace t9ime::compat
