#include "settings_window.h"

#include <commctrl.h>
#include <commdlg.h>

#include <algorithm>

#include "win_compat.h"

namespace t9ime {

namespace {

constexpr wchar_t kClass[] = L"T9Ime.Settings";

enum Id {
  kTabs = 100,
  kAutoShow, kAlwaysShow, kShowOnSwitch, kTakeOver, kThemeLabel, kTheme, kSizeLabel, kSize, kDock,
  kScriptLabel, kScript, kPageLabel, kPage, kFuzzyLabel, kDeployNote,
  kExport, kImport, kClear, kRedeploy, kDictNote,
  kOk, kCancel, kApply, kStatus,
  kFuzzyFirst = 200,
};

// Layout in DIPs.
constexpr int kWidth = 600, kHeight = 560, kMargin = 20, kRow = 44, kButtonW = 120, kButtonH = 40;

std::wstring TodayName() {
  SYSTEMTIME t;
  GetLocalTime(&t);
  wchar_t buf[64];
  swprintf_s(buf, L"T9Ime用户词库-%04d%02d%02d.txt", t.wYear, t.wMonth, t.wDay);
  return buf;
}

}  // namespace

SettingsWindow::~SettingsWindow() {
  if (hwnd_) DestroyWindow(hwnd_);
  if (font_) DeleteObject(font_);
}

void SettingsWindow::Show(HINSTANCE instance) {
  if (hwnd_) {
    Load(host_.CurrentSettings());
    ShowWindow(hwnd_, SW_SHOWNORMAL);
    SetForegroundWindow(hwnd_);
    return;
  }
  INITCOMMONCONTROLSEX icc = {sizeof(icc), ICC_TAB_CLASSES | ICC_STANDARD_CLASSES};
  InitCommonControlsEx(&icc);
  WNDCLASSEXW wc = {sizeof(wc)};
  if (!GetClassInfoExW(instance, kClass, &wc)) {
    wc = {sizeof(wc)};
    wc.lpfnWndProc = WndProc;
    wc.hInstance = instance;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);  // same as the tab pages
    wc.hIcon = LoadIconW(GetModuleHandleW(nullptr), MAKEINTRESOURCEW(1));  // T9Host's icon (res/T9Ime.ico)
    wc.lpszClassName = kClass;
    RegisterClassExW(&wc);
  }
  // Centered on the monitor of the cursor, sized for its DPI.
  POINT pt;
  GetCursorPos(&pt);
  const UINT dpi = compat::DpiForPoint(pt);
  RECT r = {0, 0, MulDiv(kWidth, dpi, 96), MulDiv(kHeight, dpi, 96)};
  AdjustWindowRectEx(&r, WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU, FALSE, WS_EX_DLGMODALFRAME);
  MONITORINFO mi = {sizeof(mi)};
  GetMonitorInfoW(MonitorFromPoint(pt, MONITOR_DEFAULTTOPRIMARY), &mi);
  const int w = r.right - r.left, h = r.bottom - r.top;
  const int x = (mi.rcWork.left + mi.rcWork.right - w) / 2, y = (mi.rcWork.top + mi.rcWork.bottom - h) / 2;
  hwnd_ = CreateWindowExW(WS_EX_DLGMODALFRAME, kClass, L"T9Ime 设置", WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU, x, y,
                          w, h, nullptr, nullptr, instance, this);
  if (!hwnd_) return;
  dpi_ = compat::DpiForWindow(hwnd_);
  CreateControls();
  Layout();
  Load(host_.CurrentSettings());
  SelectPage(0);
  ShowWindow(hwnd_, SW_SHOWNORMAL);
  SetForegroundWindow(hwnd_);
}

HWND SettingsWindow::Add(int page, const wchar_t* cls, const wchar_t* text, DWORD style, int id) {
  HWND h = CreateWindowExW(0, cls, text, WS_CHILD | style, 0, 0, 0, 0, hwnd_,
                           reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), nullptr, nullptr);
  SendMessageW(h, WM_SETFONT, reinterpret_cast<WPARAM>(font_), FALSE);
  controls_.push_back({h, page});
  return h;
}

