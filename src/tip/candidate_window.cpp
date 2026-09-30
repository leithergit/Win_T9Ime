#include "candidate_window.h"

#include <windowsx.h>

#include <algorithm>

#include "globals.h"
#include "win_compat.h"

namespace t9ime::tip {

namespace {

constexpr wchar_t kClassName[] = L"T9Ime.TipCandidate";
// WinEvents for IME candidate windows (Windows 8+ SDK; harmless on Windows 7).
constexpr DWORD kEventImeShow = 0x8027, kEventImeHide = 0x8028, kEventImeChange = 0x8029;
constexpr float kPadX = 8, kPadY = 6, kGap = 12, kLabelGap = 3, kHeight = 34;
constexpr float kTextSize = 17, kCommentSize = 13;

template <typename T>
void SafeRelease(T*& p) {
  if (p) {
    p->Release();
    p = nullptr;
  }
}

D2D1_COLOR_F Rgb(UINT32 rgb) {
  return D2D1::ColorF((rgb >> 16 & 0xFF) / 255.f, (rgb >> 8 & 0xFF) / 255.f, (rgb & 0xFF) / 255.f);
}

float TextWidth(IDWriteFactory* f, IDWriteTextFormat* fmt, const std::wstring& s) {
  if (s.empty()) return 0;
  IDWriteTextLayout* layout = nullptr;
  if (FAILED(f->CreateTextLayout(s.c_str(), static_cast<UINT32>(s.size()), fmt, 4000, 100, &layout))) return 0;
  DWRITE_TEXT_METRICS m = {};
  layout->GetMetrics(&m);
  layout->Release();
  return m.widthIncludingTrailingWhitespace;
}

std::wstring Label(size_t i) { return std::to_wstring((i + 1) % 10) + L"."; }

}  // namespace

CandidateWindow::~CandidateWindow() {
  Destroy();
  SafeRelease(text_format_);
  SafeRelease(comment_format_);
  SafeRelease(dwrite_);
  SafeRelease(d2d_);
}

bool CandidateWindow::EnsureResources() {
  if (!d2d_ && FAILED(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, &d2d_))) return false;
  if (!dwrite_ && FAILED(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory),
                                             reinterpret_cast<IUnknown**>(&dwrite_)))) {
    return false;
  }
  if (!text_format_) {
    const wchar_t* family = L"Microsoft YaHei";
    IDWriteFontCollection* fonts = nullptr;
    if (SUCCEEDED(dwrite_->GetSystemFontCollection(&fonts, FALSE))) {
      UINT32 index = 0;
      BOOL exists = FALSE;
      if (SUCCEEDED(fonts->FindFamilyName(L"Microsoft YaHei UI", &index, &exists)) && exists) {
        family = L"Microsoft YaHei UI";
      }
      fonts->Release();
    }
    for (auto [fmt, size] : {std::pair{&text_format_, kTextSize}, std::pair{&comment_format_, kCommentSize}}) {
      if (FAILED(dwrite_->CreateTextFormat(family, nullptr, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL,
                                           DWRITE_FONT_STRETCH_NORMAL, size, L"zh-cn", fmt))) {
        return false;
      }
      (*fmt)->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
      (*fmt)->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
    }
  }
  return true;
}

bool CandidateWindow::EnsureWindow(HWND owner) {
  if (hwnd_ && owner == owner_) return true;
  Destroy();
  WNDCLASSEXW wc = {sizeof(wc)};
  if (!GetClassInfoExW(g_module, kClassName, &wc)) {
    wc = {sizeof(wc)};
    wc.style = CS_IME;
    wc.lpfnWndProc = WndProc;
    wc.hInstance = g_module;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.lpszClassName = kClassName;
    if (!RegisterClassExW(&wc)) return false;
  }
  owner_ = owner;
  hwnd_ = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_TOPMOST | WS_EX_NOACTIVATE, kClassName, L"", WS_POPUP, 0, 0, 1, 1,
                          owner, nullptr, g_module, this);
  return hwnd_ != nullptr;
}

void CandidateWindow::Destroy() {
  SafeRelease(brush_);
  SafeRelease(target_);
  if (hwnd_) {
    DestroyWindow(hwnd_);
    hwnd_ = nullptr;
  }
  owner_ = nullptr;
}

