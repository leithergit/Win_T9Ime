// T9Host: per-session host process. Owns librime (engine thread), the touch
// panel and the tray icon.
//
//   T9Host.exe [--data <dir>] [--user <dir>] [--settings <ini>] [--show]
//              [--input pointer|touch|mouse] [--dump-layout <file>] [--no-single-instance]
//              [--pipe <request pipe name>] [--theme light|dark] [--open-settings]
//              [--take-over-touch-keyboard] [--restore-touch-keyboard] [--uninstall-user]
//
// --open-settings opens the settings window (in the running instance if there is one).
// --restore-touch-keyboard puts the system touch keyboard / Input Panel
// settings back and exits. --uninstall-user does that and also removes T9Ime
// from the user's input method list (the uninstaller).
// Started with the default settings file, the host adds T9Ime to the user's
// input method list once (Windows 8+ does not do that on registration).

#include <windows.h>
#include <sddl.h>
#include <shellapi.h>
#include <shlobj.h>

#include <algorithm>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "ctl_server.h"
#include "engine_thread.h"
#include "input_settings.h"
#include "panel_window.h"
#include "request_server.h"
#include "settings_window.h"
#include "system_input.h"
#include "text_output.h"
#include "win_compat.h"

namespace {

using namespace t9ime;

constexpr wchar_t kHostClass[] = L"T9Ime.Host";
constexpr UINT kTrayMessage = WM_APP + 10;
constexpr UINT kCtlMessage = WM_APP + 11;  // lParam: std::shared_ptr<CtlCall>* (UI thread takes it)
// From the engine thread: maintenance finished; lParam: std::wstring* status (UI thread takes it).
constexpr UINT kMaintenanceMessage = WM_APP + 12;
constexpr UINT kTrayId = 1;
constexpr UINT kCmdToggle = 100;
constexpr UINT kCmdExit = 101;
constexpr UINT kCmdDock = 102;
constexpr UINT kCmdSettings = 107;
constexpr UINT kCmdRedeploy = 108;
constexpr wchar_t kUserDict[] = L"rime_ice";  // learned words of both schemas
constexpr UINT_PTR kSwitchTimer = 1;
constexpr UINT kSwitchFallbackMs = 300;

struct Args {
  std::wstring data, user, settings, dump_layout, pipe_name;
  bool always_show = false;
  panel::InputMode input = compat::HasPointerInput() ? panel::InputMode::kPointer : panel::InputMode::kTouch;
  bool show = false;
  bool single_instance = true;
  bool background = false;  // started by a TIP: never disturb a running host
  bool take_over_touch_keyboard = false;
  bool restore_touch_keyboard = false;
  bool uninstall_user = false;
  bool default_settings = false;  // no --settings: the user's own settings file
  int theme = -1;
  bool open_settings = false;
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
    else if (k == L"--always-show") a.always_show = true;
    else if (k == L"--theme") a.theme = next() == L"dark" ? 1 : 0;
    else if (k == L"--background") a.background = true;
    else if (k == L"--take-over-touch-keyboard") a.take_over_touch_keyboard = true;
    else if (k == L"--restore-touch-keyboard") a.restore_touch_keyboard = true;
    else if (k == L"--uninstall-user") a.uninstall_user = true;
    else if (k == L"--open-settings") a.open_settings = true;
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
    a.default_settings = true;
  }
  return a;
}

// A control request marshalled to the UI thread.
struct CtlCall {
  std::vector<uint8_t> request;
  std::vector<uint8_t> response;
};

