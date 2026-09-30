#include "panel_renderer.h"

#include <algorithm>

#include "win_compat.h"

namespace t9ime::panel {

namespace {

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

D2D1_RECT_F ToD2D(const RectF& r) { return D2D1::RectF(r.x, r.y, r.Right(), r.Bottom()); }

constexpr float kKeyRadius = 6;
constexpr D2D1_DRAW_TEXT_OPTIONS kColorFontOption = static_cast<D2D1_DRAW_TEXT_OPTIONS>(4);

}  // namespace

Theme Theme::Light() {
  return {Rgb(0xD5D8DE), Rgb(0xFFFFFF), Rgb(0xB5BAC3), Rgb(0x9DA3AD), Rgb(0x16181C),
          Rgb(0x6B7079), Rgb(0x1E6FD9), Rgb(0xC7CBD2), Rgb(0xDCE8FA)};
}

Theme Theme::Dark() {
  return {Rgb(0x1C1E22), Rgb(0x3A3E45), Rgb(0x2A2D32), Rgb(0x5C626C), Rgb(0xEDEEF0),
          Rgb(0x9CA1AA), Rgb(0x5AA2FF), Rgb(0x25282D), Rgb(0x2B3A52)};
}

PanelRenderer::~PanelRenderer() {
  ReleaseTarget();
  SafeRelease(key_format_);
  SafeRelease(sub_format_);
  SafeRelease(function_format_);
  SafeRelease(candidate_format_);
  SafeRelease(side_format_);
  SafeRelease(strip_format_);
  SafeRelease(dwrite_);
  SafeRelease(d2d_);
}

bool PanelRenderer::Initialize() {
  if (FAILED(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, &d2d_))) return false;
  if (FAILED(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory),
                                 reinterpret_cast<IUnknown**>(&dwrite_)))) {
    return false;
  }
  // First installed family: "Microsoft YaHei UI" (Windows 8+), "Microsoft YaHei" (Windows 7).
  font_family_ = L"SimSun";
  IDWriteFontCollection* fonts = nullptr;
  if (SUCCEEDED(dwrite_->GetSystemFontCollection(&fonts, FALSE))) {
    for (const wchar_t* name : {L"Microsoft YaHei UI", L"Microsoft YaHei"}) {
      UINT32 index = 0;
      BOOL exists = FALSE;
      if (SUCCEEDED(fonts->FindFamilyName(name, &index, &exists)) && exists) {
        font_family_ = name;
        break;
      }
    }
    fonts->Release();
  }
  // Color glyphs (emoji) need Windows 8.1+; emoji are disabled on Windows 7 anyway.
  color_fonts_ = compat::Os().AtLeastWin10();

  key_format_ = CreateFormat(20, DWRITE_TEXT_ALIGNMENT_CENTER);
  sub_format_ = CreateFormat(11, DWRITE_TEXT_ALIGNMENT_CENTER);
  function_format_ = CreateFormat(16, DWRITE_TEXT_ALIGNMENT_CENTER);
  candidate_format_ = CreateFormat(19, DWRITE_TEXT_ALIGNMENT_CENTER);
  side_format_ = CreateFormat(16, DWRITE_TEXT_ALIGNMENT_CENTER);
  strip_format_ = CreateFormat(13, DWRITE_TEXT_ALIGNMENT_LEADING);
  return key_format_ && sub_format_ && function_format_ && candidate_format_ && side_format_ && strip_format_;
}

IDWriteTextFormat* PanelRenderer::CreateFormat(float size, DWRITE_TEXT_ALIGNMENT align, bool bold) {
  IDWriteTextFormat* f = nullptr;
  if (FAILED(dwrite_->CreateTextFormat(font_family_.c_str(), nullptr,
                                       bold ? DWRITE_FONT_WEIGHT_SEMI_BOLD : DWRITE_FONT_WEIGHT_NORMAL,
                                       DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, size,
                                       L"zh-cn", &f))) {
    return nullptr;
  }
  f->SetTextAlignment(align);
  f->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
  f->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
  return f;
}

float PanelRenderer::MeasureCandidate(const std::wstring& text) {
  IDWriteTextLayout* layout = nullptr;
  if (FAILED(dwrite_->CreateTextLayout(text.c_str(), static_cast<UINT32>(text.size()), candidate_format_,
                                       10000, 100, &layout))) {
    return 20.f * text.size();
  }
  DWRITE_TEXT_METRICS m = {};
  layout->GetMetrics(&m);
  layout->Release();
  return m.widthIncludingTrailingWhitespace;
}

void PanelRenderer::ReleaseTarget() {
  SafeRelease(brush_);
  SafeRelease(target_);
}

bool PanelRenderer::EnsureTarget(HWND hwnd, UINT dpi) {
  if (!target_) {
    RECT rc;
    GetClientRect(hwnd, &rc);
    const auto props = D2D1::RenderTargetProperties(D2D1_RENDER_TARGET_TYPE_DEFAULT,
                                                    D2D1::PixelFormat(), static_cast<float>(dpi),
                                                    static_cast<float>(dpi));
    const auto hwnd_props = D2D1::HwndRenderTargetProperties(
        hwnd, D2D1::SizeU(rc.right - rc.left, rc.bottom - rc.top));
    if (FAILED(d2d_->CreateHwndRenderTarget(props, hwnd_props, &target_))) return false;
    if (FAILED(target_->CreateSolidColorBrush(D2D1::ColorF(0), &brush_))) {
      ReleaseTarget();
      return false;
    }
  }
  target_->SetDpi(static_cast<float>(dpi), static_cast<float>(dpi));
  return true;
}