void SettingsWindow::CreateControls() {
  // The system message font, enlarged: comfortable for fingers.
  NONCLIENTMETRICSW ncm = {sizeof(ncm)};
  SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof(ncm), &ncm, 0);
  LOGFONTW lf = ncm.lfMessageFont;
  lf.lfHeight = -MulDiv(16, dpi_, 96);
  if (font_) DeleteObject(font_);
  font_ = CreateFontIndirectW(&lf);

  tabs_ = Add(-1, WC_TABCONTROLW, L"", WS_VISIBLE | WS_CLIPSIBLINGS, kTabs);
  const wchar_t* pages[] = {L"  键盘  ", L"  输入  ", L"  词库  "};
  for (int i = 0; i < 3; ++i) {
    TCITEMW item = {TCIF_TEXT};
    item.pszText = const_cast<wchar_t*>(pages[i]);
    TabCtrl_InsertItem(tabs_, i, &item);
  }
  const DWORD check = WS_TABSTOP | BS_AUTOCHECKBOX;
  const DWORD combo = WS_TABSTOP | WS_VSCROLL | CBS_DROPDOWNLIST;
  const DWORD button = WS_TABSTOP | BS_PUSHBUTTON;

  // 键盘
  Add(0, L"BUTTON", L"触摸输入框时自动弹出键盘", check, kAutoShow);
  Add(0, L"BUTTON", L"任何方式聚焦输入框都弹出（无触摸屏时）", check, kAlwaysShow);
  Add(0, L"BUTTON", L"切换到 T9Ime 时弹出键盘", check, kShowOnSwitch);
  Add(0, L"BUTTON", compat::Os().AtLeastWin10() ? L"关闭系统触摸键盘的自动弹出" : L"关闭系统输入面板图标", check,
      kTakeOver);
  Add(0, L"STATIC", L"主题", SS_CENTERIMAGE, kThemeLabel);
  HWND theme = Add(0, WC_COMBOBOXW, L"", combo, kTheme);
  for (const wchar_t* t : {L"跟随系统", L"浅色", L"深色"}) SendMessageW(theme, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(t));
  Add(0, L"STATIC", L"键盘大小", SS_CENTERIMAGE, kSizeLabel);
  HWND size = Add(0, WC_COMBOBOXW, L"", combo, kSize);
  for (const wchar_t* t : {L"小", L"中（默认）", L"大", L"自定义（拖动调整的大小）"}) {
    SendMessageW(size, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(t));
  }
  Add(0, L"BUTTON", L"停靠到屏幕底部", button, kDock);

  // 输入
  Add(1, L"STATIC", L"字形", SS_CENTERIMAGE, kScriptLabel);
  HWND script = Add(1, WC_COMBOBOXW, L"", combo, kScript);
  for (const wchar_t* t : {L"简体", L"繁体"}) SendMessageW(script, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(t));
  Add(1, L"STATIC", L"物理键盘候选个数", SS_CENTERIMAGE, kPageLabel);
  HWND page = Add(1, WC_COMBOBOXW, L"", combo, kPage);
  for (int n = kMinPageSize; n <= kMaxPageSize; ++n) {
    SendMessageW(page, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(std::to_wstring(n).c_str()));
  }
  Add(1, L"STATIC", L"模糊音（勾选后两种拼法都能打出）", SS_CENTERIMAGE, kFuzzyLabel);
  fuzzy_.clear();
  const auto& options = FuzzyOptions();
  for (size_t i = 0; i < options.size(); ++i) {
    fuzzy_.push_back(Add(1, L"BUTTON", options[i].label, check, kFuzzyFirst + static_cast<int>(i)));
  }
  Add(1, L"STATIC", L"更改模糊音或候选个数后会重新部署，需要几秒钟，期间键盘暂不能输入。", 0, kDeployNote);

  // 词库
  Add(2, L"BUTTON", L"导出用户词库…", button, kExport);
  Add(2, L"BUTTON", L"导入用户词库…", button, kImport);
  Add(2, L"BUTTON", L"清空学习记录…", button, kClear);
  Add(2, L"BUTTON", L"重新部署", button, kRedeploy);
  Add(2, L"STATIC",
      L"用户词库是输入法记住的常用词和新词，可导出为文本文件备份或带到其他电脑；导入会与现有记录合并。"
      L"清空后输入法忘记所有学习到的词，无法恢复（可先导出备份）。",
      0, kDictNote);

  // Bottom row, all pages.
  status_ = Add(-1, L"STATIC", L"", WS_VISIBLE | SS_LEFTNOWORDWRAP | SS_CENTERIMAGE, kStatus);
  Add(-1, L"BUTTON", L"确定", WS_VISIBLE | button | BS_DEFPUSHBUTTON, kOk);
  Add(-1, L"BUTTON", L"取消", WS_VISIBLE | button, kCancel);
  Add(-1, L"BUTTON", L"应用", WS_VISIBLE | button, kApply);
}

