#include "panel_window.h"

#include <windowsx.h>

#include <algorithm>
#include <cmath>
#include <cstdio>

#include "system_input.h"
#include "text_output.h"
#include "win_compat.h"

namespace t9ime::panel {

namespace {

constexpr wchar_t kClassName[] = L"T9Ime.Panel";
constexpr UINT_PTR kLongPressTimer = 1;
constexpr UINT_PTR kRepeatTimer = 2;
constexpr UINT_PTR kAutoHideTimer = 3;
constexpr UINT kAutoHideMs = 300;  // focus may just be moving to another field
constexpr ULONGLONG kCapsLockMs = 400;  // second shift tap within this time: caps lock
constexpr UINT kLongPressMs = 450;
constexpr UINT kRepeatMs = 70;
constexpr float kScrollSlop = 8;    // DIPs before a press on a list turns into scrolling
constexpr float kKeySlop = 12;      // DIPs a finger may slide off a key and still tap it
constexpr float kSwipeClear = 40;   // DIPs to the left on BackSpace clears the input
constexpr UINT32 kMousePointer = 0xFFFF0001;
constexpr float kMinWidth = 280, kMaxWidth = 1200, kMinHeight = 220, kMaxHeight = 800;  // DIPs

const std::vector<std::wstring> kChinesePunct = {L"，", L"。", L"？", L"！", L"、", L"：",
                                                 L"；", L"……", L"～", L"“", L"”"};
const std::vector<std::wstring> kNumberSymbols = {L"+", L"-", L"*", L"/", L"=", L"%",
                                                  L":", L"(", L")", L"#"};
const std::vector<std::wstring> kSymbolCategories = {L"中文", L"英文", L"数学", L"特殊"};

std::vector<std::wstring> Split(std::wstring_view s) {
  std::vector<std::wstring> out;
  for (size_t i = 0; i < s.size();) {
    size_t j = s.find(L' ', i);
    if (j == std::wstring_view::npos) j = s.size();
    if (j > i) out.emplace_back(s.substr(i, j - i));
    i = j + 1;
  }
  return out;
}

std::vector<std::wstring> SymbolsFor(int category) {
  switch (category) {
    case 0:
      return Split(L"， 。 ？ ！ 、 ： ； “ ” ‘ ’ （ ） 《 》 【 】 …… —— · ￥ 「 」 ～");
    case 1:
      return Split(L", . ? ! : ; ' \" ( ) [ ] { } < > @ # $ % ^ & * _ - + = / \\ | ~ `");
    case 2:
      return Split(L"+ − × ÷ = ≠ ≈ ± < > ≤ ≥ % ‰ √ ∞ ° π ∑ ∫ ∵ ∴ ∈ ∩ ∪");
    default:
      return Split(L"★ ☆ ○ ● ◎ ◇ ◆ □ ■ △ ▲ → ← ↑ ↓ ※ § № ℃ ♂ ♀ ✓ ✗ ♪");
  }
}

std::string JsonEscape(const std::string& s) {
  std::string out;
  for (char c : s) {
    if (c == '"' || c == '\\') {
      out += '\\';
      out += c;
    } else if (static_cast<unsigned char>(c) < 0x20) {
      char buf[8];
      std::snprintf(buf, sizeof(buf), "\\u%04x", c);
      out += buf;
    } else {
      out += c;
    }
  }
  return out;
}

}  // namespace

PanelWindow::~PanelWindow() {
  if (hwnd_) DestroyWindow(hwnd_);
}

bool PanelWindow::Create(HINSTANCE instance, const PanelOptions& options) {
  options_ = options;
  WNDCLASSEXW wc = {sizeof(wc)};
  wc.lpfnWndProc = WndProc;
  wc.hInstance = instance;
  wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
  wc.lpszClassName = kClassName;
  RegisterClassExW(&wc);

  if (!renderer_.Initialize()) return false;
  theme_ = (options_.theme < 0 ? compat::SystemPrefersDark() : options_.theme == 1) ? Theme::Dark() : Theme::Light();

  hwnd_ = CreateWindowExW(WS_EX_NOACTIVATE | WS_EX_TOPMOST | WS_EX_TOOLWINDOW, kClassName, L"T9Ime",
                          WS_POPUP, 0, 0, 400, 300, nullptr, nullptr, instance, this);
  if (!hwnd_) return false;
  if (options_.input == InputMode::kTouch) RegisterTouchWindow(hwnd_, TWF_WANTPALM);
  compat::DisableTouchFeedback(hwnd_);
  LoadPlacement();
  if (!options_.settings_file.empty()) {
    const wchar_t* file = options_.settings_file.c_str();
    settings_.auto_show = GetPrivateProfileIntW(L"panel", L"auto_show", 1, file) != 0;
    settings_.always_show = GetPrivateProfileIntW(L"panel", L"always_show", 0, file) != 0;
    settings_.show_on_switch = GetPrivateProfileIntW(L"panel", L"show_on_switch", 1, file) != 0;
  }
  if (options_.always_show) settings_.always_show = true;
  Relayout();
  if (!options_.dump_layout.empty()) DumpLayout();  // tests start with a hidden panel too
  return true;
}

void PanelWindow::SaveSettings() {
  if (options_.settings_file.empty()) return;
  const wchar_t* file = options_.settings_file.c_str();
  WritePrivateProfileStringW(L"panel", L"auto_show", settings_.auto_show ? L"1" : L"0", file);
  WritePrivateProfileStringW(L"panel", L"always_show", settings_.always_show ? L"1" : L"0", file);
  WritePrivateProfileStringW(L"panel", L"show_on_switch", settings_.show_on_switch ? L"1" : L"0", file);
}

std::wstring PanelWindow::Describe() {
  std::lock_guard lock(focus_mutex_);
  std::wstring s = L"panel: " + std::wstring(visible() ? L"visible" : L"hidden") +
                   (auto_shown_ ? L" (auto-shown)" : L"") + L", auto_show=" + (settings_.auto_show ? L"1" : L"0") +
                   L" always_show=" + (settings_.always_show ? L"1" : L"0") +
                   L" show_on_switch=" + (settings_.show_on_switch ? L"1" : L"0") + L"\n";
  s += L"last focus event: " + Utf8ToWide(last_focus_) + L"\n";
  return s;
}

void PanelWindow::PostFocusEvent(FocusEvent e) {
  {
    std::lock_guard lock(focus_mutex_);
    focus_events_.push_back(std::move(e));
  }
  PostMessageW(hwnd_, kFocusMessage, 0, 0);
}

void PanelWindow::OnFocusEvents() {
  std::deque<FocusEvent> events;
  {
    std::lock_guard lock(focus_mutex_);
    events.swap(focus_events_);
  }
  for (const FocusEvent& e : events) {
    last_focus_ = std::string(e.focus_in ? "in" : "out") + (e.touch ? " touch" : "") + (e.switched ? " switched" : "") +
                  " scopes=";
    for (uint32_t sc : e.scopes) last_focus_ += std::to_string(sc) + ",";
    const AutoDecision d = DecideOnFocus(e, settings_, text_mode_, compat::Os().AtLeastWin10());
    {
      std::lock_guard lock(focus_mutex_);
      last_focus_ += d.action == AutoAction::kShow   ? " -> show"
                     : d.action == AutoAction::kHide ? " -> hide"
                                                     : " -> none";
      if (e.focus_in) last_focus_ += " exe=" + WideToUtf8(e.exe);
    }
    switch (d.action) {
      case AutoAction::kShow:
        KillTimer(hwnd_, kAutoHideTimer);
        if (!visible() && touch_keyboard::IsVisible()) break;  // the user opened the Windows keyboard
        SetMode(d.mode, false);
        if (!visible()) {
          Show();
          auto_shown_ = true;
        }
        break;
      case AutoAction::kHide:
        if (auto_shown_ && visible()) SetTimer(hwnd_, kAutoHideTimer, kAutoHideMs, nullptr);
        break;
      case AutoAction::kNone:
        if (e.focus_in) KillTimer(hwnd_, kAutoHideTimer);  // focus moved on: keep the panel
        break;
    }
  }
}

void PanelWindow::Show() {
  SetWindowPos(hwnd_, HWND_TOPMOST, 0, 0, 0, 0,
               SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_SHOWWINDOW);
  InvalidateRect(hwnd_, nullptr, FALSE);
}

void PanelWindow::Dock() {
  PlaceDefault();
  SavePlacement();
}

void PanelWindow::Hide() {
  auto_shown_ = false;
  KillTimer(hwnd_, kAutoHideTimer);
  tracks_.clear();
  KillTimer(hwnd_, kLongPressTimer);
  KillTimer(hwnd_, kRepeatTimer);
  ShowWindow(hwnd_, SW_HIDE);
  if (!options_.dump_layout.empty()) DumpLayout();
}

LRESULT CALLBACK PanelWindow::WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
  PanelWindow* self = nullptr;
  if (msg == WM_NCCREATE) {
    self = static_cast<PanelWindow*>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams);
    self->hwnd_ = hwnd;
    SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
  } else {
    self = reinterpret_cast<PanelWindow*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
  }
  if (!self) return DefWindowProcW(hwnd, msg, wp, lp);
  if (msg == WM_NCDESTROY) {
    SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
    self->hwnd_ = nullptr;
    return DefWindowProcW(hwnd, msg, wp, lp);
  }
  return self->HandleMessage(msg, wp, lp);
}

