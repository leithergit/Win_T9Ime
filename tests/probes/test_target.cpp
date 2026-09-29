// Test target for end-to-end tests: a top-level window with a multi-line Edit
// control that keeps the keyboard focus. Tests read the text with WM_GETTEXT.
#include <windows.h>

namespace {
HWND g_edit = nullptr;

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
  switch (msg) {
    case WM_CREATE:
      g_edit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
                               WS_CHILD | WS_VISIBLE | WS_VSCROLL | ES_MULTILINE | ES_AUTOVSCROLL | ES_WANTRETURN,
                               0, 0, 0, 0, hwnd, reinterpret_cast<HMENU>(1), nullptr, nullptr);
      SendMessageW(g_edit, WM_SETFONT, reinterpret_cast<WPARAM>(GetStockObject(DEFAULT_GUI_FONT)), TRUE);
      return 0;
    case WM_SIZE:
      MoveWindow(g_edit, 0, 0, LOWORD(lp), HIWORD(lp), TRUE);
      return 0;
    case WM_SETFOCUS:
      SetFocus(g_edit);
      return 0;
    case WM_DESTROY:
      PostQuitMessage(0);
      return 0;
  }
  return DefWindowProcW(hwnd, msg, wp, lp);
}
}  // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int show) {
  WNDCLASSW wc = {};
  wc.lpfnWndProc = WndProc;
  wc.hInstance = instance;
  wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
  wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
  wc.lpszClassName = L"T9Ime.TestTarget";
  RegisterClassW(&wc);
  HWND hwnd = CreateWindowExW(0, wc.lpszClassName, L"T9Ime TestTarget", WS_OVERLAPPEDWINDOW, 40, 40, 520, 260,
                              nullptr, nullptr, instance, nullptr);
  ShowWindow(hwnd, show);
  SetForegroundWindow(hwnd);
  MSG msg;
  while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
    TranslateMessage(&msg);
    DispatchMessageW(&msg);
  }
  return 0;
}
