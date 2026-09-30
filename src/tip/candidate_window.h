#pragma once
// Physical-keyboard candidate window, drawn inside the application process
// (like Weasel) so it follows the application's z-order band and DPI. A small
// Direct2D 1.0 + DirectWrite popup; never activates.

#include <d2d1.h>
#include <dwrite.h>
#include <windows.h>

#include <functional>
#include <string>
#include <vector>

namespace t9ime::tip {

struct CandidateView {
  std::vector<std::wstring> texts;
  std::vector<std::wstring> comments;
  int highlighted = 0;
  bool has_prev = false, has_next = false;
};

class CandidateWindow {
 public:
  using SelectFn = std::function<void(int index)>;
  using PageFn = std::function<void(bool backward)>;

  CandidateWindow() = default;
  CandidateWindow(const CandidateWindow&) = delete;
  CandidateWindow& operator=(const CandidateWindow&) = delete;
  ~CandidateWindow();

  void SetCallbacks(SelectFn select, PageFn page) {
    on_select_ = std::move(select);
    on_page_ = std::move(page);
  }
  // Shows the list next to `caret` (screen rect of the composition start).
  void Show(const CandidateView& view, const RECT& caret, HWND owner);
  void Move(const RECT& caret);
  void Hide();
  void Destroy();
  bool visible() const { return hwnd_ && IsWindowVisible(hwnd_); }
  HWND hwnd() const { return hwnd_; }

 private:
  static LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);
  LRESULT Handle(UINT msg, WPARAM wp, LPARAM lp);
  bool EnsureWindow(HWND owner);
  bool EnsureResources();
  void Layout();
  void Place();
  void Paint();
  int HitTest(int x, int y) const;

  HWND hwnd_ = nullptr;
  HWND owner_ = nullptr;
  ID2D1Factory* d2d_ = nullptr;
  IDWriteFactory* dwrite_ = nullptr;
  IDWriteTextFormat* text_format_ = nullptr;
  IDWriteTextFormat* comment_format_ = nullptr;
  ID2D1HwndRenderTarget* target_ = nullptr;
  ID2D1SolidColorBrush* brush_ = nullptr;

  CandidateView view_;
  RECT caret_{};
  UINT dpi_ = 96;
  std::vector<D2D1_RECT_F> cells_;  // DIPs, one per candidate
  float width_ = 0, height_ = 0;    // DIPs
  SelectFn on_select_;
  PageFn on_page_;
};

}  // namespace t9ime::tip
