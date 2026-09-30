// T9Host: per-session host process. Owns librime (engine thread), the touch
// panel and the tray icon.
//
//   T9Host.exe [--data <dir>] [--user <dir>] [--settings <ini>] [--show]
//              [--input pointer|touch|mouse] [--dump-layout <file>] [--no-single-instance]
//              [--pipe <request pipe name>]

#include <windows.h>
#include <sddl.h>
#include <shellapi.h>
#include <shlobj.h>

#include <memory>
#include <string>

#include "engine_thread.h"
#include "panel_window.h"
#include "request_server.h"
#include "text_output.h"
#include "win_compat.h"

namespace {

using namespace t9ime;

constexpr wchar_t kHostClass[] = L"T9Ime.Host";
constexpr UINT kTrayMessage = WM_APP + 10;
constexpr UINT kTrayId = 1;
constexpr UINT kCmdToggle = 100;
constexpr UINT kCmdExit = 101;
constexpr UINT kCmdDock = 102;

struct Args {
  std::wstring data, user, settings, dump_layout, pipe_name;
  panel::InputMode input = compat::HasPointerInput() ? panel::InputMode::kPointer : panel::InputMode::kTouch;
  bool show = false;
  bool single_instance = true;
};

std::wstring ExeDir() {
  wchar_t path[MAX_PATH];
  const DWORD n = GetModuleFileNameW(nullptr, path, MAX_PATH);
  std::wstring s(path, n);
  return s.substr(0, s.find_last_of(L"\\/"));
}

std::wstring AppDataDir() {
  PWSTR p = nullptr;
  std::wstring dir;
  if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_RoamingAppData, 0, nullptr, &p))) dir = p;
  CoTaskMemFree(p);
  return dir + L"\\T9Ime";
}

std::wstring UserSid() {
  HANDLE token = nullptr;
  std::wstring sid;
  if (OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token)) {
    BYTE buf[256];
    DWORD len = 0;
    if (GetTokenInformation(token, TokenUser, buf, sizeof(buf), &len)) {
      LPWSTR s = nullptr;
      if (ConvertSidToStringSidW(reinterpret_cast<TOKEN_USER*>(buf)->User.Sid, &s)) {
        sid = s;
        LocalFree(s);
      }
    }
    CloseHandle(token);
  }
  return sid;
}

Args ParseArgs() {
  Args a;
  int argc = 0;
  LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
  for (int i = 1; i < argc; ++i) {
    const std::wstring k = argv[i];
    auto next = [&] { return i + 1 < argc ? std::wstring(argv[++i]) : std::wstring(); };
    if (k == L"--data") a.data = next();
    else if (k == L"--user") a.user = next();
    else if (k == L"--settings") a.settings = next();
    else if (k == L"--dump-layout") a.dump_layout = next();
    else if (k == L"--pipe") a.pipe_name = next();
    else if (k == L"--show") a.show = true;
    else if (k == L"--no-single-instance") a.single_instance = false;
    else if (k == L"--input") {
      const std::wstring v = next();
      a.input = v == L"mouse" ? panel::InputMode::kMouse
              : v == L"touch" ? panel::InputMode::kTouch
                              : panel::InputMode::kPointer;
    }
  }
  LocalFree(argv);
  const std::wstring appdata = AppDataDir();
  if (a.data.empty()) {
    // Installed layout: <dir>\data. Test packages keep x86\ and x64\ next to
    // a shared data\ directory.
    a.data = ExeDir() + L"\\data";
    if (GetFileAttributesW(a.data.c_str()) == INVALID_FILE_ATTRIBUTES) a.data = ExeDir() + L"\\..\\data";
  }
  if (a.user.empty()) a.user = appdata + L"\\Rime";
  if (a.settings.empty()) {
    CreateDirectoryW(appdata.c_str(), nullptr);
    a.settings = appdata + L"\\panel.ini";
  }
  return a;
}

