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

// After the iFlytek iOS keyboard (Docs/T9.jpg): slate gradient background,
// light keys, darker function keys with white labels, a shadow under each key.
Theme Theme::Light() {
  Theme t;
  t.background = Rgb(0x9A9EA8);
  t.background_bottom = Rgb(0x4E5462);
  t.key = Rgb(0xFBFBFC);
  t.key_bottom = Rgb(0xDADBDF);
  t.function_key = Rgb(0x8A8F9A);
  t.function_key_bottom = Rgb(0x5D6270);
  t.pressed = Rgb(0xB4B8C1);
  t.text = Rgb(0x111214);
  t.subtext = Rgb(0x4A4E56);
  t.function_text = Rgb(0xFFFFFF);
  t.accent = Rgb(0x1E6FD9);
  t.strip = Rgb(0xE6E8EC);
  t.candidate_pressed = Rgb(0xC9D6EA);
  t.shadow = Rgb(0x30343C);
  return t;
}

Theme Theme::Dark() {
  Theme t;
  t.background = Rgb(0x34373E);
  t.background_bottom = Rgb(0x1C1E22);
  t.key = Rgb(0x5A5E66);
  t.key_bottom = Rgb(0x464A51);
  t.function_key = Rgb(0x3A3D44);
  t.function_key_bottom = Rgb(0x2C2F34);
  t.pressed = Rgb(0x70757E);
  t.text = Rgb(0xF2F3F5);
  t.subtext = Rgb(0xB4B8C0);
  t.function_text = Rgb(0xF2F3F5);
  t.accent = Rgb(0x5AA2FF);
  t.strip = Rgb(0x25282D);
  t.candidate_pressed = Rgb(0x2B3A52);
  t.shadow = Rgb(0x0E0F11);
  return t;
}

