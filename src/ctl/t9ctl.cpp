// T9Ctl.dll: the control API (t9ctl.h) over the host's control pipe.

#include "t9ctl.h"

#include <msctf.h>

#include <string>
#include <vector>

#include "ime_profile.h"
#include "pipe.h"
#include "protocol.h"

namespace {

using namespace t9ime;

constexpr DWORD kCallTimeoutMs = 5000;
constexpr DWORD kStartTimeoutMs = 8000;  // host start incl. first connection

HMODULE g_module = nullptr;

struct State {
  bool visible = false;
  int mode = 0;
  RECT rect = {};
};

std::wstring DirOf(const std::wstring& path) { return path.substr(0, path.find_last_of(L"\\/") + 1); }

bool Exists(const std::wstring& path) {
  const DWORD a = GetFileAttributesW(path.c_str());
  return a != INVALID_FILE_ATTRIBUTES && !(a & FILE_ATTRIBUTE_DIRECTORY);
}

// T9Host.exe next to this DLL, else next to the registered TIP.
std::wstring HostPath() {
  wchar_t buf[MAX_PATH];
  const DWORD n = GetModuleFileNameW(g_module, buf, MAX_PATH);
  std::wstring path = DirOf(std::wstring(buf, n)) + L"T9Host.exe";
  if (Exists(path)) return path;
  wchar_t clsid[64];
  StringFromGUID2(kClsidT9Tip, clsid, 64);
  const std::wstring key = std::wstring(L"CLSID\\") + clsid + L"\\InprocServer32";
  DWORD size = sizeof(buf);
  if (RegGetValueW(HKEY_CLASSES_ROOT, key.c_str(), nullptr, RRF_RT_REG_SZ, nullptr, buf, &size) == ERROR_SUCCESS) {
    path = DirOf(buf) + L"T9Host.exe";
    if (Exists(path)) return path;
  }
  return {};
}

bool StartHost() {
  const std::wstring path = HostPath();
  if (path.empty()) return false;
  std::wstring cmd = L"\"" + path + L"\" --background";
  STARTUPINFOW si = {sizeof(si)};
  PROCESS_INFORMATION pi = {};
  if (!CreateProcessW(path.c_str(), cmd.data(), nullptr, nullptr, FALSE, 0, nullptr, nullptr, &si, &pi)) {
    return false;
  }
  CloseHandle(pi.hThread);
  CloseHandle(pi.hProcess);
  return true;
}

bool Connect(ipc::PipeClient* pipe, bool start) {
  const std::wstring name = ipc::PipeName(ipc::Endpoint::kControl);
  if (pipe->Connect(name, 200)) return true;
  if (!start || !StartHost()) return false;
  const ULONGLONG deadline = GetTickCount64() + kStartTimeoutMs;
  while (GetTickCount64() < deadline) {
    if (pipe->Connect(name, 200)) return true;
    Sleep(100);
  }
  return false;
}

// One request on a fresh connection. `start`: launch T9Host if needed.
bool Call(ipc::Writer request, bool start, State* state = nullptr) {
  ipc::PipeClient pipe;
  if (!Connect(&pipe, start)) return false;
  std::vector<uint8_t> response;
  if (!pipe.Call(request.Finish(), &response, kCallTimeoutMs)) return false;
  const ipc::Reader r(response.data(), response.size());
  if (!r.ok() || r.type() != ipc::MsgType::kAck) return false;
  if (state) {
    state->visible = r.BoolOr(ipc::kTagVisible, false);
    state->mode = static_cast<int>(r.U32Or(ipc::kTagMode, 0));
    const int x = static_cast<int32_t>(r.U32Or(ipc::kTagX, 0)), y = static_cast<int32_t>(r.U32Or(ipc::kTagY, 0));
    state->rect = {x, y, x + static_cast<int32_t>(r.U32Or(ipc::kTagWidth, 0)),
                   y + static_cast<int32_t>(r.U32Or(ipc::kTagHeight, 0))};
  }
  return true;
}

ipc::Writer WithWindow(ipc::MsgType type, HWND hwnd) {
  ipc::Writer w(type);
  w.U32(ipc::kTagHwnd, static_cast<uint32_t>(reinterpret_cast<uintptr_t>(hwnd)));
  return w;
}

bool ValidMode(int mode) { return mode >= T9_MODE_CHINESE && mode <= T9_MODE_SYMBOL; }

HWND FocusOf(HWND window) {
  GUITHREADINFO gti = {sizeof(gti)};
  return GetGUIThreadInfo(GetWindowThreadProcessId(window, nullptr), &gti) && gti.hwndFocus ? gti.hwndFocus : window;
}

bool OnCallingThread(HWND hwnd) { return GetWindowThreadProcessId(hwnd, nullptr) == GetCurrentThreadId(); }

// The keyboard is shown for the calling application when the caller owns the
// foreground window (a button click in its UI): T9Host then switches that
// application to T9Ime too (D5). Not on this thread - see T9_Activate.
HWND CallerWindow() {
  HWND fg = GetForegroundWindow();
  return fg && OnCallingThread(fg) ? fg : nullptr;
}

}  // namespace

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID) {
  if (reason == DLL_PROCESS_ATTACH) {
    g_module = instance;
    DisableThreadLibraryCalls(instance);
  }
  return TRUE;
}

