#pragma once
// Interaction with the system's input machinery:
//   - switching the focused application to the T9Ime profile (SPEC U6),
//   - the Windows touch keyboard (TabTip / InputPane) (SPEC §6, U5).

#include <windows.h>

namespace t9ime {

// Asks Windows to make T9Ime the active input method of the foreground
// application: session-wide profile activation, then (if the application
// still has no T9Ime connection after a moment) WM_INPUTLANGCHANGEREQUEST to
// its focus window. Rate limited per window. UI thread (COM initialized).
class ImeSwitcher {
 public:
  // Step 1. Returns false when it was attempted for this window very recently.
  bool Begin(HWND foreground);
  // Step 2 (call ~300 ms later if the TIP did not connect).
  void Fallback(HWND foreground);

 private:
  HWND last_window_ = nullptr;
  ULONGLONG last_tick_ = 0;
};

namespace touch_keyboard {

// True if the Windows touch keyboard is on screen (Windows 8+; false on 7).
bool IsVisible();

// Automatic invocation of the Windows touch keyboard (Windows 10/11 settings
// under HKCU\Software\Microsoft\TabletTip\1.7). TakeOver() backs up the
// current values and turns auto-invoke off; Restore() puts them back.
bool IsTakenOver();
void TakeOver();
void Restore();

}  // namespace touch_keyboard

}  // namespace t9ime