void SettingsWindow::Layout() {
  auto px = [&](int dip) { return MulDiv(dip, dpi_, 96); };
  auto place = [&](int id, int x, int y, int w, int h) {
    MoveWindow(GetDlgItem(hwnd_, id), px(x), px(y), px(w), px(h), FALSE);
  };
  const int inner = kWidth - 2 * kMargin;
  // Only the tab strip: the pages sit on the window's own (white) background,
  // so labels and check boxes need no background matching.
  place(kTabs, kMargin / 2, kMargin / 2, kWidth - kMargin, 38);
  const int top = kMargin + 44;
  const int x = kMargin + 12, w = inner - 24;

  int y = top;
  for (int id : {kAutoShow, kAlwaysShow, kShowOnSwitch, kTakeOver}) {
    place(id, x, y, w, kRow);
    y += kRow;
  }
  y += 8;
  place(kThemeLabel, x, y, 110, kRow);
  place(kTheme, x + 120, y + 4, 240, 300);
  y += kRow + 4;
  place(kSizeLabel, x, y, 110, kRow);
  place(kSize, x + 120, y + 4, 300, 300);
  y += kRow + 12;
  place(kDock, x, y, 200, kButtonH);

  y = top;
  place(kScriptLabel, x, y, 180, kRow);
  place(kScript, x + 190, y + 4, 160, 300);
  y += kRow + 4;
  place(kPageLabel, x, y, 180, kRow);
  place(kPage, x + 190, y + 4, 160, 300);
  y += kRow + 8;
  place(kFuzzyLabel, x, y, w, kRow);
  y += kRow;
  const int col = (w - 12) / 3;
  for (size_t i = 0; i < fuzzy_.size(); ++i) {
    place(kFuzzyFirst + static_cast<int>(i), x + 12 + static_cast<int>(i % 3) * col, y + static_cast<int>(i / 3) * 40,
          col, 40);
  }
  y += static_cast<int>((fuzzy_.size() + 2) / 3) * 40 + 8;
  place(kDeployNote, x, y, w, 52);

  y = top;
  place(kExport, x, y, 220, kButtonH);
  place(kImport, x + 236, y, 220, kButtonH);
  y += kButtonH + 16;
  place(kClear, x, y, 220, kButtonH);
  place(kRedeploy, x + 236, y, 220, kButtonH);
  y += kButtonH + 20;
  place(kDictNote, x, y, w, 120);

  const int by = kHeight - kMargin / 2 - kButtonH;
  place(kStatus, kMargin, by, kWidth - 3 * (kButtonW + 10) - kMargin - 10, kButtonH);
  place(kOk, kWidth - 3 * (kButtonW + 10), by, kButtonW, kButtonH);
  place(kCancel, kWidth - 2 * (kButtonW + 10), by, kButtonW, kButtonH);
  place(kApply, kWidth - (kButtonW + 10), by, kButtonW, kButtonH);
  InvalidateRect(hwnd_, nullptr, TRUE);
}