PanelRenderer::~PanelRenderer() {
  ReleaseTarget();
  SafeRelease(key_format_);
  SafeRelease(sub_format_);
  SafeRelease(function_format_);
  SafeRelease(candidate_format_);
  SafeRelease(side_format_);
  SafeRelease(strip_format_);
  SafeRelease(preview_format_);
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

  key_format_ = CreateFormat(22, DWRITE_TEXT_ALIGNMENT_CENTER, true);
  sub_format_ = CreateFormat(12, DWRITE_TEXT_ALIGNMENT_CENTER);
  function_format_ = CreateFormat(16, DWRITE_TEXT_ALIGNMENT_CENTER);
  candidate_format_ = CreateFormat(19, DWRITE_TEXT_ALIGNMENT_CENTER);
  side_format_ = CreateFormat(16, DWRITE_TEXT_ALIGNMENT_CENTER);
  strip_format_ = CreateFormat(13, DWRITE_TEXT_ALIGNMENT_LEADING);
  preview_format_ = CreateFormat(34, DWRITE_TEXT_ALIGNMENT_CENTER, true);
  return key_format_ && sub_format_ && function_format_ && candidate_format_ && side_format_ && strip_format_ &&
         preview_format_;
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
  for (auto& g : gradients_) SafeRelease(g);
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

ID2D1LinearGradientBrush* PanelRenderer::Gradient(const D2D1_COLOR_F& top, const D2D1_COLOR_F& bottom, float y0,
                                                   float y1) {
  auto same = [](const D2D1_COLOR_F& a, const D2D1_COLOR_F& b) {
    return a.r == b.r && a.g == b.g && a.b == b.b && a.a == b.a;
  };
  int slot = -1;
  for (int i = 0; i < 3 && slot < 0; ++i) {
    if (gradients_[i] && same(gradient_colors_[i][0], top) && same(gradient_colors_[i][1], bottom)) slot = i;
  }
  if (slot < 0) {
    for (int i = 0; i < 3 && slot < 0; ++i) {
      if (!gradients_[i]) slot = i;
    }
    if (slot < 0) {  // theme changed: start over
      for (auto& g : gradients_) SafeRelease(g);
      slot = 0;
    }
    const D2D1_GRADIENT_STOP stops[2] = {{0.f, top}, {1.f, bottom}};
    ID2D1GradientStopCollection* collection = nullptr;
    if (FAILED(target_->CreateGradientStopCollection(stops, 2, &collection))) return nullptr;
    target_->CreateLinearGradientBrush(D2D1::LinearGradientBrushProperties(D2D1::Point2F(0, 0), D2D1::Point2F(0, 1)),
                                       collection, &gradients_[slot]);
    collection->Release();
    if (!gradients_[slot]) return nullptr;
    gradient_colors_[slot][0] = top;
    gradient_colors_[slot][1] = bottom;
  }
  gradients_[slot]->SetStartPoint(D2D1::Point2F(0, y0));
  gradients_[slot]->SetEndPoint(D2D1::Point2F(0, y1));
  return gradients_[slot];
}

void PanelRenderer::Render(HWND hwnd, UINT dpi, const Layout& layout, const Theme& theme,
                           const std::vector<ElementKey>& pressed) {
  if (!EnsureTarget(hwnd, dpi)) return;
  target_->BeginDraw();
  target_->SetTransform(D2D1::Matrix3x2F::Identity());
  target_->Clear(theme.background);
  const D2D1_SIZE_F size = target_->GetSize();
  if (auto* bg = Gradient(theme.background, theme.background_bottom, 0, size.height)) {
    target_->FillRectangle(D2D1::RectF(0, 0, size.width, size.height), bg);
  }

  // Handle strip, candidate bar and expanded candidates on a light surface.
  brush_->SetColor(theme.strip);
  target_->FillRectangle(ToD2D(layout.handle), brush_);
  for (const RegionRect& r : layout.regions) {
    const bool candidates = r.region == Region::kCandidateBar ||
                            (r.region == Region::kGrid &&
                             std::any_of(layout.elements.begin(), layout.elements.end(), [](const Element& e) {
                               return e.action == Action::kCandidate && e.region == Region::kGrid;
                             }));
    if (candidates) {
      target_->FillRectangle(D2D1::RectF(0, r.rect.y, size.width, r.rect.Bottom()), brush_);
    }
    if (r.region == Region::kSideList) {  // one function key holding the list
      const float gap = Metrics::kGap;
      const D2D1_RECT_F side = D2D1::RectF(r.rect.x + gap, r.rect.y + gap, r.rect.Right() - gap, r.rect.Bottom() - gap);
      brush_->SetColor(theme.shadow);
      target_->FillRoundedRectangle(
          D2D1::RoundedRect(D2D1::RectF(side.left, side.top + 1.5f, side.right, side.bottom + 1.5f), kKeyRadius,
                            kKeyRadius),
          brush_);
      if (auto* g = Gradient(theme.function_key, theme.function_key_bottom, side.top, side.bottom)) {
        target_->FillRoundedRectangle(D2D1::RoundedRect(side, kKeyRadius, kKeyRadius), g);
      }
      brush_->SetColor(theme.strip);
    }
  }

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
          const bool side = e.region == Region::kSideList;
          if (is_pressed || e.selected) {
            brush_->SetColor(is_pressed ? theme.pressed : theme.key);
            target_->FillRoundedRectangle(D2D1::RoundedRect(rect, kKeyRadius, kKeyRadius), brush_);
          }
          DrawText(e.label, side ? side_format_ : candidate_format_, rect,
                   side && !is_pressed && !e.selected ? theme.function_text : theme.text);
          break;
        }
        [[fallthrough]];
      default: {
        // Shadow, then the key face (gradient; flat when pressed or toggled on).
        brush_->SetColor(theme.shadow);
        target_->FillRoundedRectangle(
            D2D1::RoundedRect(D2D1::RectF(rect.left, rect.top + 1.5f, rect.right, rect.bottom + 1.5f), kKeyRadius,
                              kKeyRadius),
            brush_);
        const bool function = e.function_key && !(e.selected && e.action == Action::kShift);
        ID2D1Brush* face = nullptr;
        if (is_pressed) {
          brush_->SetColor(theme.pressed);
          face = brush_;
        } else {
          face = function ? Gradient(theme.function_key, theme.function_key_bottom, rect.top, rect.bottom)
                          : Gradient(theme.key, theme.key_bottom, rect.top, rect.bottom);
          if (!face) {
            brush_->SetColor(function ? theme.function_key : theme.key);
            face = brush_;
          }
        }
        target_->FillRoundedRectangle(D2D1::RoundedRect(rect, kKeyRadius, kKeyRadius), face);
        const D2D1_COLOR_F text = function && !is_pressed ? theme.function_text : theme.text;
        if (e.sublabel.empty()) {
          DrawText(e.label, e.function_key ? function_format_ : key_format_, rect, text);
        } else {  // small digit / long-press character on top, the key itself below
          const float split = rect.top + (rect.bottom - rect.top) * 0.36f;
          DrawText(e.sublabel, sub_format_, D2D1::RectF(rect.left, rect.top + 2, rect.right, split + 2),
                   function && !is_pressed ? theme.function_text : theme.subtext);
          DrawText(e.label, e.function_key ? function_format_ : key_format_,
                   D2D1::RectF(rect.left, split - 4, rect.right, rect.bottom - 2), text);
        }
        break;
      }
    }
    if (clipped) target_->PopAxisAlignedClip();
  }

  // Key preview: the pressed QWERTY key, enlarged above the finger, so the
  // user sees what is typed (passwords show only dots in the field).
  for (const Element& e : layout.elements) {
    if (e.action != Action::kLetter ||
        std::find(pressed.begin(), pressed.end(), ElementKey::Of(e)) == pressed.end()) {
      continue;
    }
    const float w = e.rect.w * 1.5f, h = e.rect.h * 1.15f;
    const float x = std::clamp(e.rect.x + e.rect.w / 2 - w / 2, 0.f, std::max(0.f, layout.handle.w - w));
    const float y = std::max(0.f, e.rect.y - h + e.rect.h * 0.1f);
    const D2D1_RECT_F bubble = D2D1::RectF(x, y, x + w, y + h);
    brush_->SetColor(theme.key);
    target_->FillRoundedRectangle(D2D1::RoundedRect(bubble, kKeyRadius, kKeyRadius), brush_);
    brush_->SetColor(theme.accent);
    target_->DrawRoundedRectangle(D2D1::RoundedRect(bubble, kKeyRadius, kKeyRadius), brush_, 1.5f);
    DrawText(e.label, preview_format_, bubble, theme.text);
  }

  if (target_->EndDraw() == D2DERR_RECREATE_TARGET) ReleaseTarget();
}

}  // namespace t9ime::panel
