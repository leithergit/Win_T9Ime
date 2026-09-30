#pragma once
// Windows 7 baseline helpers. Everything newer than Windows 7 is resolved at run
// time with GetProcAddress; the SDK only declares these when _WIN32_WINNT is
// raised, so the few constants and types needed are declared here.

#include <windows.h>

namespace t9ime::compat {

// ---- Pointer input (Windows 8+) ----
constexpr UINT kWmNcPointerDown = 0x0242;
constexpr UINT kWmPointerUpdate = 0x0245;
constexpr UINT kWmPointerDown = 0x0246;
constexpr UINT kWmPointerUp = 0x0247;
constexpr UINT kWmPointerLeave = 0x024A;
constexpr UINT kWmPointerActivate = 0x024B;
constexpr UINT kWmPointerCaptureChanged = 0x024C;
constexpr LRESULT kPaNoActivate = 3;
constexpr DWORD kPointerTypeTouch = 2;
constexpr DWORD kPointerTypePen = 3;
constexpr DWORD kPointerTypeMouse = 4;
constexpr WORD kPointerFlagCanceled = 0x8000;  // HIWORD(wParam) of WM_POINTER*
inline UINT32 PointerId(WPARAM w) { return LOWORD(w); }

// ---- DPI (Windows 8.1+ / 10+) ----
constexpr UINT kWmDpiChanged = 0x02E0;

// Windows version from RtlGetVersion (not subject to manifest compatibility shims).
struct OsVersion {
  DWORD major = 0, minor = 0, build = 0;
  bool AtLeastWin10() const { return major >= 10; }
  bool IsWin7() const { return major == 6 && minor == 1; }
};
const OsVersion& Os();

// True when WM_POINTER input is available (Windows 8+).
bool HasPointerInput();
// GetPointerType; false when unavailable.
bool GetPointerType(UINT32 pointer_id, DWORD* type);
// Disables system touch visuals (contact circles, press-and-hold) for a window.
// Windows 8+: SetWindowFeedbackSetting; Windows 7: tablet property.
void DisableTouchFeedback(HWND hwnd);
// Windows 7 tablet service flags (tpcshrd.h): TABLET_DISABLE_PRESSANDHOLD,
// _PENTAPFEEDBACK, _PENBARRELFEEDBACK, _FLICKS, _FLICKFALLBACKKEYS.
constexpr DWORD kTabletGestureOff = 0x00000001 | 0x00000008 | 0x00000010 | 0x00010000 | 0x00100000;
constexpr UINT kWmTabletQuerySystemGestureStatus = 0x02CC;

// Effective DPI of a window: GetDpiForWindow (10+), GetDpiForMonitor (8.1+),
// otherwise the system DPI.
UINT DpiForWindow(HWND hwnd);
// DPI of the monitor nearest to a point, with the same fallbacks.
UINT DpiForPoint(POINT pt);

// True when mouse messages being processed were synthesized from touch or pen
// (Windows 7+ signature in GetMessageExtraInfo).
inline bool IsMouseFromTouchOrPen() {
  return (static_cast<ULONG_PTR>(GetMessageExtraInfo()) & 0xFFFFFF00) == 0xFF515700;
}

// Windows 10+: "Apps use light theme" = 0 in the Personalize key.
bool SystemPrefersDark();

}  // namespace t9ime::compat