void SettingsWindow::SelectPage(int page) {
  page_ = page;
  TabCtrl_SetCurSel(tabs_, page);
  for (const Control& c : controls_) {
    if (c.page >= 0) ShowWindow(c.hwnd, c.page == page ? SW_SHOW : SW_HIDE);
  }
  // The tab control is behind the page controls.
  SetWindowPos(tabs_, HWND_BOTTOM, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
}

void SettingsWindow::Load(const SettingsValues& v) {
  auto check = [&](int id, bool on) { CheckDlgButton(hwnd_, id, on ? BST_CHECKED : BST_UNCHECKED); };
  check(kAutoShow, v.auto_show.auto_show);
  check(kAlwaysShow, v.auto_show.always_show);
  check(kShowOnSwitch, v.auto_show.show_on_switch);
  check(kTakeOver, v.take_over_touch_keyboard);
  SendDlgItemMessageW(hwnd_, kTheme, CB_SETCURSEL, v.theme + 1, 0);
  SendDlgItemMessageW(hwnd_, kSize, CB_SETCURSEL, v.size < 0 ? 3 : v.size, 0);
  SendDlgItemMessageW(hwnd_, kScript, CB_SETCURSEL, v.input.traditional ? 1 : 0, 0);
  SendDlgItemMessageW(hwnd_, kPage, CB_SETCURSEL, v.input.page_size - kMinPageSize, 0);
  const auto& options = FuzzyOptions();
  for (size_t i = 0; i < options.size(); ++i) {
    const bool on = std::find(v.input.fuzzy.begin(), v.input.fuzzy.end(), options[i].id) != v.input.fuzzy.end();
    check(kFuzzyFirst + static_cast<int>(i), on);
  }
  UpdateEnabled();
}

SettingsValues SettingsWindow::Read() const {
  SettingsValues v;
  auto checked = [&](int id) { return IsDlgButtonChecked(hwnd_, id) == BST_CHECKED; };
  v.auto_show.auto_show = checked(kAutoShow);
  v.auto_show.always_show = checked(kAlwaysShow);
  v.auto_show.show_on_switch = checked(kShowOnSwitch);
  v.take_over_touch_keyboard = checked(kTakeOver);
  v.theme = static_cast<int>(SendDlgItemMessageW(hwnd_, kTheme, CB_GETCURSEL, 0, 0)) - 1;
  const int size = static_cast<int>(SendDlgItemMessageW(hwnd_, kSize, CB_GETCURSEL, 0, 0));
  v.size = size >= 0 && size <= 2 ? size : -1;
  v.input.traditional = SendDlgItemMessageW(hwnd_, kScript, CB_GETCURSEL, 0, 0) == 1;
  v.input.page_size = std::clamp(
      kMinPageSize + static_cast<int>(SendDlgItemMessageW(hwnd_, kPage, CB_GETCURSEL, 0, 0)), kMinPageSize, kMaxPageSize);
  const auto& options = FuzzyOptions();
  for (size_t i = 0; i < options.size(); ++i) {
    if (checked(kFuzzyFirst + static_cast<int>(i))) v.input.fuzzy.push_back(options[i].id);
  }
  return v;
}

void SettingsWindow::UpdateEnabled() {
  // "Always" only matters with the automatic pop-up on.
  EnableWindow(GetDlgItem(hwnd_, kAlwaysShow), IsDlgButtonChecked(hwnd_, kAutoShow) == BST_CHECKED);
  for (int id : {kExport, kImport, kClear, kRedeploy}) EnableWindow(GetDlgItem(hwnd_, id), !busy_);
}

void SettingsWindow::Apply() {
  host_.ApplySettings(Read());
  Load(host_.CurrentSettings());  // e.g. the size combo after a resize
}

void SettingsWindow::SetStatus(const std::wstring& text, bool busy) {
  if (!hwnd_) return;
  busy_ = busy;
  SetWindowTextW(status_, text.c_str());
  UpdateEnabled();
}

LRESULT CALLBACK SettingsWindow::WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
  SettingsWindow* self = nullptr;
  if (msg == WM_NCCREATE) {
    self = static_cast<SettingsWindow*>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams);
    self->hwnd_ = hwnd;
    SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
  } else {
    self = reinterpret_cast<SettingsWindow*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
  }
  if (!self) return DefWindowProcW(hwnd, msg, wp, lp);
  if (msg == WM_NCDESTROY) {
    SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
    self->hwnd_ = nullptr;
    self->controls_.clear();
    self->fuzzy_.clear();
    self->busy_ = false;
    return DefWindowProcW(hwnd, msg, wp, lp);
  }
  return self->HandleMessage(msg, wp, lp);
}

