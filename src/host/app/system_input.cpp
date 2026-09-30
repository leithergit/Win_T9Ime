#include "system_input.h"

#include <msctf.h>

#include <iterator>
#include <mutex>
#include <string>

#include "win_compat.h"

namespace t9ime {

namespace {

// Same identifiers as src/tip/globals.cpp.
const CLSID kClsidT9Tip = {0xbb2f3ba4, 0x3b7a, 0x414a, {0xa9, 0x8f, 0x08, 0xc1, 0x00, 0xe5, 0x44, 0x7e}};
const GUID kGuidT9Profile = {0x3ea4ff8c, 0xcaf7, 0x456f, {0x95, 0x42, 0x1c, 0x0f, 0x1e, 0xf2, 0x63, 0x5e}};
constexpr LANGID kLangId = MAKELANGID(LANG_CHINESE, SUBLANG_CHINESE_SIMPLIFIED);
constexpr ULONGLONG kRetryMs = 3000;

// IFrameworkInputPane (shobjidl_core.h, Windows 8 SDK) declared locally: the
// SDK hides it below _WIN32_WINNT 0x0602.
const CLSID kClsidFrameworkInputPane = {0xD5120AA3, 0x46BA, 0x44C5, {0x82, 0x2D, 0xCA, 0x80, 0x92, 0xC1, 0xFC, 0x72}};
const IID kIidFrameworkInputPane = {0x5752238B, 0x24F0, 0x495A, {0x82, 0xF1, 0x2F, 0xD5, 0x93, 0x05, 0x67, 0x96}};
struct IFrameworkInputPane : IUnknown {
  virtual HRESULT STDMETHODCALLTYPE Advise(IUnknown*, IUnknown*, DWORD*) = 0;
  virtual HRESULT STDMETHODCALLTYPE AdviseWithHWND(HWND, IUnknown*, DWORD*) = 0;
  virtual HRESULT STDMETHODCALLTYPE Unadvise(DWORD) = 0;
  virtual HRESULT STDMETHODCALLTYPE Location(RECT*) = 0;
};

constexpr wchar_t kTabTipKey[] = L"Software\\Microsoft\\TabletTip\\1.7";
constexpr wchar_t kBackupKey[] = L"Software\\T9Ime\\TouchKeyboardBackup";
// Windows 11: "Show the touch keyboard" (0 never, 1 when no keyboard, 2 always).
// Windows 10: show automatically in desktop mode without a keyboard.
constexpr const wchar_t* kTabTipValues[] = {L"TouchKeyboardTapInvoke", L"EnableDesktopModeAutoInvoke"};
// Windows 7 Tablet PC Input Panel (user policies): no icon next to text boxes
// for touch / pen, no tab at the screen edge.
constexpr wchar_t kTipPolicyKey[] = L"Software\\Policies\\Microsoft\\TabletTip\\1.7";
constexpr const wchar_t* kTipPolicyValues[] = {L"HideIPTIPTouchTarget", L"HideIPTIPTarget", L"DisableEdgeTarget"};
constexpr DWORD kMissing = 0xFFFFFFFF;  // backup marker: the value did not exist

}  // namespace

bool ImeSwitcher::Begin(HWND foreground) {
  const ULONGLONG now = GetTickCount64();
  if (foreground == last_window_ && now - last_tick_ < kRetryMs) return false;
  last_window_ = foreground;
  last_tick_ = now;
  ITfInputProcessorProfileMgr* mgr = nullptr;
  if (FAILED(CoCreateInstance(CLSID_TF_InputProcessorProfiles, nullptr, CLSCTX_INPROC_SERVER,
                              IID_ITfInputProcessorProfileMgr, reinterpret_cast<void**>(&mgr)))) {
    return true;
  }
  // FORSESSION: all threads of this desktop (the default "per user" input
  // method mode then carries it to the foreground application).
  mgr->ActivateProfile(TF_PROFILETYPE_INPUTPROCESSOR, kLangId, kClsidT9Tip, kGuidT9Profile, nullptr,
                       TF_IPPMF_FORSESSION | TF_IPPMF_DONTCARECURRENTINPUTLANGUAGE);
  mgr->Release();
  return true;
}

void ImeSwitcher::Fallback(HWND foreground) {
  // Ask the focus window to change its input language to Chinese (Simplified);
  // with the profile marked above, that language comes up with T9Ime.
  GUITHREADINFO gti = {sizeof(gti)};
  const DWORD tid = GetWindowThreadProcessId(foreground, nullptr);
  HWND target = GetGUIThreadInfo(tid, &gti) && gti.hwndFocus ? gti.hwndFocus : foreground;
  PostMessageW(target, WM_INPUTLANGCHANGEREQUEST, 0, reinterpret_cast<LPARAM>(reinterpret_cast<HKL>(0x08040804)));
}

namespace foreground {

namespace {
constexpr ULONGLONG kSettleMs = 1500;
std::mutex g_mutex;
HWND g_current = nullptr, g_previous = nullptr, g_before = nullptr;
ULONGLONG g_changed = 0;
HWINEVENTHOOK g_hook = nullptr;

void CALLBACK OnForeground(HWINEVENTHOOK, DWORD, HWND hwnd, LONG object, LONG, DWORD, DWORD) {
  if (object != OBJID_WINDOW || !hwnd) return;
  std::lock_guard lock(g_mutex);
  if (hwnd == g_current) return;
  g_before = g_previous;
  g_previous = g_current;
  g_current = hwnd;
  g_changed = GetTickCount64();
}
}  // namespace

void Start() {
  if (g_hook) return;
  {
    std::lock_guard lock(g_mutex);
    g_current = GetForegroundWindow();
  }
  g_hook = SetWinEventHook(EVENT_SYSTEM_FOREGROUND, EVENT_SYSTEM_FOREGROUND, nullptr, OnForeground, 0, 0,
                           WINEVENT_OUTOFCONTEXT | WINEVENT_SKIPOWNPROCESS);
}

void Stop() {
  if (g_hook) UnhookWinEvent(g_hook);
  g_hook = nullptr;
}

bool SettledIn(DWORD tid) {
  HWND fg = GetForegroundWindow();
  if (!fg || GetWindowThreadProcessId(fg, nullptr) != tid) return false;
  std::lock_guard lock(g_mutex);
  if (fg != g_current) return false;  // the hook has not seen this change yet
  return GetTickCount64() - g_changed >= kSettleMs || g_current == g_before;
}

}  // namespace foreground

namespace touch_keyboard {

bool IsVisible() {
  if (!compat::Os().AtLeastWin10()) return false;
  IFrameworkInputPane* pane = nullptr;
  if (FAILED(CoCreateInstance(kClsidFrameworkInputPane, nullptr, CLSCTX_INPROC_SERVER, kIidFrameworkInputPane,
                              reinterpret_cast<void**>(&pane)))) {
    return false;
  }
  RECT r = {};
  const bool visible = SUCCEEDED(pane->Location(&r)) && r.right > r.left && r.bottom > r.top;
  pane->Release();
  return visible;
}

bool IsTakenOver() {
  HKEY key = nullptr;
  if (RegOpenKeyExW(HKEY_CURRENT_USER, kBackupKey, 0, KEY_READ, &key) != ERROR_SUCCESS) return false;
  RegCloseKey(key);
  return true;
}

namespace {

// Backs up `names` of `key_path` into the backup key (prefix `tag`) and sets them to `value`.
void BackupAndSet(const wchar_t* key_path, const wchar_t* const* names, size_t count, DWORD value, HKEY backup,
                  const std::wstring& tag) {
  HKEY key = nullptr;
  if (RegCreateKeyExW(HKEY_CURRENT_USER, key_path, 0, nullptr, 0, KEY_READ | KEY_WRITE, nullptr, &key, nullptr) !=
      ERROR_SUCCESS) {
    return;
  }
  for (size_t i = 0; i < count; ++i) {
    DWORD old = kMissing, size = sizeof(old);
    if (RegGetValueW(key, nullptr, names[i], RRF_RT_REG_DWORD, nullptr, &old, &size) != ERROR_SUCCESS) old = kMissing;
    const std::wstring backup_name = tag + names[i];
    RegSetValueExW(backup, backup_name.c_str(), 0, REG_DWORD, reinterpret_cast<const BYTE*>(&old), sizeof(old));
    RegSetValueExW(key, names[i], 0, REG_DWORD, reinterpret_cast<const BYTE*>(&value), sizeof(value));
  }
  RegCloseKey(key);
}

void RestoreValues(const wchar_t* key_path, const wchar_t* const* names, size_t count, HKEY backup,
                   const std::wstring& tag) {
  HKEY key = nullptr;
  if (RegOpenKeyExW(HKEY_CURRENT_USER, key_path, 0, KEY_WRITE, &key) != ERROR_SUCCESS) return;
  for (size_t i = 0; i < count; ++i) {
    DWORD old = kMissing, size = sizeof(old);
    const std::wstring backup_name = tag + names[i];
    if (RegGetValueW(backup, nullptr, backup_name.c_str(), RRF_RT_REG_DWORD, nullptr, &old, &size) != ERROR_SUCCESS) {
      continue;
    }
    if (old == kMissing) {
      RegDeleteValueW(key, names[i]);
    } else {
      RegSetValueExW(key, names[i], 0, REG_DWORD, reinterpret_cast<const BYTE*>(&old), sizeof(old));
    }
  }
  RegCloseKey(key);
}

// The Input Panel process reads its settings at start: restart it (it comes
// back on demand).
void RestartInputPanel() {
  HWND tip = FindWindowW(L"IPTip_Main_Window", nullptr);
  if (!tip) return;
  DWORD pid = 0;
  GetWindowThreadProcessId(tip, &pid);
  if (HANDLE p = OpenProcess(PROCESS_TERMINATE, FALSE, pid)) {
    TerminateProcess(p, 0);
    CloseHandle(p);
  }
}

}  // namespace

void TakeOver() {
  if (IsTakenOver()) return;
  HKEY policy_backup = nullptr;
  if (RegCreateKeyExW(HKEY_CURRENT_USER, kBackupKey, 0, nullptr, 0, KEY_WRITE, nullptr, &policy_backup, nullptr) ==
      ERROR_SUCCESS) {
    BackupAndSet(kTipPolicyKey, kTipPolicyValues, std::size(kTipPolicyValues), 1, policy_backup, L"policy.");
    RegCloseKey(policy_backup);
  }
  RestartInputPanel();
  HKEY tabtip = nullptr, backup = nullptr;
  if (RegCreateKeyExW(HKEY_CURRENT_USER, kTabTipKey, 0, nullptr, 0, KEY_READ | KEY_WRITE, nullptr, &tabtip, nullptr) !=
      ERROR_SUCCESS) {
    return;
  }
  if (RegCreateKeyExW(HKEY_CURRENT_USER, kBackupKey, 0, nullptr, 0, KEY_WRITE, nullptr, &backup, nullptr) ==
      ERROR_SUCCESS) {
    for (const wchar_t* name : kTabTipValues) {
      DWORD value = kMissing, size = sizeof(value);
      if (RegGetValueW(tabtip, nullptr, name, RRF_RT_REG_DWORD, nullptr, &value, &size) != ERROR_SUCCESS) {
        value = kMissing;
      }
      RegSetValueExW(backup, name, 0, REG_DWORD, reinterpret_cast<const BYTE*>(&value), sizeof(value));
      const DWORD off = 0;
      RegSetValueExW(tabtip, name, 0, REG_DWORD, reinterpret_cast<const BYTE*>(&off), sizeof(off));
    }
    RegCloseKey(backup);
  }
  RegCloseKey(tabtip);
}

void Restore() {
  HKEY backup = nullptr, tabtip = nullptr;
  if (RegOpenKeyExW(HKEY_CURRENT_USER, kBackupKey, 0, KEY_READ, &backup) != ERROR_SUCCESS) return;
  RestoreValues(kTipPolicyKey, kTipPolicyValues, std::size(kTipPolicyValues), backup, L"policy.");
  RestartInputPanel();
  if (RegOpenKeyExW(HKEY_CURRENT_USER, kTabTipKey, 0, KEY_WRITE, &tabtip) == ERROR_SUCCESS) {
    for (const wchar_t* name : kTabTipValues) {
      DWORD value = kMissing, size = sizeof(value);
      if (RegGetValueW(backup, nullptr, name, RRF_RT_REG_DWORD, nullptr, &value, &size) != ERROR_SUCCESS) continue;
      if (value == kMissing) {
        RegDeleteValueW(tabtip, name);
      } else {
        RegSetValueExW(tabtip, name, 0, REG_DWORD, reinterpret_cast<const BYTE*>(&value), sizeof(value));
      }
    }
    RegCloseKey(tabtip);
  }
  RegCloseKey(backup);
  RegDeleteKeyW(HKEY_CURRENT_USER, kBackupKey);
}

}  // namespace touch_keyboard

}  // namespace t9ime