class HostApp {
 public:
  bool Init(HINSTANCE instance, const Args& args) {
    WNDCLASSEXW wc = {sizeof(wc)};
    wc.lpfnWndProc = WndProc;
    wc.hInstance = instance;
    wc.lpszClassName = kHostClass;
    RegisterClassExW(&wc);
    // Hidden top-level window: tray callbacks and the broadcast "show panel"
    // message (neither reaches message-only windows).
    hwnd_ = CreateWindowExW(WS_EX_TOOLWINDOW, kHostClass, L"T9Ime Host", WS_POPUP, 0, 0, 0, 0, nullptr,
                            nullptr, instance, nullptr);
    if (!hwnd_) return false;
    SetWindowLongPtrW(hwnd_, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(this));

    panel_ = std::make_unique<panel::PanelWindow>(engine_);
    panel::PanelOptions po;
    po.input = args.input;
    po.settings_file = args.settings;
    po.dump_layout = args.dump_layout;
    if (!panel_->Create(instance, po)) return false;

    RimeEngine::Options eo;
    eo.shared_dir = WideToUtf8(args.data);
    eo.user_dir = WideToUtf8(args.user);
    engine_.Start(eo, "t9", panel_->hwnd(), panel::PanelWindow::kEngineMessage);
    // Physical-keyboard requests from the TIP. Failure means another host owns
    // the pipe (e.g. a test instance); the panel still works.
    server_ = std::make_unique<ipc::RequestServer>(engine_, args.pipe_name);
    if (!server_->Start()) server_.reset();

    AddTrayIcon();
    if (args.show) panel_->Show();
    return true;
  }

  void Shutdown() {
    RemoveTrayIcon();
    server_.reset();  // before the engine: connection threads call into it
    engine_.Stop();
    panel_.reset();
    if (hwnd_) DestroyWindow(hwnd_);
    hwnd_ = nullptr;
  }

  static UINT ActivateMessage() {
    static const UINT msg = RegisterWindowMessageW(L"T9Ime.Host.ShowPanel");
    return msg;
  }

 private:
  static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    auto* self = reinterpret_cast<HostApp*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (self) {
      if (msg == kTrayMessage) {
        const UINT event = LOWORD(lp);
        if (event == WM_LBUTTONUP) self->panel_->Toggle();
        if (event == WM_RBUTTONUP || event == WM_CONTEXTMENU) self->ShowMenu();
        return 0;
      }
      if (msg == WM_COMMAND) {
        if (LOWORD(wp) == kCmdToggle) self->panel_->Toggle();
        if (LOWORD(wp) == kCmdDock) self->panel_->Dock();
        if (LOWORD(wp) == kCmdExit) PostQuitMessage(0);
        return 0;
      }
      if (msg == ActivateMessage()) {
        self->panel_->Show();
        return 0;
      }
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
  }

  void AddTrayIcon() {
    NOTIFYICONDATAW nid = {sizeof(nid)};
    nid.hWnd = hwnd_;
    nid.uID = kTrayId;
    nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    nid.uCallbackMessage = kTrayMessage;
    nid.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
    lstrcpynW(nid.szTip, L"T9Ime 九宫格输入法", ARRAYSIZE(nid.szTip));
    Shell_NotifyIconW(NIM_ADD, &nid);
  }

  void RemoveTrayIcon() {
    if (!hwnd_) return;
    NOTIFYICONDATAW nid = {sizeof(nid)};
    nid.hWnd = hwnd_;
    nid.uID = kTrayId;
    Shell_NotifyIconW(NIM_DELETE, &nid);
  }

  void ShowMenu() {
    HMENU menu = CreatePopupMenu();
    AppendMenuW(menu, MF_STRING, kCmdToggle, panel_->visible() ? L"隐藏键盘" : L"显示键盘");
    AppendMenuW(menu, MF_STRING, kCmdDock, L"停靠到屏幕底部");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, kCmdExit, L"退出");
    POINT pt;
    GetCursorPos(&pt);
    SetForegroundWindow(hwnd_);  // required for the menu to close on outside clicks
    TrackPopupMenu(menu, TPM_RIGHTBUTTON, pt.x, pt.y, 0, hwnd_, nullptr);
    PostMessageW(hwnd_, WM_NULL, 0, 0);
    DestroyMenu(menu);
  }

  HWND hwnd_ = nullptr;
  EngineThread engine_;
  std::unique_ptr<ipc::RequestServer> server_;
  std::unique_ptr<panel::PanelWindow> panel_;
};

}  // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int) {
  const Args args = ParseArgs();

  HANDLE mutex = nullptr;
  if (args.single_instance) {
    const std::wstring name = L"Local\\T9Ime.Host." + UserSid();
    mutex = CreateMutexW(nullptr, TRUE, name.c_str());
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
      // Already running: ask it to show the panel.
      PostMessageW(HWND_BROADCAST, HostApp::ActivateMessage(), 0, 0);
      if (mutex) CloseHandle(mutex);
      return 0;
    }
  }

  CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
  HostApp app;
  if (!app.Init(instance, args)) {
    app.Shutdown();
    CoUninitialize();
    return 1;
  }
  MSG msg;
  while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
    TranslateMessage(&msg);
    DispatchMessageW(&msg);
  }
  app.Shutdown();
  CoUninitialize();
  if (mutex) CloseHandle(mutex);
  return 0;
}