LRESULT SettingsWindow::HandleMessage(UINT msg, WPARAM wp, LPARAM lp) {
  switch (msg) {
    case WM_NOTIFY:
      if (reinterpret_cast<NMHDR*>(lp)->idFrom == kTabs && reinterpret_cast<NMHDR*>(lp)->code == TCN_SELCHANGE) {
        SelectPage(TabCtrl_GetCurSel(tabs_));
      }
      return 0;
    case WM_COMMAND: {
      const int id = LOWORD(wp);
      switch (id) {
        case kAutoShow:
          UpdateEnabled();
          return 0;
        case kDock:
          host_.DockKeyboard();
          return 0;
        case kOk:
          Apply();
          DestroyWindow(hwnd_);
          return 0;
        case kCancel:
          DestroyWindow(hwnd_);
          return 0;
        case kApply:
          Apply();
          return 0;
        case kRedeploy:
          host_.Redeploy();
          return 0;
        case kExport:
        case kImport: {
          wchar_t file[MAX_PATH] = {};
          if (id == kExport) lstrcpynW(file, TodayName().c_str(), MAX_PATH);
          OPENFILENAMEW ofn = {sizeof(ofn)};
          ofn.hwndOwner = hwnd_;
          ofn.lpstrFilter = L"文本文件 (*.txt)\0*.txt\0所有文件 (*.*)\0*.*\0";
          ofn.lpstrFile = file;
          ofn.nMaxFile = MAX_PATH;
          ofn.lpstrDefExt = L"txt";
          ofn.Flags = OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR |
                      (id == kExport ? OFN_OVERWRITEPROMPT : OFN_FILEMUSTEXIST);
          const BOOL ok = id == kExport ? GetSaveFileNameW(&ofn) : GetOpenFileNameW(&ofn);
          if (ok) id == kExport ? host_.ExportDictionary(file) : host_.ImportDictionary(file);
          return 0;
        }
        case kClear:
          if (MessageBoxW(hwnd_, L"清空后，输入法学习到的所有词语都会被删除，无法恢复。\n\n确定要清空吗？",
                          L"清空学习记录", MB_OKCANCEL | MB_ICONWARNING | MB_DEFBUTTON2) == IDOK) {
            host_.ClearDictionary();
          }
          return 0;
      }
      return 0;
    }
    case WM_CTLCOLORSTATIC:
      // Static text on the tab page background.
      SetBkMode(reinterpret_cast<HDC>(wp), TRANSPARENT);
      return reinterpret_cast<LRESULT>(GetSysColorBrush(COLOR_WINDOW));
    case compat::kWmDpiChanged: {
      dpi_ = HIWORD(wp);
      const RECT* r = reinterpret_cast<const RECT*>(lp);
      SetWindowPos(hwnd_, nullptr, r->left, r->top, r->right - r->left, r->bottom - r->top,
                   SWP_NOZORDER | SWP_NOACTIVATE);
      // New font for the new DPI.
      NONCLIENTMETRICSW ncm = {sizeof(ncm)};
      SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof(ncm), &ncm, 0);
      LOGFONTW lf = ncm.lfMessageFont;
      lf.lfHeight = -MulDiv(16, dpi_, 96);
      HFONT old = font_;
      font_ = CreateFontIndirectW(&lf);
      for (const Control& c : controls_) SendMessageW(c.hwnd, WM_SETFONT, reinterpret_cast<WPARAM>(font_), FALSE);
      if (old) DeleteObject(old);
      Layout();
      return 0;
    }
    case WM_CLOSE:
      DestroyWindow(hwnd_);
      return 0;
  }
  return DefWindowProcW(hwnd_, msg, wp, lp);
}

}  // namespace t9ime