extern "C" {

BOOL T9_API T9_IsInstalled(void) {
  wchar_t clsid[64];
  StringFromGUID2(kClsidT9Tip, clsid, 64);
  const std::wstring key = std::wstring(L"CLSID\\") + clsid + L"\\InprocServer32";
  HKEY h = nullptr;
  if (RegOpenKeyExW(HKEY_CLASSES_ROOT, key.c_str(), 0, KEY_READ, &h) != ERROR_SUCCESS) return FALSE;
  RegCloseKey(h);
  return TRUE;
}

// Switching goes through T9Host (session profile + WM_INPUTLANGCHANGEREQUEST to
// the window), even for the caller's own window: activating a TSF profile on
// the caller's thread needs COM there, and initializing / uninitializing COM
// around it left T9Ime unable to notice being switched off again (Windows 7).
BOOL T9_API T9_Activate(HWND hwnd) {
  if (!hwnd) hwnd = GetForegroundWindow();
  if (!hwnd || !IsWindow(hwnd)) return FALSE;
  if (Call(WithWindow(ipc::MsgType::kCtlActivate, hwnd), true)) return TRUE;
  // No host: at least ask the window's thread for the T9Ime language.
  return PostMessageW(FocusOf(hwnd), WM_INPUTLANGCHANGEREQUEST, 0, reinterpret_cast<LPARAM>(T9LanguageHkl()));
}

BOOL T9_API T9_Deactivate(HWND hwnd) {
  if (!hwnd) hwnd = GetForegroundWindow();
  if (!hwnd || !IsWindow(hwnd)) return FALSE;
  if (Call(WithWindow(ipc::MsgType::kCtlDeactivate, hwnd), false)) return TRUE;
  // No host: switch the window's thread to the first other language.
  HKL layouts[16];
  const int n = GetKeyboardLayoutList(16, layouts);
  for (int i = 0; i < n; ++i) {
    if (LOWORD(reinterpret_cast<ULONG_PTR>(layouts[i])) != kT9LangId) {
      return PostMessageW(FocusOf(hwnd), WM_INPUTLANGCHANGEREQUEST, 0, reinterpret_cast<LPARAM>(layouts[i]));
    }
  }
  return FALSE;
}

BOOL T9_API T9_ShowKeyboard(int mode) {
  if (mode != T9_MODE_KEEP && !ValidMode(mode)) return FALSE;
  ipc::Writer w = WithWindow(ipc::MsgType::kCtlShow, CallerWindow());
  if (mode != T9_MODE_KEEP) w.U32(ipc::kTagMode, static_cast<uint32_t>(mode));
  return Call(std::move(w), true);
}

BOOL T9_API T9_HideKeyboard(void) {
  if (Call(ipc::Writer(ipc::MsgType::kCtlHide), false)) return TRUE;
  return !T9_IsKeyboardVisible();  // no host: nothing to hide
}

BOOL T9_API T9_ToggleKeyboard(void) { return Call(WithWindow(ipc::MsgType::kCtlToggle, CallerWindow()), true); }

BOOL T9_API T9_IsKeyboardVisible(void) {
  State s;
  return Call(ipc::Writer(ipc::MsgType::kCtlQuery), false, &s) && s.visible;
}

BOOL T9_API T9_SetMode(int mode) {
  if (!ValidMode(mode)) return FALSE;
  ipc::Writer w(ipc::MsgType::kCtlSetMode);
  w.U32(ipc::kTagMode, static_cast<uint32_t>(mode));
  return Call(std::move(w), true);
}

int T9_API T9_GetMode(void) {
  State s;
  return Call(ipc::Writer(ipc::MsgType::kCtlQuery), false, &s) ? s.mode : 0;
}

BOOL T9_API T9_SetDock(void) { return Call(ipc::Writer(ipc::MsgType::kCtlDock), true); }

BOOL T9_API T9_SetPosition(int x, int y) {
  ipc::Writer w(ipc::MsgType::kCtlSetPosition);
  w.I32(ipc::kTagX, x).I32(ipc::kTagY, y);
  return Call(std::move(w), true);
}

BOOL T9_API T9_GetKeyboardRect(RECT* rect) {
  if (!rect) return FALSE;
  State s;
  if (!Call(ipc::Writer(ipc::MsgType::kCtlQuery), false, &s)) return FALSE;
  *rect = s.rect;
  return TRUE;
}

UINT T9_API T9_GetVisibilityMessage(void) {
  static const UINT msg = RegisterWindowMessageW(L"T9Ime.Visibility");
  return msg;
}

BOOL T9_API T9_RegisterVisibilityNotify(HWND hwnd) {
  if (!hwnd || !IsWindow(hwnd)) return FALSE;
  // An elevated caller's window would not receive messages from T9Host (UIPI).
  ChangeWindowMessageFilterEx(hwnd, T9_GetVisibilityMessage(), MSGFLT_ALLOW, nullptr);
  return Call(WithWindow(ipc::MsgType::kCtlRegisterNotify, hwnd), true);
}

BOOL T9_API T9_UnregisterVisibilityNotify(HWND hwnd) {
  return Call(WithWindow(ipc::MsgType::kCtlUnregisterNotify, hwnd), false);
}

}  // extern "C"
