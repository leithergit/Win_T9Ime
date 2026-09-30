// Test target for end-to-end tests: a top-level window with a multi-line Edit
// control that keeps the keyboard focus. Tests read the text with WM_GETTEXT.
//   --activate-tip   switch this process to the T9Ime TSF profile
#include <windows.h>
#include <msctf.h>
#include <shellapi.h>

namespace {
HWND g_edit = nullptr;
WNDPROC g_edit_proc = nullptr;

// The stock multi-line Edit ignores Ctrl+A; handle it so tests can check that
// Ctrl shortcuts reach the application.
LRESULT CALLBACK EditProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
  if (msg == WM_KEYDOWN && wp == 'A' && (GetKeyState(VK_CONTROL) & 0x8000)) {
    SendMessageW(hwnd, EM_SETSEL, 0, -1);
    return 0;
  }
  if (msg == WM_CHAR && wp == 1) return 0;  // swallow the Ctrl+A character (beep)
  return CallWindowProcW(g_edit_proc, hwnd, msg, wp, lp);
}

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
  switch (msg) {
    case WM_CREATE:
      g_edit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
                               WS_CHILD | WS_VISIBLE | WS_VSCROLL | ES_MULTILINE | ES_AUTOVSCROLL | ES_WANTRETURN,
                               0, 0, 0, 0, hwnd, reinterpret_cast<HMENU>(1), nullptr, nullptr);
      SendMessageW(g_edit, WM_SETFONT, reinterpret_cast<WPARAM>(GetStockObject(DEFAULT_GUI_FONT)), TRUE);
      g_edit_proc = reinterpret_cast<WNDPROC>(
          SetWindowLongPtrW(g_edit, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(EditProc)));
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

// {BB2F3BA4-3B7A-414A-A98F-08C100E5447E} / {3EA4FF8C-CAF7-456F-9542-1C0F1EF2635E}
const CLSID kClsidT9Tip = {0xbb2f3ba4, 0x3b7a, 0x414a, {0xa9, 0x8f, 0x08, 0xc1, 0x00, 0xe5, 0x44, 0x7e}};
const GUID kGuidT9Profile = {0x3ea4ff8c, 0xcaf7, 0x456f, {0x95, 0x42, 0x1c, 0x0f, 0x1e, 0xf2, 0x63, 0x5e}};

bool ActivateT9Tip() {
  ITfInputProcessorProfileMgr* mgr = nullptr;
  if (FAILED(CoCreateInstance(CLSID_TF_InputProcessorProfiles, nullptr, CLSCTX_INPROC_SERVER,
                              IID_ITfInputProcessorProfileMgr, reinterpret_cast<void**>(&mgr)))) {
    return false;
  }
  const HRESULT hr = mgr->ActivateProfile(TF_PROFILETYPE_INPUTPROCESSOR, MAKELANGID(LANG_CHINESE, SUBLANG_CHINESE_SIMPLIFIED),
                                          kClsidT9Tip, kGuidT9Profile, nullptr,
                                          TF_IPPMF_FORPROCESS | TF_IPPMF_DONTCARECURRENTINPUTLANGUAGE);
  mgr->Release();
  return SUCCEEDED(hr);
}

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int show) {
  int argc = 0;
  LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
  bool activate = false;
  for (int i = 1; i < argc; ++i) activate |= lstrcmpW(argv[i], L"--activate-tip") == 0;
  LocalFree(argv);
  CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
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
  if (activate) SetWindowTextW(hwnd, ActivateT9Tip() ? L"T9Ime TestTarget [tip]" : L"T9Ime TestTarget [tip failed]");
  MSG msg;
  while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
    TranslateMessage(&msg);
    DispatchMessageW(&msg);
  }
  CoUninitialize();
  return 0;
}
