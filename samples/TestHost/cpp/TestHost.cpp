// TestHost (C++): shows how an application uses T9Ctl.dll.
//   - buttons for every T9Ctl call,
//   - a text box that shrinks so the touch keyboard never covers it
//     (T9_RegisterVisibilityNotify + T9_GetKeyboardRect),
//   - a status line with the keyboard state.
#include <windows.h>

#include <string>

#include "t9ctl.h"

// Visual styles for the buttons (common controls 6).
#pragma comment(linker, "\"/manifestdependency:type='win32' name='Microsoft.Windows.Common-Controls' version='6.0.0.0' processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")

namespace {

enum Command {
  kShow = 100, kHide, kToggle, kChinese, kEnglish, kNumber, kSymbol, kDock, kActivate, kDeactivate,
};

struct Button {
  int id;
  const wchar_t* text;
};
const Button kButtons[] = {
    {kShow, L"显示键盘"}, {kHide, L"隐藏键盘"},  {kToggle, L"切换"},       {kChinese, L"中文"},
    {kEnglish, L"英文"},   {kNumber, L"数字"},    {kSymbol, L"符号"},        {kDock, L"停靠底部"},
    {kActivate, L"切到 T9Ime"}, {kDeactivate, L"切走 T9Ime"},
};
constexpr int kButtonH = 32, kButtonW = 96, kStatusH = 24;

HWND g_edit = nullptr, g_status = nullptr;
HFONT g_font = nullptr;

void UpdateStatus(HWND hwnd) {
  RECT kb = {};
  const BOOL visible = T9_IsKeyboardVisible();
  T9_GetKeyboardRect(&kb);
  static const wchar_t* const kModes[] = {L"未运行", L"中文", L"英文", L"数字", L"符号"};
  const int mode = T9_GetMode();
  std::wstring s = std::wstring(L"键盘：") + (visible ? L"显示" : L"隐藏") + L"，布局：" +
                   kModes[mode >= 0 && mode <= 4 ? mode : 0] + L"，位置 " + std::to_wstring(kb.left) + L"," +
                   std::to_wstring(kb.top) + L" 大小 " + std::to_wstring(kb.right - kb.left) + L"×" +
                   std::to_wstring(kb.bottom - kb.top) + (T9_IsInstalled() ? L"" : L"（T9Ime 未安装）");
  SetWindowTextW(g_status, s.c_str());

  // Keep the text box above the keyboard.
  RECT client;
  GetClientRect(hwnd, &client);
  const int buttons_h = 2 * kButtonH + 8;
  int bottom = client.bottom - kStatusH;
  if (visible) {
    POINT top_left = {kb.left, kb.top};
    ScreenToClient(hwnd, &top_left);
    RECT screen_client = client;
    MapWindowPoints(hwnd, nullptr, reinterpret_cast<POINT*>(&screen_client), 2);
    const bool overlaps = kb.left < screen_client.right && kb.right > screen_client.left;
    if (overlaps && top_left.y > buttons_h && top_left.y < bottom) bottom = top_left.y - 4;
  }
  MoveWindow(g_edit, 4, buttons_h, client.right - 8, bottom - buttons_h, TRUE);
}

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
  if (msg == T9_GetVisibilityMessage()) {
    UpdateStatus(hwnd);
    return 0;
  }
  switch (msg) {
    case WM_CREATE: {
      NONCLIENTMETRICSW ncm = {sizeof(ncm)};
      SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof(ncm), &ncm, 0);
      ncm.lfMessageFont.lfHeight = ncm.lfMessageFont.lfHeight * 3 / 2;
      g_font = CreateFontIndirectW(&ncm.lfMessageFont);
      int i = 0;
      for (const Button& b : kButtons) {
        HWND h = CreateWindowW(L"BUTTON", b.text, WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 4 + (i % 5) * (kButtonW + 4),
                               4 + (i / 5) * kButtonH, kButtonW, kButtonH - 4, hwnd,
                               reinterpret_cast<HMENU>(static_cast<INT_PTR>(b.id)), nullptr, nullptr);
        SendMessageW(h, WM_SETFONT, reinterpret_cast<WPARAM>(g_font), TRUE);
        ++i;
      }
      g_edit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"", WS_CHILD | WS_VISIBLE | WS_VSCROLL | ES_MULTILINE,
                               0, 0, 0, 0, hwnd, nullptr, nullptr, nullptr);
      g_status = CreateWindowW(L"STATIC", L"", WS_CHILD | WS_VISIBLE, 0, 0, 0, 0, hwnd, nullptr, nullptr, nullptr);
      SendMessageW(g_edit, WM_SETFONT, reinterpret_cast<WPARAM>(g_font), TRUE);
      SendMessageW(g_status, WM_SETFONT, reinterpret_cast<WPARAM>(g_font), TRUE);
      T9_RegisterVisibilityNotify(hwnd);
      return 0;
    }
    case WM_SIZE: {
      MoveWindow(g_status, 4, HIWORD(lp) - kStatusH, LOWORD(lp) - 8, kStatusH, TRUE);
      UpdateStatus(hwnd);
      return 0;
    }
    case WM_MOVE:
      UpdateStatus(hwnd);
      return 0;
    case WM_COMMAND:
      switch (LOWORD(wp)) {
        case kShow: T9_ShowKeyboard(T9_MODE_KEEP); break;
        case kHide: T9_HideKeyboard(); break;
        case kToggle: T9_ToggleKeyboard(); break;
        case kChinese: T9_ShowKeyboard(T9_MODE_CHINESE); break;
        case kEnglish: T9_ShowKeyboard(T9_MODE_ENGLISH); break;
        case kNumber: T9_ShowKeyboard(T9_MODE_NUMBER); break;
        case kSymbol: T9_ShowKeyboard(T9_MODE_SYMBOL); break;
        case kDock: T9_SetDock(); break;
        case kActivate: T9_Activate(hwnd); break;
        case kDeactivate: T9_Deactivate(hwnd); break;
        default: return 0;
      }
      SetFocus(g_edit);  // the buttons took the focus; give it back to the text box
      UpdateStatus(hwnd);
      return 0;
    case WM_DESTROY:
      T9_UnregisterVisibilityNotify(hwnd);
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
  wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_BTNFACE + 1);
  wc.lpszClassName = L"T9Ime.TestHost";
  RegisterClassW(&wc);
  HWND hwnd = CreateWindowW(wc.lpszClassName, L"T9Ime TestHost (C++)", WS_OVERLAPPEDWINDOW, CW_USEDEFAULT,
                            CW_USEDEFAULT, 5 * (kButtonW + 4) + 24, 480, nullptr, nullptr, instance, nullptr);
  ShowWindow(hwnd, show);
  SetFocus(g_edit);
  MSG msg;
  while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
    TranslateMessage(&msg);
    DispatchMessageW(&msg);
  }
  return 0;
}