void PanelRenderer::Resize(UINT width_px, UINT height_px) {
  if (target_) target_->Resize(D2D1::SizeU(width_px, height_px));
}

void PanelRenderer::DrawText(const std::wstring& text, IDWriteTextFormat* format, const D2D1_RECT_F& rect,
                             const D2D1_COLOR_F& color) {
  if (text.empty() || !format) return;
  brush_->SetColor(color);
  target_->DrawText(text.c_str(), static_cast<UINT32>(text.size()), format, rect, brush_,
                    color_fonts_ ? static_cast<D2D1_DRAW_TEXT_OPTIONS>(kColorFontOption | D2D1_DRAW_TEXT_OPTIONS_CLIP)
                                 : D2D1_DRAW_TEXT_OPTIONS_CLIP);
}

void PanelRenderer::Render(HWND hwnd, UINT dpi, const Layout& layout, const Theme& theme,
                           const std::vector<ElementKey>& pressed) {
  if (!EnsureTarget(hwnd, dpi)) return;
  target_->BeginDraw();
  target_->SetTransform(D2D1::Matrix3x2F::Identity());
  target_->Clear(theme.background);

  // Handle strip.
  brush_->SetColor(theme.strip);
  target_->FillRectangle(ToD2D(layout.handle), brush_);

  for (const Element& e : layout.elements) {
    const bool is_pressed = std::find(pressed.begin(), pressed.end(), ElementKey::Of(e)) != pressed.end();
    const bool clipped = e.region != Region::kNone;
    if (clipped) {
      for (const RegionRect& r : layout.regions) {
        if (r.region == e.region) {
          target_->PushAxisAlignedClip(ToD2D(r.rect), D2D1_ANTIALIAS_MODE_ALIASED);
          break;
        }
      }
    }
    const D2D1_RECT_F rect = ToD2D(e.rect);
    switch (e.action) {
      case Action::kHandle: {
        // Grip dots in the middle, preedit on the left.
        brush_->SetColor(theme.subtext);
        const float cx = e.rect.x + e.rect.w / 2, cy = e.rect.y + e.rect.h / 2;
        target_->FillRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(cx - 18, cy - 2, cx + 18, cy + 2), 2, 2),
                                      brush_);
        DrawText(e.label, strip_format_, D2D1::RectF(rect.left + 8, rect.top, cx - 24, rect.bottom), theme.text);
        // Resize grip: three diagonal strokes in the top-right corner.
        const float gx = rect.right - 8, gy = rect.top + 5;
        for (int i = 1; i <= 3; ++i) {
          const float d = 4.f * i;
          target_->DrawLine(D2D1::Point2F(gx - d, gy), D2D1::Point2F(gx, gy + d), brush_, 1.2f);
        }
        break;
      }
      case Action::kCandidate:
        if (is_pressed) {
          brush_->SetColor(theme.candidate_pressed);
          target_->FillRectangle(rect, brush_);
        }
        DrawText(e.label, candidate_format_, rect, e.selected ? theme.accent : theme.text);
        break;
      case Action::kPinyin:
      case Action::kSymbolCategory:
      case Action::kText:
        if (e.region == Region::kSideList || (e.region == Region::kGrid && e.action != Action::kText)) {
          // list items: flat, pressed / selected highlight
          if (is_pressed || e.selected) {
            brush_->SetColor(is_pressed ? theme.pressed : theme.key);
            target_->FillRoundedRectangle(D2D1::RoundedRect(rect, kKeyRadius, kKeyRadius), brush_);
          }
          DrawText(e.label, e.region == Region::kGrid ? candidate_format_ : side_format_, rect, theme.text);
          break;
        }
        [[fallthrough]];
      default: {
        brush_->SetColor(is_pressed ? theme.pressed : e.function_key ? theme.function_key : theme.key);
        target_->FillRoundedRectangle(D2D1::RoundedRect(rect, kKeyRadius, kKeyRadius), brush_);
        if (e.sublabel.empty()) {
          DrawText(e.label, e.function_key ? function_format_ : key_format_, rect, theme.text);
        } else {
          const float split = rect.top + (rect.bottom - rect.top) * 0.62f;
          DrawText(e.label, e.function_key ? function_format_ : key_format_,
                   D2D1::RectF(rect.left, rect.top, rect.right, split + 2), theme.text);
          DrawText(e.sublabel, sub_format_, D2D1::RectF(rect.left, split - 2, rect.right, rect.bottom - 2),
                   theme.subtext);
        }
        break;
      }
    }
    if (clipped) target_->PopAxisAlignedClip();
  }

  if (target_->EndDraw() == D2DERR_RECREATE_TARGET) ReleaseTarget();
}

}  // namespace t9ime::panel