void CandidateWindow::Show(const CandidateView& view, const RECT& caret, HWND owner) {
  if (view.texts.empty() || !EnsureResources() || !EnsureWindow(owner)) {
    Hide();
    return;
  }
  view_ = view;
  caret_ = caret;
  Layout();
  Place();
  if (!IsWindowVisible(hwnd_)) {
    ShowWindow(hwnd_, SW_SHOWNOACTIVATE);
    NotifyWinEvent(kEventImeShow, hwnd_, OBJID_CLIENT, CHILDID_SELF);
  } else {
    NotifyWinEvent(kEventImeChange, hwnd_, OBJID_CLIENT, CHILDID_SELF);
  }
  InvalidateRect(hwnd_, nullptr, FALSE);
}

void CandidateWindow::Move(const RECT& caret) {
  caret_ = caret;
  if (visible()) Place();
}

void CandidateWindow::Hide() {
  if (hwnd_ && IsWindowVisible(hwnd_)) {
    ShowWindow(hwnd_, SW_HIDE);
    NotifyWinEvent(kEventImeHide, hwnd_, OBJID_CLIENT, CHILDID_SELF);
  }
}

void CandidateWindow::Layout() {
  cells_.clear();
  float x = kPadX;
  for (size_t i = 0; i < view_.texts.size(); ++i) {
    float w = TextWidth(dwrite_, comment_format_, Label(i)) + kLabelGap + TextWidth(dwrite_, text_format_, view_.texts[i]);
    if (i < view_.comments.size() && !view_.comments[i].empty()) {
      w += kLabelGap + TextWidth(dwrite_, comment_format_, view_.comments[i]);
    }
    cells_.push_back(D2D1::RectF(x - kGap / 2, kPadY, x + w + kGap / 2, kPadY + kHeight));
    x += w + kGap;
  }
  width_ = x - kGap + kPadX;
  height_ = kHeight + 2 * kPadY;
}

void CandidateWindow::Place() {
  // Move first, then ask the window for its DPI: it reflects the application's
  // DPI awareness (unaware apps get 96 and are scaled by the system).
  SetWindowPos(hwnd_, HWND_TOPMOST, caret_.left, caret_.bottom + 2, 0, 0, SWP_NOACTIVATE | SWP_NOSIZE);
  dpi_ = compat::DpiForWindow(hwnd_);
  const float scale = dpi_ / 96.f;
  const int w = static_cast<int>(width_ * scale + 0.5f), h = static_cast<int>(height_ * scale + 0.5f);
  MONITORINFO mi = {sizeof(mi)};
  GetMonitorInfoW(MonitorFromRect(&caret_, MONITOR_DEFAULTTONEAREST), &mi);
  const RECT& work = mi.rcWork;
  int x = caret_.left, y = caret_.bottom + 2;
  if (y + h > work.bottom) y = caret_.top - h - 2;  // no room below: above the text
  x = std::clamp(x, static_cast<int>(work.left), std::max(static_cast<int>(work.left), static_cast<int>(work.right) - w));
  y = std::max(y, static_cast<int>(work.top));
  SetWindowPos(hwnd_, HWND_TOPMOST, x, y, w, h, SWP_NOACTIVATE);
  if (target_) target_->Resize(D2D1::SizeU(w, h));
}

int CandidateWindow::HitTest(int x, int y) const {
  const float scale = dpi_ / 96.f;
  const float fx = x / scale, fy = y / scale;
  for (size_t i = 0; i < cells_.size(); ++i) {
    if (fx >= cells_[i].left && fx < cells_[i].right && fy >= cells_[i].top && fy < cells_[i].bottom) {
      return static_cast<int>(i);
    }
  }
  return -1;
}

