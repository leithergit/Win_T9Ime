#include "lang_bar.h"

#include <olectl.h>

#include <vector>

#include "globals.h"

namespace t9ime::tip {

namespace {

constexpr DWORD kSinkCookie = 1;

// Draws a single character as an icon (white text with alpha, readable on the
// dark taskbar; the language bar draws it on its own background).
HICON MakeTextIcon(const wchar_t* text) {
  const int size = GetSystemMetrics(SM_CXSMICON);
  BITMAPINFO bi = {};
  bi.bmiHeader.biSize = sizeof(bi.bmiHeader);
  bi.bmiHeader.biWidth = size;
  bi.bmiHeader.biHeight = -size;
  bi.bmiHeader.biPlanes = 1;
  bi.bmiHeader.biBitCount = 32;
  void* bits = nullptr;
  HDC dc = CreateCompatibleDC(nullptr);
  HBITMAP color = CreateDIBSection(dc, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
  if (!color) {
    DeleteDC(dc);
    return nullptr;
  }
  HGDIOBJ old_bmp = SelectObject(dc, color);
  HFONT font = CreateFontW(-size + 1, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                           CLIP_DEFAULT_PRECIS, ANTIALIASED_QUALITY, DEFAULT_PITCH, L"Microsoft YaHei");
  HGDIOBJ old_font = SelectObject(dc, font);
  SetBkMode(dc, TRANSPARENT);
  SetTextColor(dc, RGB(255, 255, 255));
  RECT rc = {0, 0, size, size};
  DrawTextW(dc, text, -1, &rc, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
  GdiFlush();
  // GDI leaves alpha at 0: derive it from the rendered intensity.
  auto* px = static_cast<DWORD*>(bits);
  for (int i = 0; i < size * size; ++i) {
    const DWORD c = px[i] & 0xFF;
    px[i] = c ? (c << 24) | 0x00FFFFFF : 0;
  }
  SelectObject(dc, old_font);
  SelectObject(dc, old_bmp);
  DeleteObject(font);
  DeleteDC(dc);
  // Monochrome rows are WORD aligned. All-zero mask: the alpha channel decides.
  HBITMAP mask = CreateBitmap(size, size, 1, 1, std::vector<BYTE>(static_cast<size_t>((size + 15) / 16 * 2 * size), 0).data());
  ICONINFO ii = {TRUE, 0, 0, mask, color};
  HICON icon = CreateIconIndirect(&ii);
  DeleteObject(mask);
  DeleteObject(color);
  return icon;
}

}  // namespace

LangBarButton::LangBarButton(std::function<void()> on_click) : on_click_(std::move(on_click)) { DllAddRef(); }

LangBarButton::~LangBarButton() {
  if (sink_) sink_->Release();
  DllRelease();
}

STDMETHODIMP LangBarButton::QueryInterface(REFIID riid, void** ppv) {
  if (!ppv) return E_INVALIDARG;
  *ppv = nullptr;
  if (riid == IID_IUnknown || riid == IID_ITfLangBarItem || riid == IID_ITfLangBarItemButton) {
    *ppv = static_cast<ITfLangBarItemButton*>(this);
  } else if (riid == IID_ITfSource) {
    *ppv = static_cast<ITfSource*>(this);
  }
  if (!*ppv) return E_NOINTERFACE;
  AddRef();
  return S_OK;
}

STDMETHODIMP_(ULONG) LangBarButton::Release() {
  const ULONG r = --refs_;
  if (r == 0) delete this;
  return r;
}

void LangBarButton::SetAsciiMode(bool ascii) {
  if (ascii == ascii_) return;
  ascii_ = ascii;
  if (sink_) sink_->OnUpdate(TF_LBI_ICON | TF_LBI_TEXT | TF_LBI_TOOLTIP);
}

STDMETHODIMP LangBarButton::GetInfo(TF_LANGBARITEMINFO* info) {
  if (!info) return E_INVALIDARG;
  info->clsidService = kClsidTextService;
  info->guidItem = kGuidLbiInputMode;
  info->dwStyle = TF_LBI_STYLE_BTN_BUTTON | TF_LBI_STYLE_SHOWNINTRAY;
  info->ulSort = 0;
  lstrcpynW(info->szDescription, kDescription, ARRAYSIZE(info->szDescription));
  return S_OK;
}

STDMETHODIMP LangBarButton::GetStatus(DWORD* status) {
  if (!status) return E_INVALIDARG;
  *status = 0;
  return S_OK;
}

STDMETHODIMP LangBarButton::Show(BOOL) { return E_NOTIMPL; }

STDMETHODIMP LangBarButton::GetTooltipString(BSTR* tip) {
  if (!tip) return E_INVALIDARG;
  *tip = SysAllocString(ascii_ ? L"英文 (Shift 切换)" : L"中文 (Shift 切换)");
  return *tip ? S_OK : E_OUTOFMEMORY;
}

STDMETHODIMP LangBarButton::OnClick(TfLBIClick click, POINT, const RECT*) {
  return Guard([&] {
    if (click == TF_LBI_CLK_LEFT && on_click_) on_click_();
    return S_OK;
  });
}

STDMETHODIMP LangBarButton::InitMenu(ITfMenu*) { return S_OK; }
STDMETHODIMP LangBarButton::OnMenuSelect(UINT) { return S_OK; }

STDMETHODIMP LangBarButton::GetIcon(HICON* icon) {
  if (!icon) return E_INVALIDARG;
  *icon = MakeTextIcon(ascii_ ? L"英" : L"中");
  return *icon ? S_OK : E_FAIL;
}

STDMETHODIMP LangBarButton::GetText(BSTR* text) {
  if (!text) return E_INVALIDARG;
  *text = SysAllocString(ascii_ ? L"英" : L"中");
  return *text ? S_OK : E_OUTOFMEMORY;
}

STDMETHODIMP LangBarButton::AdviseSink(REFIID riid, IUnknown* sink, DWORD* cookie) {
  if (!sink || !cookie) return E_INVALIDARG;
  if (riid != IID_ITfLangBarItemSink) return CONNECT_E_CANNOTCONNECT;
  if (sink_) return CONNECT_E_ADVISELIMIT;
  if (FAILED(sink->QueryInterface(IID_ITfLangBarItemSink, reinterpret_cast<void**>(&sink_)))) {
    sink_ = nullptr;
    return E_NOINTERFACE;
  }
  *cookie = kSinkCookie;
  return S_OK;
}

STDMETHODIMP LangBarButton::UnadviseSink(DWORD cookie) {
  if (cookie != kSinkCookie || !sink_) return CONNECT_E_NOCONNECTION;
  sink_->Release();
  sink_ = nullptr;
  return S_OK;
}

}  // namespace t9ime::tip
