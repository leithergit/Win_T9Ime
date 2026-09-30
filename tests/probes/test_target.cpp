// Test target for end-to-end tests: a top-level window with a multi-line Edit
// control that keeps the keyboard focus. Tests read the text with WM_GETTEXT.
// Below it: an Edit with InputScope IS_NUMBER and a password Edit.
//   --activate-tip       switch this process to the T9Ime TSF profile
//   --activate-english   switch this process to the US English keyboard
// Posting WM_APP + 1 to the window switches to T9Ime later (a user's switch).
// Either way the previous input method is restored on close.
#include <windows.h>
#include <msctf.h>
#include <shellapi.h>

void RestorePrevious();
bool ActivateProfile(bool t9ime);

namespace {
HWND g_edit = nullptr;
HWND g_number = nullptr;
HWND g_password = nullptr;
HWND g_password_mirror = nullptr;  // hidden Static with the password text (tests read it)
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
      // Second field: InputScope IS_NUMBER; third: a password field.
      g_number = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"", WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL, 0, 0, 0, 0,
                                 hwnd, reinterpret_cast<HMENU>(2), nullptr, nullptr);
      g_password = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"", WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL | ES_PASSWORD,
                                   0, 0, 0, 0, hwnd, reinterpret_cast<HMENU>(3), nullptr, nullptr);
      g_password_mirror = CreateWindowExW(0, L"STATIC", L"", WS_CHILD, 0, 0, 0, 0, hwnd, reinterpret_cast<HMENU>(4),
                                          nullptr, nullptr);
      if (HMODULE msctf = LoadLibraryW(L"msctf.dll")) {
        using SetInputScopeFn = HRESULT(WINAPI*)(HWND, int);
        if (auto fn = reinterpret_cast<SetInputScopeFn>(GetProcAddress(msctf, "SetInputScope"))) {
          fn(g_number, 29);  // IS_NUMBER
        }
      }
      return 0;
    case WM_SIZE: {
      const int w = LOWORD(lp), h = HIWORD(lp), row = 28;
      MoveWindow(g_edit, 0, 0, w, h - 2 * row, TRUE);
      MoveWindow(g_number, 0, h - 2 * row, w, row, TRUE);
      MoveWindow(g_password, 0, h - row, w, row, TRUE);
      return 0;
    }
    case WM_SETFOCUS:
      SetFocus(g_edit);
      return 0;
    case WM_COMMAND:
      if (LOWORD(wp) == 3 && HIWORD(wp) == EN_CHANGE) {
        wchar_t text[256] = {};
        GetWindowTextW(g_password, text, 256);
        SetWindowTextW(g_password_mirror, text);
      }
      return 0;
    case WM_APP + 1:
      SetWindowTextW(hwnd, ActivateProfile(true) ? L"T9Ime TestTarget [tip]" : L"T9Ime TestTarget [tip failed]");
      return 0;
    case WM_CLOSE:
      RestorePrevious();  // still in the foreground: restores the user's input method
      DestroyWindow(hwnd);
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

// With the default per-user input method mode (Windows 8+), activating a
// profile in the foreground process switches the user's input method
// everywhere. Remember the previous profile and restore it on exit.
TF_INPUTPROCESSORPROFILE g_previous = {};
bool g_have_previous = false;

void RestorePrevious() {
  if (!g_have_previous) return;
  ITfInputProcessorProfileMgr* mgr = nullptr;
  if (FAILED(CoCreateInstance(CLSID_TF_InputProcessorProfiles, nullptr, CLSCTX_INPROC_SERVER,
                              IID_ITfInputProcessorProfileMgr, reinterpret_cast<void**>(&mgr)))) {
    return;
  }
  mgr->ActivateProfile(g_previous.dwProfileType, g_previous.langid, g_previous.clsid, g_previous.guidProfile,
                       g_previous.hkl, TF_IPPMF_FORPROCESS);
  mgr->Release();
  g_have_previous = false;
}

bool ActivateProfile(bool t9ime) {
  ITfInputProcessorProfileMgr* mgr = nullptr;
  if (FAILED(CoCreateInstance(CLSID_TF_InputProcessorProfiles, nullptr, CLSCTX_INPROC_SERVER,
                              IID_ITfInputProcessorProfileMgr, reinterpret_cast<void**>(&mgr)))) {
    return false;
  }
  g_have_previous = SUCCEEDED(mgr->GetActiveProfile(GUID_TFCAT_TIP_KEYBOARD, &g_previous));
  const HRESULT hr =
      t9ime ? mgr->ActivateProfile(TF_PROFILETYPE_INPUTPROCESSOR, MAKELANGID(LANG_CHINESE, SUBLANG_CHINESE_SIMPLIFIED),
                                   kClsidT9Tip, kGuidT9Profile, nullptr,
                                   TF_IPPMF_FORPROCESS | TF_IPPMF_DONTCARECURRENTINPUTLANGUAGE)
            : mgr->ActivateProfile(TF_PROFILETYPE_KEYBOARDLAYOUT, 0x0409, GUID_NULL, GUID_NULL,
                                   reinterpret_cast<HKL>(static_cast<uintptr_t>(0x04090409)), TF_IPPMF_FORPROCESS);
  mgr->Release();
  return SUCCEEDED(hr);
}

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int show) {
  int argc = 0;
  LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
  bool activate = false, english = false;
  for (int i = 1; i < argc; ++i) {
    activate |= lstrcmpW(argv[i], L"--activate-tip") == 0;
    english |= lstrcmpW(argv[i], L"--activate-english") == 0;
  }
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
  if (activate) SetWindowTextW(hwnd, ActivateProfile(true) ? L"T9Ime TestTarget [tip]" : L"T9Ime TestTarget [tip failed]");
  if (english) {
    // Windows 7: TSF refuses a keyboard-layout profile of another language here;
    // the classic API switches the thread (and records the previous one first).
    bool ok = ActivateProfile(false);
    if (!ok) ok = ActivateKeyboardLayout(LoadKeyboardLayoutW(L"00000409", 0), KLF_SETFORPROCESS) != nullptr;
    SetWindowTextW(hwnd, ok ? L"T9Ime TestTarget [en]" : L"T9Ime TestTarget [en failed]");
  }
  MSG msg;
  while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
    TranslateMessage(&msg);
    DispatchMessageW(&msg);
  }
  CoUninitialize();
  return 0;
}
