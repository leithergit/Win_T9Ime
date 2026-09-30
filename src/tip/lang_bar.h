#pragma once
// Language bar / taskbar input-mode button showing 中 or 英. Clicking it
// toggles the mode. Uses GUID_LBI_INPUTMODE so Windows 8+ shows it in the
// taskbar (TIPCAP_SYSTRAYSUPPORT). Adapted from Weasel WeaselTSF/LanguageBar.

#include <msctf.h>

#include <atomic>
#include <functional>

namespace t9ime::tip {

class LangBarButton final : public ITfLangBarItemButton, public ITfSource {
 public:
  explicit LangBarButton(std::function<void()> on_click);
  ~LangBarButton();

  void SetAsciiMode(bool ascii);
  void Detach() { on_click_ = nullptr; }

  // IUnknown
  STDMETHODIMP QueryInterface(REFIID riid, void** ppv) override;
  STDMETHODIMP_(ULONG) AddRef() override { return ++refs_; }
  STDMETHODIMP_(ULONG) Release() override;
  // ITfLangBarItem
  STDMETHODIMP GetInfo(TF_LANGBARITEMINFO* info) override;
  STDMETHODIMP GetStatus(DWORD* status) override;
  STDMETHODIMP Show(BOOL show) override;
  STDMETHODIMP GetTooltipString(BSTR* tip) override;
  // ITfLangBarItemButton
  STDMETHODIMP OnClick(TfLBIClick click, POINT pt, const RECT* area) override;
  STDMETHODIMP InitMenu(ITfMenu* menu) override;
  STDMETHODIMP OnMenuSelect(UINT id) override;
  STDMETHODIMP GetIcon(HICON* icon) override;
  STDMETHODIMP GetText(BSTR* text) override;
  // ITfSource
  STDMETHODIMP AdviseSink(REFIID riid, IUnknown* sink, DWORD* cookie) override;
  STDMETHODIMP UnadviseSink(DWORD cookie) override;

 private:
  std::atomic<ULONG> refs_{1};
  std::function<void()> on_click_;
  ITfLangBarItemSink* sink_ = nullptr;
  bool ascii_ = false;
};

}  // namespace t9ime::tip
