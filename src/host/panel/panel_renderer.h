#pragma once
// Direct2D 1.0 (HwndRenderTarget) + DirectWrite renderer for the touch panel.
// Works on Windows 7 SP1 with the platform update. Coordinates are DIPs.

#include <d2d1.h>
#include <dwrite.h>
#include <windows.h>

#include <string>
#include <vector>

#include "panel_layout.h"

namespace t9ime::panel {

// Keys and background are vertical gradients (first color at the top).
struct Theme {
  D2D1_COLOR_F background, key, function_key, pressed, text, subtext, accent, strip, candidate_pressed;
  D2D1_COLOR_F background_bottom, key_bottom, function_key_bottom, shadow, function_text;
  static Theme Light();
  static Theme Dark();
};

// Identifies an element across relayouts (for the pressed state).
struct ElementKey {
  Action action = Action::kNone;
  int index = -1;
  std::wstring label;
  bool operator==(const ElementKey&) const = default;
  static ElementKey Of(const Element& e) { return {e.action, e.index, e.label}; }
};

class PanelRenderer {
 public:
  PanelRenderer() = default;
  PanelRenderer(const PanelRenderer&) = delete;
  PanelRenderer& operator=(const PanelRenderer&) = delete;
  ~PanelRenderer();

  bool Initialize();
  void Render(HWND hwnd, UINT dpi, const Layout& layout, const Theme& theme,
              const std::vector<ElementKey>& pressed);
  void Resize(UINT width_px, UINT height_px);
  // Width of candidate text in DIPs.
  float MeasureCandidate(const std::wstring& text);

 private:
  bool EnsureTarget(HWND hwnd, UINT dpi);
  void DrawText(const std::wstring& text, IDWriteTextFormat* format, const D2D1_RECT_F& rect,
                const D2D1_COLOR_F& color);
  IDWriteTextFormat* CreateFormat(float size, DWRITE_TEXT_ALIGNMENT align, bool bold = false);
  void ReleaseTarget();

  ID2D1Factory* d2d_ = nullptr;
  IDWriteFactory* dwrite_ = nullptr;
  ID2D1HwndRenderTarget* target_ = nullptr;
  ID2D1SolidColorBrush* brush_ = nullptr;
  // Recreated when the theme changes; start / end points are set per shape.
  ID2D1LinearGradientBrush* Gradient(const D2D1_COLOR_F& top, const D2D1_COLOR_F& bottom, float y0, float y1);
  ID2D1LinearGradientBrush* gradients_[3] = {};
  D2D1_COLOR_F gradient_colors_[3][2] = {};
  IDWriteTextFormat* key_format_ = nullptr;
  IDWriteTextFormat* sub_format_ = nullptr;
  IDWriteTextFormat* function_format_ = nullptr;
  IDWriteTextFormat* candidate_format_ = nullptr;
  IDWriteTextFormat* side_format_ = nullptr;
  IDWriteTextFormat* strip_format_ = nullptr;
  IDWriteTextFormat* preview_format_ = nullptr;
  std::wstring font_family_;
  bool color_fonts_ = false;
};

}  // namespace t9ime::panel