LRESULT PanelWindow::HandleMessage(UINT msg, WPARAM wp, LPARAM lp) {
  auto mouse_screen = [&] {
    POINT pt{GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
    ClientToScreen(hwnd_, &pt);
    return pt;
  };
  // In touch/pointer mode touch input arrives separately; skip its promoted mouse messages.
  const bool ignore_mouse = options_.input != InputMode::kMouse && compat::IsMouseFromTouchOrPen();

  switch (msg) {
    case WM_MOUSEACTIVATE:
      return MA_NOACTIVATE;
    case compat::kWmPointerActivate:
      return compat::kPaNoActivate;

    case WM_LBUTTONDOWN:
      if (ignore_mouse) return 0;
      SetCapture(hwnd_);
      PointerDown(kMousePointer, mouse_screen());
      return 0;
    case WM_MOUSEMOVE:
      if (ignore_mouse || !tracks_.count(kMousePointer)) return 0;
      PointerMove(kMousePointer, mouse_screen());
      return 0;
    case WM_LBUTTONUP:
      if (ignore_mouse) return 0;
      PointerUp(kMousePointer, mouse_screen());  // before ReleaseCapture: WM_CAPTURECHANGED cancels
      if (GetCapture() == hwnd_) ReleaseCapture();
      return 0;
    case WM_CAPTURECHANGED:
      if (reinterpret_cast<HWND>(lp) != hwnd_) PointerCancel(kMousePointer);
      return 0;

    case compat::kWmPointerDown:
    case compat::kWmPointerUpdate:
    case compat::kWmPointerUp: {
      if (options_.input != InputMode::kPointer) break;
      DWORD type = 0;
      compat::GetPointerType(compat::PointerId(wp), &type);
      if (type == compat::kPointerTypeMouse) break;  // mouse keeps using mouse messages
      const UINT32 id = compat::PointerId(wp);
      const POINT pt{GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
      if (msg == compat::kWmPointerDown) {
        PointerDown(id, pt);
      } else if (msg == compat::kWmPointerUpdate) {
        PointerMove(id, pt);
      } else if (HIWORD(wp) & compat::kPointerFlagCanceled) {
        PointerCancel(id);
      } else {
        PointerUp(id, pt);
      }
      return 0;  // not passed on: no promoted mouse messages, no press-and-hold
    }
    case compat::kWmPointerCaptureChanged:
      PointerCancel(compat::PointerId(wp));
      return 0;

    case WM_TOUCH:
      if (HandleTouch(wp, lp)) return 0;
      break;

    case WM_TIMER:
      if (wp == kAutoHideTimer) {
        KillTimer(hwnd_, kAutoHideTimer);
        if (tracks_.empty()) Hide();  // not while a finger is on the panel
        return 0;
      }
      OnTimer(wp);
      return 0;

    case kFocusMessage:
      OnFocusEvents();
      return 0;

    case kEngineMessage:
      if (wp == 1) {
        snapshot_.state.preedit = "引擎初始化失败";
        Relayout();
        InvalidateRect(hwnd_, nullptr, FALSE);
        return 0;
      }
      OnEngineSnapshots();
      return 0;

    case WM_SIZE:
      renderer_.Resize(LOWORD(lp), HIWORD(lp));
      Relayout();
      InvalidateRect(hwnd_, nullptr, FALSE);
      return 0;
    case WM_PAINT:
      Paint();
      return 0;
    case WM_ERASEBKGND:
      return 1;

    case compat::kWmDpiChanged: {
      dpi_ = LOWORD(wp);
      const RECT* r = reinterpret_cast<const RECT*>(lp);
      SetWindowPos(hwnd_, nullptr, r->left, r->top, r->right - r->left, r->bottom - r->top,
                   SWP_NOZORDER | SWP_NOACTIVATE);
      return 0;
    }
    case WM_SETTINGCHANGE:
      if (lp && lstrcmpW(reinterpret_cast<LPCWSTR>(lp), L"ImmersiveColorSet") == 0) {
        theme_ = (options_.theme < 0 ? compat::SystemPrefersDark() : options_.theme == 1) ? Theme::Dark() : Theme::Light();
        InvalidateRect(hwnd_, nullptr, FALSE);
      }
      return 0;
    case WM_DISPLAYCHANGE:
      LoadPlacement();
      return 0;
  }
  return DefWindowProcW(hwnd_, msg, wp, lp);
}

bool PanelWindow::HandleTouch(WPARAM wp, LPARAM lp) {
  const UINT count = LOWORD(wp);
  std::vector<TOUCHINPUT> inputs(count);
  const auto handle = reinterpret_cast<HTOUCHINPUT>(lp);
  if (!count || !GetTouchInputInfo(handle, count, inputs.data(), sizeof(TOUCHINPUT))) return false;
  for (const TOUCHINPUT& ti : inputs) {
    const POINT pt{TOUCH_COORD_TO_PIXEL(ti.x), TOUCH_COORD_TO_PIXEL(ti.y)};
    if (ti.dwFlags & TOUCHEVENTF_DOWN) {
      PointerDown(ti.dwID, pt);
    } else if (ti.dwFlags & TOUCHEVENTF_UP) {
      PointerUp(ti.dwID, pt);
    } else if (ti.dwFlags & TOUCHEVENTF_MOVE) {
      PointerMove(ti.dwID, pt);
    }
  }
  CloseTouchInputHandle(handle);
  return true;
}

void PanelWindow::ScreenToDip(POINT screen, float* x, float* y) const {
  ScreenToClient(hwnd_, &screen);
  *x = screen.x / Scale();
  *y = screen.y / Scale();
}

// ---------------------------------------------------------------------------
// Gestures

void PanelWindow::PointerDown(UINT32 id, POINT screen) {
  float x, y;
  ScreenToDip(screen, &x, &y);
  const Element* e = HitTest(layout_, x, y);
  if (!e) return;
  Track t;
  t.key = ElementKey::Of(*e);
  t.action = e->action;
  t.region = e->region;
  t.text = e->text;
  t.x0 = t.lx = x;
  t.y0 = t.ly = y;
  t.screen0 = screen;
  GetWindowRect(hwnd_, &t.window0);
  t.resizing = t.action == Action::kHandle && x >= layout_.handle.Right() - Metrics::kResizeGrip;
  tracks_[id] = t;
  if (on_interaction_ && t.action != Action::kHandle) on_interaction_();
  if (t.action == Action::kBackspace || t.action == Action::kKey || t.action == Action::kSpace ||
      t.action == Action::kLetter) {
    long_press_id_ = id;
    SetTimer(hwnd_, kLongPressTimer, kLongPressMs, nullptr);
  }
  InvalidateRect(hwnd_, nullptr, FALSE);
}

void PanelWindow::PointerMove(UINT32 id, POINT screen) {
  auto it = tracks_.find(id);
  if (it == tracks_.end()) return;
  Track& t = it->second;
  if (t.action == Action::kHandle && t.resizing) {
    // Top-right grip: width follows x, height grows upwards (bottom edge stays).
    const float scale = Scale();
    const int w0 = t.window0.right - t.window0.left, h0 = t.window0.bottom - t.window0.top;
    const int w = std::clamp(w0 + static_cast<int>(screen.x - t.screen0.x), static_cast<int>(kMinWidth * scale),
                             static_cast<int>(kMaxWidth * scale));
    const int h = std::clamp(h0 - static_cast<int>(screen.y - t.screen0.y), static_cast<int>(kMinHeight * scale),
                             static_cast<int>(kMaxHeight * scale));
    width_dip_ = w / scale;
    height_dip_ = h / scale;
    SetWindowPos(hwnd_, nullptr, t.window0.left, t.window0.bottom - h, w, h, SWP_NOZORDER | SWP_NOACTIVATE);
    return;
  }
  if (t.action == Action::kHandle) {
    SetWindowPos(hwnd_, nullptr, t.window0.left + screen.x - t.screen0.x,
                 t.window0.top + screen.y - t.screen0.y, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
    return;
  }
  float x, y;
  ScreenToDip(screen, &x, &y);
  if (t.region != Region::kNone) {
    const bool horizontal = t.region == Region::kCandidateBar;
    const float total = horizontal ? x - t.x0 : y - t.y0;
    if (!t.scrolling && std::fabs(total) > kScrollSlop) {
      t.scrolling = true;
      if (id == long_press_id_) KillTimer(hwnd_, kLongPressTimer);
    }
    if (t.scrolling) {
      ApplyScroll(t.region, -(horizontal ? x - t.lx : y - t.ly));
      Relayout();
    }
  } else {
    if (t.action == Action::kBackspace && x - t.x0 < -kSwipeClear && !t.long_fired) t.swipe_clear = true;
    bool inside = false;
    for (const Element& e : layout_.elements) {
      if (ElementKey::Of(e) == t.key) {
        const RectF& r = e.rect;
        inside = x >= r.x - kKeySlop && x < r.Right() + kKeySlop && y >= r.y - kKeySlop &&
                 y < r.Bottom() + kKeySlop;
      }
    }
    t.inside = inside || t.swipe_clear;
    if (!t.inside && id == long_press_id_) KillTimer(hwnd_, kLongPressTimer);
  }
  t.lx = x;
  t.ly = y;
  InvalidateRect(hwnd_, nullptr, FALSE);
}

void PanelWindow::PointerUp(UINT32 id, POINT screen) {
  auto it = tracks_.find(id);
  if (it == tracks_.end()) return;
  const Track t = it->second;
  tracks_.erase(it);
  if (id == long_press_id_) {
    KillTimer(hwnd_, kLongPressTimer);
    KillTimer(hwnd_, kRepeatTimer);
    long_press_id_ = 0;
  }
  if (t.action == Action::kHandle) {
    if (screen.x != t.screen0.x || screen.y != t.screen0.y) SavePlacement();
  } else if (t.swipe_clear) {
    Execute(Action::kClear, -1, {}, {});
  } else if (!t.scrolling && !t.long_fired && t.inside) {
    Execute(t.action, t.key.index, t.text, t.key.label);
  }
  InvalidateRect(hwnd_, nullptr, FALSE);
}

void PanelWindow::PointerCancel(UINT32 id) {
  if (tracks_.erase(id) && id == long_press_id_) {
    KillTimer(hwnd_, kLongPressTimer);
    KillTimer(hwnd_, kRepeatTimer);
    long_press_id_ = 0;
  }
  InvalidateRect(hwnd_, nullptr, FALSE);
}

void PanelWindow::OnTimer(UINT_PTR timer) {
  auto it = tracks_.find(long_press_id_);
  if (it == tracks_.end() || !it->second.inside || it->second.scrolling) {
    KillTimer(hwnd_, timer);
    return;
  }
  if (timer == kLongPressTimer) {
    KillTimer(hwnd_, kLongPressTimer);
    LongPress(it->second);
  } else if (timer == kRepeatTimer) {
    Execute(Action::kBackspace, -1, {}, {});
  }
}

void PanelWindow::LongPress(Track& t) {
  t.long_fired = true;
  switch (t.action) {
    case Action::kBackspace:
      Execute(Action::kBackspace, -1, {}, {});
      SetTimer(hwnd_, kRepeatTimer, kRepeatMs, nullptr);
      break;
    case Action::kLetter:
      TypeLetter(Utf8ToWide(t.text));  // long press: the small character on the key
      break;
    case Action::kKey:
    case Action::kSpace: {
      // Long press types the digit itself (only outside a composition).
      const std::wstring digit = Utf8ToWide(t.text);
      engine_.Post([digit](Session& s, std::vector<Passthrough>& out) {
        if (!s.HasInput()) out.push_back({0, digit});
      });
      break;
    }
    default:
      break;
  }
}

// ---------------------------------------------------------------------------
// Actions

// QWERTY letters go straight to the field, queued behind any engine output.
// A one-shot shift ends with the letter.
void PanelWindow::TypeLetter(const std::wstring& text) {
  engine_.Post([text](Session& s, std::vector<Passthrough>& out) {
    if (s.HasInput()) s.Space();
    out.push_back({0, text});
  });
  if (shift_ && !caps_lock_) {
    shift_ = false;
    Relayout();
  }
}

void PanelWindow::Execute(Action action, int index, const std::string& text, const std::wstring& label) {
  switch (action) {
    case Action::kLetter:
      TypeLetter(label);
      break;
    case Action::kShift: {
      const ULONGLONG now = GetTickCount64();
      if (caps_lock_) {
        caps_lock_ = shift_ = false;
      } else if (shift_ && now - shift_tick_ < kCapsLockMs) {
        caps_lock_ = true;
      } else {
        shift_ = !shift_;
      }
      shift_tick_ = now;
      Relayout();
      break;
    }
    case Action::kKey: {
      const char key = text.empty() ? 0 : text[0];
      engine_.Post([key](Session& s, std::vector<Passthrough>&) { s.Key(key); });
      break;
    }
    case Action::kText:
      engine_.Post([label](Session& s, std::vector<Passthrough>& out) {
        if (s.HasInput()) s.Space();  // commit the composition first
        out.push_back({0, label});
      });
      break;
    case Action::kBackspace:
      engine_.Post([](Session& s, std::vector<Passthrough>& out) {
        if (s.HasInput()) s.Backspace(); else out.push_back({VK_BACK, {}});
      });
      break;
    case Action::kClear:
      engine_.Post([](Session& s, std::vector<Passthrough>&) { s.Escape(); });
      break;
    case Action::kSpace:
      engine_.Post([](Session& s, std::vector<Passthrough>& out) {
        if (s.HasInput()) s.Space(); else out.push_back({0, L" "});
      });
      break;
    case Action::kEnter:
      engine_.Post([](Session& s, std::vector<Passthrough>& out) {
        if (s.HasInput()) s.Enter(); else out.push_back({VK_RETURN, {}});
      });
      break;
    case Action::kSymbols:
      SetMode(Mode::kSymbol);
      break;
    case Action::kNumbers:
      SetMode(Mode::kNumber);
      break;
    case Action::kBack:
      SetMode(return_mode_);
      break;
    case Action::kToggleLanguage:
      SetMode(mode_ == Mode::kChinese ? Mode::kEnglish : Mode::kChinese);
      break;
    case Action::kHide:
      Hide();
      break;
    case Action::kExpand:
      expanded_ = !expanded_;
      grid_scroll_ = 0;
      Relayout();
      break;
    case Action::kCandidate:
      expanded_ = false;
      engine_.Post([index](Session& s, std::vector<Passthrough>&) { s.SelectCandidate(index); });
      break;
    case Action::kPinyin:
      engine_.Post([index](Session& s, std::vector<Passthrough>&) { s.PickBar(index); });
      break;
    case Action::kSymbolCategory:
      symbol_category_ = index;
      grid_scroll_ = 0;
      Relayout();
      break;
    default:
      break;
  }
}

void PanelWindow::SetMode(Mode mode, bool remember) {
  if (mode == mode_) return;
  const Mode old = mode_;
  if ((mode == Mode::kNumber || mode == Mode::kSymbol) && old != Mode::kNumber && old != Mode::kSymbol) {
    return_mode_ = old;
  }
  mode_ = mode;
  if (remember && (mode == Mode::kChinese || mode == Mode::kEnglish)) text_mode_ = mode;
  shift_ = caps_lock_ = false;
  expanded_ = false;
  side_scroll_ = grid_scroll_ = candidate_scroll_ = 0;
  // Only the Chinese nine-key layout composes: commit whatever is pending.
  engine_.Post([](Session& s, std::vector<Passthrough>&) {
    if (s.HasInput()) s.Space();
  });
  Relayout();
  InvalidateRect(hwnd_, nullptr, FALSE);
}

// ---------------------------------------------------------------------------
// Engine state -> view

void PanelWindow::OnEngineSnapshots() {
  auto snapshots = engine_.Take();
  if (snapshots.empty()) return;
  const std::string old_input = snapshot_.state.input;
  for (EngineSnapshot& snap : snapshots) {
    if (!snap.state.commit.empty()) Output(Utf8ToWide(snap.state.commit));
    for (const Passthrough& p : snap.passthrough) {
      if (p.vk) SendVirtualKey(p.vk); else Output(p.text);
    }
  }
  snapshot_ = std::move(snapshots.back());
  if (snapshot_.state.input != old_input) {
    candidate_scroll_ = side_scroll_ = grid_scroll_ = 0;
    if (snapshot_.state.input.empty()) expanded_ = false;
  }
  Relayout();
  InvalidateRect(hwnd_, nullptr, FALSE);
}

// Through the focused TIP (TSF edit session) when possible; SendInput otherwise
// (applications without TSF, or before the IME is active in the target).
void PanelWindow::Output(const std::wstring& text) {
  if (text.empty()) return;
  if (deliver_ && deliver_(text)) {
    ++pushed_;
    return;
  }
  ++sent_;
  SendText(text);
}

std::vector<std::wstring> PanelWindow::SideItems() const {
  switch (mode_) {
    case Mode::kChinese:
      if (composing()) {
        std::vector<std::wstring> items;
        for (const auto& b : snapshot_.state.pinyin_bar) items.push_back(Utf8ToWide(b.spelling));
        return items;
      }
      return kChinesePunct;
    case Mode::kEnglish:
      return {};
    case Mode::kNumber:
      return kNumberSymbols;
    case Mode::kSymbol:
      return kSymbolCategories;
  }
  return {};
}

void PanelWindow::Relayout() {
  if (!hwnd_) return;
  RECT rc;
  GetClientRect(hwnd_, &rc);
  dpi_ = compat::DpiForWindow(hwnd_);
  LayoutInput in;
  in.width = (rc.right - rc.left) / Scale();
  in.height = (rc.bottom - rc.top) / Scale();
  in.mode = mode_;
  in.composing = composing();
  in.expanded = expanded_;
  in.shift = shift_;
  in.caps_lock = caps_lock_;
  in.preedit = Utf8ToWide(snapshot_.state.preedit);
  const auto& list = snapshot_.candidates.empty() ? snapshot_.state.page : snapshot_.candidates;
  for (const Candidate& c : list) in.candidates.push_back(Utf8ToWide(c.text));
  in.highlighted = snapshot_.state.page_no == 0 ? snapshot_.state.highlighted : -1;
  in.side_items = SideItems();
  in.side_selected = mode_ == Mode::kSymbol ? symbol_category_ : -1;
  if (mode_ == Mode::kSymbol) in.symbols = SymbolsFor(symbol_category_);
  in.candidate_scroll = candidate_scroll_;
  in.side_scroll = side_scroll_;
  in.grid_scroll = grid_scroll_;
  in.measure = [this](const std::wstring& s) { return renderer_.MeasureCandidate(s); };
  layout_ = BuildLayout(in);
}

void PanelWindow::ApplyScroll(Region region, float delta) {
  float* value = nullptr;
  float content = 0;
  switch (region) {
    case Region::kCandidateBar: value = &candidate_scroll_; content = layout_.candidate_content; break;
    case Region::kSideList: value = &side_scroll_; content = layout_.side_content; break;
    case Region::kGrid: value = &grid_scroll_; content = layout_.grid_content; break;
    default: return;
  }
  float visible = 0;
  for (const RegionRect& r : layout_.regions) {
    if (r.region == region) visible = r.horizontal ? r.rect.w : r.rect.h;
  }
  *value = std::clamp(*value + delta, 0.f, std::max(0.f, content - visible));
}

void PanelWindow::Paint() {
  PAINTSTRUCT ps;
  BeginPaint(hwnd_, &ps);
  std::vector<ElementKey> pressed;
  for (const auto& [id, t] : tracks_) {
    if (t.action != Action::kHandle && t.inside && !t.scrolling) pressed.push_back(t.key);
  }
  renderer_.Render(hwnd_, dpi_, layout_, theme_, pressed);
  EndPaint(hwnd_, &ps);
  if (!options_.dump_layout.empty()) DumpLayout();
}

// Test hook: element centers in screen pixels, as JSON.
void PanelWindow::DumpLayout() {
  static unsigned seq = 0;
  RECT wr;
  GetWindowRect(hwnd_, &wr);
  std::string json = "{\"seq\":" + std::to_string(++seq) + ",\"visible\":" + (visible() ? "true" : "false") +
                     ",\"focus\":\"" + JsonEscape(last_focus_) + "\",\"pushed\":" + std::to_string(pushed_) + ",\"sent\":" + std::to_string(sent_) +
                     ",\"input\":\"" + JsonEscape(snapshot_.state.input) + "\",\"window\":[" +
                     std::to_string(wr.left) + "," + std::to_string(wr.top) + "," +
                     std::to_string(wr.right) + "," + std::to_string(wr.bottom) + "],\"elements\":[";
  bool first = true;
  for (const Element& e : layout_.elements) {
    POINT pt{static_cast<LONG>((e.rect.x + e.rect.w / 2) * Scale()),
             static_cast<LONG>((e.rect.y + e.rect.h / 2) * Scale())};
    ClientToScreen(hwnd_, &pt);
    json += std::string(first ? "" : ",") + "{\"action\":" + std::to_string(static_cast<int>(e.action)) +
            ",\"index\":" + std::to_string(e.index) + ",\"label\":\"" + JsonEscape(WideToUtf8(e.label)) +
            "\",\"text\":\"" + JsonEscape(e.text) + "\",\"x\":" + std::to_string(pt.x) +
            ",\"y\":" + std::to_string(pt.y) + "}";
    first = false;
  }
  json += "]}";
  const std::wstring tmp = options_.dump_layout + L".tmp";
  HANDLE f = CreateFileW(tmp.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (f == INVALID_HANDLE_VALUE) return;
  DWORD written = 0;
  WriteFile(f, json.data(), static_cast<DWORD>(json.size()), &written, nullptr);
  CloseHandle(f);
  MoveFileExW(tmp.c_str(), options_.dump_layout.c_str(), MOVEFILE_REPLACE_EXISTING);
}

// ---------------------------------------------------------------------------
// Placement

void PanelWindow::PlaceDefault() {
  HMONITOR monitor = MonitorFromWindow(GetForegroundWindow(), MONITOR_DEFAULTTOPRIMARY);
  MONITORINFO mi = {sizeof(mi)};
  GetMonitorInfoW(monitor, &mi);
  const RECT& work = mi.rcWork;
  POINT center{(work.left + work.right) / 2, (work.top + work.bottom) / 2};
  const float scale = compat::DpiForPoint(center) / 96.f;
  const int w = std::min(static_cast<int>(width_dip_ * scale), static_cast<int>(work.right - work.left));
  const int h = std::min(static_cast<int>(height_dip_ * scale), static_cast<int>(work.bottom - work.top));
  SetWindowPos(hwnd_, nullptr, center.x - w / 2, work.bottom - h, w, h, SWP_NOZORDER | SWP_NOACTIVATE);
}

void PanelWindow::LoadPlacement() {
  if (options_.settings_file.empty()) {
    PlaceDefault();
    return;
  }
  const wchar_t* file = options_.settings_file.c_str();
  const int x = GetPrivateProfileIntW(L"panel", L"x", INT_MIN, file);
  const int y = GetPrivateProfileIntW(L"panel", L"y", INT_MIN, file);
  width_dip_ = static_cast<float>(GetPrivateProfileIntW(L"panel", L"width", 400, file));
  height_dip_ = static_cast<float>(GetPrivateProfileIntW(L"panel", L"height", 300, file));
  width_dip_ = std::clamp(width_dip_, kMinWidth, kMaxWidth);
  height_dip_ = std::clamp(height_dip_, kMinHeight, kMaxHeight);
  if (x == INT_MIN || y == INT_MIN) {
    PlaceDefault();
    return;
  }
  const float scale = compat::DpiForPoint({x, y}) / 96.f;
  RECT r{x, y, x + static_cast<int>(width_dip_ * scale), y + static_cast<int>(height_dip_ * scale)};
  if (!MonitorFromRect(&r, MONITOR_DEFAULTTONULL)) {
    PlaceDefault();
    return;
  }
  SetWindowPos(hwnd_, nullptr, r.left, r.top, r.right - r.left, r.bottom - r.top,
               SWP_NOZORDER | SWP_NOACTIVATE);
}

void PanelWindow::SavePlacement() {
  if (options_.settings_file.empty()) return;
  RECT r;
  GetWindowRect(hwnd_, &r);
  const wchar_t* file = options_.settings_file.c_str();
  WritePrivateProfileStringW(L"panel", L"x", std::to_wstring(r.left).c_str(), file);
  WritePrivateProfileStringW(L"panel", L"y", std::to_wstring(r.top).c_str(), file);
  WritePrivateProfileStringW(L"panel", L"width", std::to_wstring(static_cast<int>(width_dip_)).c_str(), file);
  WritePrivateProfileStringW(L"panel", L"height", std::to_wstring(static_cast<int>(height_dip_)).c_str(), file);
}

}  // namespace t9ime::panel
