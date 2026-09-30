// t9ctl.exe: command line front end of T9Ctl.dll.
//
//   t9ctl show [chinese|english|number|symbol]   show the touch keyboard
//   t9ctl hide | toggle | dock
//   t9ctl mode chinese|english|number|symbol
//   t9ctl pos <x> <y>                              move (screen pixels, top-left)
//   t9ctl status                                   visible=0|1 mode=<m> rect=x,y,w,h
//   t9ctl activate [hwnd] | deactivate [hwnd]      switch an application to / from T9Ime
//   t9ctl installed
//   t9ctl watch <seconds>                          print visibility notifications
//
// Exit code: 0 success, 1 failure, 2 usage.

#include <windows.h>

#include <cstdio>
#include <cstdlib>
#include <cwchar>

#include "t9ctl.h"

namespace {

const wchar_t* const kModes[] = {L"", L"chinese", L"english", L"number", L"symbol"};

int ModeOf(const wchar_t* name) {
  for (int i = 1; i <= 4; ++i) {
    if (_wcsicmp(name, kModes[i]) == 0) return i;
  }
  return -1;
}

void PrintStatus() {
  RECT r = {};
  T9_GetKeyboardRect(&r);
  const int mode = T9_GetMode();
  std::printf("visible=%d mode=%ls rect=%ld,%ld,%ld,%ld\n", T9_IsKeyboardVisible() ? 1 : 0,
              mode >= 1 && mode <= 4 ? kModes[mode] : L"none", r.left, r.top, r.right - r.left, r.bottom - r.top);
  std::fflush(stdout);
}

HWND ParseWindow(int argc, wchar_t** argv, int index) {
  return index < argc ? reinterpret_cast<HWND>(static_cast<uintptr_t>(std::wcstoull(argv[index], nullptr, 0)))
                      : nullptr;
}

// Registers a message-only window and prints each notification.
int Watch(int seconds) {
  WNDCLASSW wc = {};
  wc.lpfnWndProc = [](HWND h, UINT m, WPARAM w, LPARAM l) -> LRESULT {
    if (m == T9_GetVisibilityMessage()) {
      RECT r = {};
      T9_GetKeyboardRect(&r);
      std::printf("notify visible=%d rect=%ld,%ld,%ld,%ld\n", static_cast<int>(w), r.left, r.top, r.right - r.left,
                  r.bottom - r.top);
      std::fflush(stdout);
      return 0;
    }
    return DefWindowProcW(h, m, w, l);
  };
  wc.hInstance = GetModuleHandleW(nullptr);
  wc.lpszClassName = L"T9Ime.CtlWatch";
  RegisterClassW(&wc);
  HWND hwnd = CreateWindowExW(0, wc.lpszClassName, L"", 0, 0, 0, 0, 0, HWND_MESSAGE, nullptr, wc.hInstance, nullptr);
  if (!hwnd || !T9_RegisterVisibilityNotify(hwnd)) {
    std::printf("watch: cannot register\n");
    return 1;
  }
  std::printf("watching\n");
  std::fflush(stdout);
  SetTimer(hwnd, 1, static_cast<UINT>(seconds) * 1000, nullptr);
  MSG msg;
  while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
    if (msg.message == WM_TIMER) break;
    DispatchMessageW(&msg);
  }
  T9_UnregisterVisibilityNotify(hwnd);
  DestroyWindow(hwnd);
  return 0;
}

int Usage() {
  std::printf(
      "usage: t9ctl show [chinese|english|number|symbol] | hide | toggle | dock | mode <m> |\n"
      "             pos <x> <y> | status | activate [hwnd] | deactivate [hwnd] | installed | watch <seconds>\n");
  return 2;
}

}  // namespace

int wmain(int argc, wchar_t** argv) {
  if (argc < 2) return Usage();
  const wchar_t* cmd = argv[1];
  auto result = [](BOOL ok) { return ok ? 0 : 1; };
  if (_wcsicmp(cmd, L"show") == 0) {
    const int mode = argc > 2 ? ModeOf(argv[2]) : T9_MODE_KEEP;
    if (mode < 0) return Usage();
    return result(T9_ShowKeyboard(mode));
  }
  if (_wcsicmp(cmd, L"hide") == 0) return result(T9_HideKeyboard());
  if (_wcsicmp(cmd, L"toggle") == 0) return result(T9_ToggleKeyboard());
  if (_wcsicmp(cmd, L"dock") == 0) return result(T9_SetDock());
  if (_wcsicmp(cmd, L"mode") == 0) {
    const int mode = argc > 2 ? ModeOf(argv[2]) : -1;
    if (mode < 0) return Usage();
    return result(T9_SetMode(mode));
  }
  if (_wcsicmp(cmd, L"pos") == 0) {
    if (argc < 4) return Usage();
    return result(T9_SetPosition(_wtoi(argv[2]), _wtoi(argv[3])));
  }
  if (_wcsicmp(cmd, L"status") == 0) {
    PrintStatus();
    return 0;
  }
  if (_wcsicmp(cmd, L"activate") == 0) return result(T9_Activate(ParseWindow(argc, argv, 2)));
  if (_wcsicmp(cmd, L"deactivate") == 0) return result(T9_Deactivate(ParseWindow(argc, argv, 2)));
  if (_wcsicmp(cmd, L"installed") == 0) {
    const BOOL installed = T9_IsInstalled();
    std::printf("installed=%d\n", installed ? 1 : 0);
    return result(installed);
  }
  if (_wcsicmp(cmd, L"watch") == 0) return Watch(argc > 2 ? _wtoi(argv[2]) : 60);
  return Usage();
}