// Tray icon: 中 / 英 on a colored square (the panel's text mode).
HICON MakeTextIcon(const wchar_t* text, COLORREF background) {
  const int size = GetSystemMetrics(SM_CXSMICON);
  HDC screen = GetDC(nullptr);
  HDC dc = CreateCompatibleDC(screen);
  HBITMAP color = CreateCompatibleBitmap(screen, size, size);
  HBITMAP mask = CreateBitmap(size, size, 1, 1, nullptr);  // all 0: opaque
  HGDIOBJ old_bitmap = SelectObject(dc, color);
  HBRUSH brush = CreateSolidBrush(background);
  RECT r = {0, 0, size, size};
  FillRect(dc, &r, brush);
  DeleteObject(brush);
  HFONT font = CreateFontW(-size * 7 / 8, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                           CLIP_DEFAULT_PRECIS, ANTIALIASED_QUALITY, DEFAULT_PITCH, L"Microsoft YaHei");
  HGDIOBJ old_font = SelectObject(dc, font);
  SetBkMode(dc, TRANSPARENT);
  SetTextColor(dc, RGB(255, 255, 255));
  DrawTextW(dc, text, -1, &r, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
  SelectObject(dc, old_font);
  DeleteObject(font);
  SelectObject(dc, old_bitmap);
  ICONINFO ii = {TRUE, 0, 0, mask, color};
  HICON icon = CreateIconIndirect(&ii);
  DeleteObject(color);
  DeleteObject(mask);
  DeleteDC(dc);
  ReleaseDC(nullptr, screen);
  return icon;
}

class HostApp : public SettingsHost {
 public:
  bool Init(HINSTANCE instance, const Args& args) {
    instance_ = instance;
    settings_file_ = args.settings;
    data_dir_ = args.data;
    user_dir_ = args.user;
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
    po.always_show = args.always_show;
    po.theme = args.theme;
    if (!panel_->Create(instance, po)) return false;

    // Input settings: customizations written, compiled data checked (upgrades);
    // deploy before the first use if needed.
    input_ = LoadInputSettings(settings_file_);
    if (PrepareUserData(data_dir_, user_dir_, input_)) {
      const std::filesystem::path shared = data_dir_, user = user_dir_;
      engine_.DeployOnStart([shared, user](bool ok) {
        if (ok) MarkDeployed(shared, user);
      });
    }
    engine_.SetOption("traditionalization", input_.traditional);
    RimeEngine::Options eo;
    eo.shared_dir = WideToUtf8(args.data);
    eo.user_dir = WideToUtf8(args.user);
    engine_.Start(eo, "t9", panel_->hwnd(), panel::PanelWindow::kEngineMessage);
    panel_->SetOnTextModeChanged([this](panel::Mode) { UpdateTrayIcon(); });
    // Physical-keyboard requests from the TIP. Failure means another host owns
    // the pipe (e.g. a test instance); the panel still works.
    // Events pipe first: a TIP attaches to it right after its Hello succeeds.
    events_ = std::make_unique<ipc::EventServer>(focus_, args.pipe_name.empty() ? L"" : args.pipe_name + L".evt");
    if (!events_->Start()) events_.reset();
    server_ = std::make_unique<ipc::RequestServer>(engine_, focus_, args.pipe_name);
    if (!server_->Start()) server_.reset();
    if (server_) {
      server_->SetDiagnostics([this] {
        return panel_->Describe() + (engine_.ready() ? L"engine: ready\n" : L"engine: warming up\n");
      });
    }
    // Control API (T9Ctl.dll, t9ctl.exe): executed on the UI thread. The call
    // object is shared so a timed-out request never leaves a dangling pointer.
    ctl_ = std::make_unique<ipc::CtlServer>(
        [this](const std::vector<uint8_t>& request) -> std::vector<uint8_t> {
          auto call = std::make_shared<CtlCall>();
          call->request = request;
          auto* handoff = new std::shared_ptr<CtlCall>(call);
          DWORD_PTR ignored = 0;
          if (!SendMessageTimeoutW(hwnd_, kCtlMessage, 0, reinterpret_cast<LPARAM>(handoff), SMTO_NORMAL, 3000,
                                   &ignored)) {
            return {};  // UI thread busy or gone (the handoff is freed if the message is ever handled)
          }
          return call->response;
        },
        args.pipe_name.empty() ? L"" : args.pipe_name + L".ctl");
    if (!ctl_->Start()) ctl_.reset();
    panel_->SetOnPlacement([this] { NotifyPlacement(); });
    // Panel output goes to the focused TIP when there is one.
    panel_->SetDeliver([this](const std::wstring& text) { return focus_.PushCommit(text); });
    // Touching the panel while the foreground application has no T9Ime
    // connection: switch it to T9Ime (SPEC U6).
    panel_->SetOnInteraction([this] {
      if (focus_.Foreground(nullptr)) return;
      switch_window_ = GetForegroundWindow();
      if (switch_window_ && switcher_.Begin(switch_window_)) SetTimer(hwnd_, kSwitchTimer, kSwitchFallbackMs, nullptr);
    });
    // Focus changes drive the automatic show / hide (called on pipe threads).
    focus_.SetListener([this](const ipc::FocusInfo& info) {
      panel::FocusEvent e;
      e.focus_in = info.focused;
      e.exe = info.exe.substr(info.exe.find_last_of(L"\\/") + 1);
      e.scopes = info.scopes;
      e.touch = info.touch;
      e.read_only = info.read_only;
      e.switched = info.focused && info.activated && foreground::SettledIn(info.tid);
      e.deactivated = info.deactivated && info.tid == GetWindowThreadProcessId(GetForegroundWindow(), nullptr);
      panel_->PostFocusEvent(std::move(e));
    });

    foreground::Start([this](HWND fg) {
      if (panel_) panel_->OnForegroundChanged(fg);
    });
    if (args.take_over_touch_keyboard) touch_keyboard::TakeOver();
    AddTrayIcon();
    if (args.show) panel_->Show();
    if (args.open_settings) settings_window_.Show(instance_);
    // First run for this user (installed host): put T9Ime into the user's
    // input method list. Once only - the user may remove it again. Test and
    // debugging runs (--settings) never touch the list.
    if (args.default_settings && !GetPrivateProfileIntW(L"install", L"user_list", 0, settings_file_.c_str())) {
      if (user_list::Add()) WritePrivateProfileStringW(L"install", L"user_list", L"1", settings_file_.c_str());
    }
    return true;
  }

  // Keyboard navigation (Tab, Enter) in the settings window.
  bool PreTranslate(MSG* msg) {
    HWND settings = settings_window_.hwnd();
    return settings && IsDialogMessageW(settings, msg);
  }

  void Shutdown() {
    if (HWND settings = settings_window_.hwnd()) DestroyWindow(settings);
    RemoveTrayIcon();
    if (tray_icon_) DestroyIcon(tray_icon_);
    foreground::Stop();
    ctl_.reset();
    server_.reset();  // before the engine: connection threads call into it
    events_.reset();
    engine_.Stop();
    panel_.reset();
    if (hwnd_) DestroyWindow(hwnd_);
    hwnd_ = nullptr;
  }

  // Posted to windows registered with T9_RegisterVisibilityNotify; wParam: visible.
  static UINT VisibilityMessage() {
    static const UINT msg = RegisterWindowMessageW(L"T9Ime.Visibility");
    return msg;
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
        if (LOWORD(wp) == kCmdSettings) self->settings_window_.Show(self->instance_);
        if (LOWORD(wp) == kCmdRedeploy) self->Redeploy();
        if (LOWORD(wp) == kCmdExit) PostQuitMessage(0);
        return 0;
      }
      if (msg == kMaintenanceMessage) {
        std::unique_ptr<std::wstring> status(reinterpret_cast<std::wstring*>(lp));
        self->maintenance_busy_ = false;
        self->settings_window_.SetStatus(*status, false);
        self->ShowBalloon(*status);
        return 0;
      }
      if (msg == WM_TIMER && wp == kSwitchTimer) {
        KillTimer(hwnd, kSwitchTimer);
        if (!self->focus_.Foreground(nullptr) && GetForegroundWindow() == self->switch_window_) {
          self->switcher_.Fallback(self->switch_window_);
        }
        return 0;
      }
      if (msg == kCtlMessage) {
        std::unique_ptr<std::shared_ptr<CtlCall>> call(reinterpret_cast<std::shared_ptr<CtlCall>*>(lp));
        const ipc::Reader r((*call)->request.data(), (*call)->request.size());
        (*call)->response = self->HandleCtl(r);
        return 1;
      }
      if (msg == ActivateMessage()) {  // another T9Host.exe was started; wParam 1: --open-settings
        wp ? self->settings_window_.Show(self->instance_) : self->panel_->Show();
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
    tray_icon_ = TrayIconFor(panel_->text_mode());
    nid.hIcon = tray_icon_;
    lstrcpynW(nid.szTip, TrayTip().c_str(), ARRAYSIZE(nid.szTip));
    Shell_NotifyIconW(NIM_ADD, &nid);
  }

  static HICON TrayIconFor(panel::Mode mode) {
    return mode == panel::Mode::kEnglish ? MakeTextIcon(L"英", RGB(0x55, 0x5B, 0x68))
                                         : MakeTextIcon(L"中", RGB(0x1E, 0x6F, 0xD9));
  }

  std::wstring TrayTip() const {
    return std::wstring(L"T9Ime 九宫格输入法 - ") +
           (panel_->text_mode() == panel::Mode::kEnglish ? L"英文" : L"中文");
  }

  void UpdateTrayIcon() {
    HICON old = tray_icon_;
    tray_icon_ = TrayIconFor(panel_->text_mode());
    NOTIFYICONDATAW nid = {sizeof(nid)};
    nid.hWnd = hwnd_;
    nid.uID = kTrayId;
    nid.uFlags = NIF_ICON | NIF_TIP;
    nid.hIcon = tray_icon_;
    lstrcpynW(nid.szTip, TrayTip().c_str(), ARRAYSIZE(nid.szTip));
    Shell_NotifyIconW(NIM_MODIFY, &nid);
    if (old) DestroyIcon(old);
  }

  // Result of maintenance started from the tray (the settings window shows it too).
  void ShowBalloon(const std::wstring& text) {
    if (settings_window_.hwnd()) return;  // shown in the window's status line
    NOTIFYICONDATAW nid = {sizeof(nid)};
    nid.hWnd = hwnd_;
    nid.uID = kTrayId;
    nid.uFlags = NIF_INFO;
    lstrcpynW(nid.szInfoTitle, L"T9Ime", ARRAYSIZE(nid.szInfoTitle));
    lstrcpynW(nid.szInfo, text.c_str(), ARRAYSIZE(nid.szInfo));
    nid.dwInfoFlags = NIIF_INFO;
    Shell_NotifyIconW(NIM_MODIFY, &nid);
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
    AppendMenuW(menu, MF_STRING, kCmdSettings, L"设置…");
    AppendMenuW(menu, MF_STRING | (maintenance_busy_ ? MF_GRAYED : 0), kCmdRedeploy, L"重新部署");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, kCmdExit, L"退出");
    POINT pt;
    GetCursorPos(&pt);
    SetForegroundWindow(hwnd_);  // required for the menu to close on outside clicks
    TrackPopupMenu(menu, TPM_RIGHTBUTTON, pt.x, pt.y, 0, hwnd_, nullptr);
    PostMessageW(hwnd_, WM_NULL, 0, 0);
    DestroyMenu(menu);
  }

  // Control request on the UI thread; answers with the panel state.
  std::vector<uint8_t> HandleCtl(const ipc::Reader& r) {
    using ipc::MsgType;
    const uint32_t seq = r.seq();
    auto error = [seq] { return ipc::Writer(MsgType::kError, seq).Finish(); };
    const uint32_t mode_value = r.U32Or(ipc::kTagMode, 0);
    const std::optional<panel::Mode> mode =
        mode_value >= 1 && mode_value <= 4 ? std::optional(static_cast<panel::Mode>(mode_value - 1)) : std::nullopt;
    HWND target = reinterpret_cast<HWND>(static_cast<uintptr_t>(r.U32Or(ipc::kTagHwnd, 0)));
    switch (r.type()) {
      case MsgType::kCtlQuery:
        break;
      case MsgType::kCtlShow:
      case MsgType::kCtlToggle:
        if (r.type() == MsgType::kCtlShow) {
          if (mode) panel_->SetModeByApplication(*mode);
          panel_->Show();
        } else {
          panel_->Toggle();
        }
        if (panel_->visible()) panel_->BindToForeground();  // hides when the user switches to another application
        break;
      case MsgType::kCtlHide:
        panel_->Hide();
        break;
      case MsgType::kCtlSetMode:
        if (!mode) return error();
        panel_->SetModeByApplication(*mode);
        break;
      case MsgType::kCtlDock:
        panel_->Dock();
        break;
      case MsgType::kCtlSetPosition:
        panel_->MoveTo(static_cast<int32_t>(r.U32Or(ipc::kTagX, 0)), static_cast<int32_t>(r.U32Or(ipc::kTagY, 0)));
        break;
      case MsgType::kCtlActivate:
      case MsgType::kCtlDeactivate:
        if (!target) target = GetForegroundWindow();
        if (!target || !IsWindow(target)) return error();
        if (r.type() == MsgType::kCtlActivate) {
          ImeSwitcher::Activate(target);
        } else if (!ImeSwitcher::Deactivate(target)) {
          return error();
        }
        break;
      case MsgType::kCtlRegisterNotify:
        if (!target || !IsWindow(target)) return error();
        if (std::find(notify_.begin(), notify_.end(), target) == notify_.end()) notify_.push_back(target);
        break;
      case MsgType::kCtlUnregisterNotify:
        notify_.erase(std::remove(notify_.begin(), notify_.end(), target), notify_.end());
        break;
      default:
        return error();
    }
    RECT rc = {};
    GetWindowRect(panel_->hwnd(), &rc);
    ipc::Writer ack(MsgType::kAck, seq);
    ack.Bool(ipc::kTagVisible, panel_->visible()).U32(ipc::kTagMode, static_cast<uint32_t>(panel_->mode()) + 1);
    ack.I32(ipc::kTagX, rc.left).I32(ipc::kTagY, rc.top);
    ack.I32(ipc::kTagWidth, rc.right - rc.left).I32(ipc::kTagHeight, rc.bottom - rc.top);
    return ack.Finish();
  }

  // Panel shown / hidden / moved: tell the registered windows (once per change).
  void NotifyPlacement() {
    if (!panel_) return;
    const bool visible = panel_->visible();
    RECT rc = {};
    GetWindowRect(panel_->hwnd(), &rc);
    if (visible == notified_visible_ && EqualRect(&rc, &notified_rect_)) return;
    notified_visible_ = visible;
    notified_rect_ = rc;
    notify_.erase(std::remove_if(notify_.begin(), notify_.end(), [](HWND h) { return !IsWindow(h); }),
                  notify_.end());
    for (HWND h : notify_) PostMessageW(h, VisibilityMessage(), visible ? 1 : 0, 0);
  }

  // ---------------------------------------------------------------- SettingsHost

  SettingsValues CurrentSettings() override {
    SettingsValues v;
    v.auto_show = panel_->settings();
    v.take_over_touch_keyboard = touch_keyboard::IsTakenOver();
    v.theme = panel_->theme_setting();
    v.size = panel_->size_preset();
    v.input = input_;
    return v;
  }

  void ApplySettings(const SettingsValues& v) override {
    panel_->settings() = v.auto_show;
    panel_->SaveSettings();
    if (v.take_over_touch_keyboard != touch_keyboard::IsTakenOver()) {
      v.take_over_touch_keyboard ? touch_keyboard::TakeOver() : touch_keyboard::Restore();
    }
    if (v.theme != panel_->theme_setting()) panel_->SetThemeSetting(v.theme);
    if (v.size >= 0 && v.size != panel_->size_preset()) panel_->SetSizePreset(v.size);

    const InputSettings old = input_;
    input_ = v.input;
    SaveInputSettings(settings_file_, input_);
    if (input_.traditional != old.traditional) engine_.SetOption("traditionalization", input_.traditional);
    if (!input_.SameDeployment(old)) Redeploy();
  }

  // Writes the customizations and compiles them (seconds; TIP keys pass
  // through meanwhile). Also repairs a broken user build.
  void Redeploy() override {
    PrepareUserData(data_dir_, user_dir_, input_);
    const std::filesystem::path shared = data_dir_, user = user_dir_;
    StartMaintenance(L"正在重新部署，请稍候…", [shared, user](RimeEngine& engine) -> std::wstring {
      if (!engine.Redeploy()) return L"重新部署失败";
      MarkDeployed(shared, user);
      return L"重新部署完成";
    });
  }

  void ExportDictionary(const std::wstring& file) override {
    const std::string path = WideToUtf8(file);
    StartMaintenance(L"正在导出用户词库…", [path](RimeEngine& engine) -> std::wstring {
      const int n = engine.ExportUserDict(WideToUtf8(kUserDict), path);
      return n >= 0 ? L"已导出 " + std::to_wstring(n) + L" 条" : L"导出失败（还没有学习到的词？）";
    });
  }

  void ImportDictionary(const std::wstring& file) override {
    const std::string path = WideToUtf8(file);
    StartMaintenance(L"正在导入用户词库…", [path](RimeEngine& engine) -> std::wstring {
      const int n = engine.ImportUserDict(WideToUtf8(kUserDict), path);
      return n >= 0 ? L"已导入 " + std::to_wstring(n) + L" 条" : L"导入失败（文件格式不对？）";
    });
  }

  void ClearDictionary() override {
    StartMaintenance(L"正在清空学习记录…", [](RimeEngine& engine) -> std::wstring {
      return engine.ClearUserDict(WideToUtf8(kUserDict)) ? L"学习记录已清空" : L"清空失败";
    });
  }

  void DockKeyboard() override { panel_->Dock(); }

  // Runs `work` on the engine thread with every session closed; its status
  // text comes back to the UI thread (settings window, tray balloon).
  void StartMaintenance(const std::wstring& busy_text, std::function<std::wstring(RimeEngine&)> work) {
    maintenance_busy_ = true;
    settings_window_.SetStatus(busy_text, true);
    auto result = std::make_shared<std::wstring>();
    HWND hwnd = hwnd_;
    engine_.Maintain(
        [work, result](RimeEngine& engine) {
          *result = work(engine);
          return true;
        },
        [hwnd, result](bool) { PostMessageW(hwnd, kMaintenanceMessage, 0, reinterpret_cast<LPARAM>(new std::wstring(*result))); });
  }

  HINSTANCE instance_ = nullptr;
  std::wstring settings_file_, data_dir_, user_dir_;
  InputSettings input_;
  SettingsWindow settings_window_{*this};
  bool maintenance_busy_ = false;
  HICON tray_icon_ = nullptr;
  HWND hwnd_ = nullptr;
  std::vector<HWND> notify_;
  bool notified_visible_ = false;
  RECT notified_rect_ = {};
  EngineThread engine_;
  ipc::FocusRegistry focus_;
  ImeSwitcher switcher_;
  HWND switch_window_ = nullptr;
  std::unique_ptr<ipc::RequestServer> server_;
  std::unique_ptr<ipc::EventServer> events_;
  std::unique_ptr<ipc::CtlServer> ctl_;
  std::unique_ptr<panel::PanelWindow> panel_;
};

}  // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int) {
  const Args args = ParseArgs();

  HANDLE mutex = nullptr;
  if (args.restore_touch_keyboard || args.uninstall_user) {  // uninstaller: no host
    touch_keyboard::Restore();
    if (args.uninstall_user) {
      user_list::Remove();
      WritePrivateProfileStringW(L"install", L"user_list", nullptr, args.settings.c_str());
    }
    return 0;
  }
  if (args.single_instance) {
    const std::wstring name = L"Local\\T9Ime.Host." + UserSid();
    mutex = CreateMutexW(nullptr, TRUE, name.c_str());
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
      // Already running: ask it to show the panel (unless started by a TIP).
      if (!args.background) PostMessageW(HWND_BROADCAST, HostApp::ActivateMessage(), args.open_settings ? 1 : 0, 0);
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
    if (app.PreTranslate(&msg)) continue;
    TranslateMessage(&msg);
    DispatchMessageW(&msg);
  }
  app.Shutdown();
  CoUninitialize();
  if (mutex) CloseHandle(mutex);
  return 0;
}