void CandidateWindow::Paint() {
  if (!target_) {
    RECT rc;
    GetClientRect(hwnd_, &rc);
    const auto props = D2D1::RenderTargetProperties(D2D1_RENDER_TARGET_TYPE_DEFAULT, D2D1::PixelFormat(),
                                                    static_cast<float>(dpi_), static_cast<float>(dpi_));
    if (FAILED(d2d_->CreateHwndRenderTarget(props, D2D1::HwndRenderTargetProperties(hwnd_, D2D1::SizeU(rc.right, rc.bottom)),
                                            &target_)) ||
        FAILED(target_->CreateSolidColorBrush(D2D1::ColorF(0), &brush_))) {
      SafeRelease(target_);
      return;
    }
  }
  target_->SetDpi(static_cast<float>(dpi_), static_cast<float>(dpi_));
  const bool dark = compat::SystemPrefersDark();
  const auto bg = Rgb(dark ? 0x2B2D31 : 0xFFFFFF), border = Rgb(dark ? 0x4A4D55 : 0xC9CDD4);
  const auto text = Rgb(dark ? 0xEDEEF0 : 0x16181C), sub = Rgb(dark ? 0x9CA1AA : 0x6B7079);
  const auto hl_bg = Rgb(dark ? 0x2F5DA8 : 0x1E6FD9), hl_text = Rgb(0xFFFFFF);

  target_->BeginDraw();
  target_->Clear(bg);
  brush_->SetColor(border);
  target_->DrawRectangle(D2D1::RectF(0.5f, 0.5f, width_ - 0.5f, height_ - 0.5f), brush_);
  for (size_t i = 0; i < cells_.size(); ++i) {
    const bool hl = static_cast<int>(i) == view_.highlighted;
    D2D1_RECT_F r = cells_[i];
    if (hl) {
      brush_->SetColor(hl_bg);
      target_->FillRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(r.left + 2, r.top + 2, r.right - 2, r.bottom - 2), 4, 4),
                                    brush_);
    }
    float x = r.left + kGap / 2;
    auto draw = [&](const std::wstring& s, IDWriteTextFormat* fmt, D2D1_COLOR_F color) {
      const float w = TextWidth(dwrite_, fmt, s);
      brush_->SetColor(color);
      target_->DrawText(s.c_str(), static_cast<UINT32>(s.size()), fmt, D2D1::RectF(x, r.top, x + w + 1, r.bottom), brush_,
                        D2D1_DRAW_TEXT_OPTIONS_CLIP);
      x += w + kLabelGap;
    };
    draw(Label(i), comment_format_, hl ? hl_text : sub);
    draw(view_.texts[i], text_format_, hl ? hl_text : text);
    if (i < view_.comments.size() && !view_.comments[i].empty()) draw(view_.comments[i], comment_format_, hl ? hl_text : sub);
  }
  if (target_->EndDraw() == D2DERR_RECREATE_TARGET) {
    SafeRelease(brush_);
    SafeRelease(target_);
  }
}

LRESULT CALLBACK CandidateWindow::WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
  if (msg == WM_NCCREATE) {
    SetWindowLongPtrW(hwnd, GWLP_USERDATA,
                      reinterpret_cast<LONG_PTR>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams));
  }
  auto* self = reinterpret_cast<CandidateWindow*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
  if (self && self->hwnd_ == hwnd) return self->Handle(msg, wp, lp);
  return DefWindowProcW(hwnd, msg, wp, lp);
}

LRESULT CandidateWindow::Handle(UINT msg, WPARAM wp, LPARAM lp) {
  switch (msg) {
    case WM_MOUSEACTIVATE:
      return MA_NOACTIVATE;
    case compat::kWmPointerActivate:
      return compat::kPaNoActivate;
    case WM_PAINT: {
      PAINTSTRUCT ps;
      BeginPaint(hwnd_, &ps);
      Paint();
      EndPaint(hwnd_, &ps);
      return 0;
    }
    case WM_ERASEBKGND:
      return 1;
    case WM_LBUTTONUP: {
      const int index = HitTest(GET_X_LPARAM(lp), GET_Y_LPARAM(lp));
      if (index >= 0 && on_select_) on_select_(index);
      return 0;
    }
    case WM_MOUSEWHEEL:
      if (on_page_) on_page_(GET_WHEEL_DELTA_WPARAM(wp) > 0);
      return 0;
  }
  return DefWindowProcW(hwnd_, msg, wp, lp);
}

}  // namespace t9ime::tip
