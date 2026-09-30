#pragma once
// The T9Ime text input processor (one instance per UI thread that uses TSF).
// Thin client: key events go to T9Host, results are applied as TSF
// compositions. Structure follows Weasel's WeaselTSF (GPL-3.0), with these
// differences: no boost, request timeouts, per-instance key state, Deactivate
// ends the composition and unadvises every sink, compartments sync both ways.

#include <msctf.h>
#include <wrl/client.h>

#include <atomic>
#include <optional>

#include "candidate_ui.h"
#include "candidate_window.h"
#include "host_client.h"
#include "key_event.h"
#include "lang_bar.h"
#include "protocol.h"

namespace t9ime::tip {

class TextService final : public ITfTextInputProcessorEx,
                          public ITfThreadMgrEventSink,
                          public ITfTextLayoutSink,
                          public ITfKeyEventSink,
                          public ITfCompositionSink,
                          public ITfThreadFocusSink,
                          public ITfActiveLanguageProfileNotifySink,
                          public ITfCompartmentEventSink,
                          public ITfDisplayAttributeProvider {
 public:
  TextService();

  // IUnknown
  STDMETHODIMP QueryInterface(REFIID riid, void** ppv) override;
  STDMETHODIMP_(ULONG) AddRef() override { return ++refs_; }
  STDMETHODIMP_(ULONG) Release() override;

  // ITfTextInputProcessor(Ex)
  STDMETHODIMP Activate(ITfThreadMgr* mgr, TfClientId id) override { return ActivateEx(mgr, id, 0); }
  STDMETHODIMP ActivateEx(ITfThreadMgr* mgr, TfClientId id, DWORD flags) override;
  STDMETHODIMP Deactivate() override;

  // ITfThreadMgrEventSink
  STDMETHODIMP OnInitDocumentMgr(ITfDocumentMgr*) override { return S_OK; }
  STDMETHODIMP OnUninitDocumentMgr(ITfDocumentMgr*) override { return S_OK; }
  STDMETHODIMP OnSetFocus(ITfDocumentMgr* focus, ITfDocumentMgr* previous) override;
  STDMETHODIMP OnPushContext(ITfContext*) override { return S_OK; }
  STDMETHODIMP OnPopContext(ITfContext*) override { return S_OK; }

  // ITfTextLayoutSink
  STDMETHODIMP OnLayoutChange(ITfContext* context, TfLayoutCode code, ITfContextView* view) override;

  // ITfKeyEventSink
  STDMETHODIMP OnSetFocus(BOOL foreground) override;
  STDMETHODIMP OnTestKeyDown(ITfContext* c, WPARAM w, LPARAM l, BOOL* eaten) override;
  STDMETHODIMP OnKeyDown(ITfContext* c, WPARAM w, LPARAM l, BOOL* eaten) override;
  STDMETHODIMP OnTestKeyUp(ITfContext* c, WPARAM w, LPARAM l, BOOL* eaten) override;
  STDMETHODIMP OnKeyUp(ITfContext* c, WPARAM w, LPARAM l, BOOL* eaten) override;
  STDMETHODIMP OnPreservedKey(ITfContext*, REFGUID, BOOL* eaten) override;

  // ITfCompositionSink
  STDMETHODIMP OnCompositionTerminated(TfEditCookie ec, ITfComposition* composition) override;

  // ITfThreadFocusSink
  STDMETHODIMP OnSetThreadFocus() override;
  STDMETHODIMP OnKillThreadFocus() override;

  // ITfActiveLanguageProfileNotifySink
  STDMETHODIMP OnActivated(REFCLSID clsid, REFGUID profile, BOOL activated) override;

  // ITfCompartmentEventSink
  STDMETHODIMP OnChange(REFGUID compartment) override;

  // ITfDisplayAttributeProvider
  STDMETHODIMP EnumDisplayAttributeInfo(IEnumTfDisplayAttributeInfo** out) override;
  STDMETHODIMP GetDisplayAttributeInfo(REFGUID guid, ITfDisplayAttributeInfo** out) override;

 private:
  ~TextService();

  // Keys
  HRESULT HandleKey(ITfContext* context, WPARAM vk, LPARAM lp, bool up, bool test, BOOL* eaten);
  bool KeyboardUsable(ITfContext* context);

  // Results from the host -> composition, candidates, mode indicator.
  // `outside_key_event`: called from a window message (candidate click, later
  // panel pushes) rather than a key sink. Then a synchronous edit session is
  // tried first: asynchronous ones may be deferred until the next key in
  // CUAS (IMM32) documents.
  void Apply(ITfContext* context, const ipc::Result& result, bool outside_key_event = false);
  HRESULT ApplyInSession(TfEditCookie ec, ITfContext* context, const ipc::Result& result);
  bool StartComposition(TfEditCookie ec, ITfContext* context);
  void EndComposition(TfEditCookie ec, ITfContext* context, bool clear);
  void SetDisplayAttribute(TfEditCookie ec, ITfContext* context, ITfRange* range);
  void UpdateCandidates(TfEditCookie ec, ITfContext* context, const ipc::Result& result);
  bool CompositionRect(TfEditCookie ec, ITfContext* context, RECT* rect);
  void HideCandidates();
  void Abort();  // drop the composition here and in the host
  void SelectCandidate(int index);
  void ChangePage(bool backward);

  // Sinks
  bool AdviseSinks();
  void UnadviseSinks();
  void AdviseLayoutSink(ITfDocumentMgr* doc);
  void UnadviseLayoutSink();

  // Compartments (GUID_COMPARTMENT_KEYBOARD_OPENCLOSE / _INPUTMODE_CONVERSION)
  bool GetCompartment(REFGUID guid, DWORD* value, ITfContext* context = nullptr);
  void SetCompartment(REFGUID guid, DWORD value);
  void SyncAsciiMode(bool ascii);
  void SetAsciiModeInHost(bool ascii);

  std::atomic<ULONG> refs_{1};
  Microsoft::WRL::ComPtr<ITfThreadMgr> thread_mgr_;
  TfClientId client_id_ = TF_CLIENTID_NULL;
  DWORD activate_flags_ = 0;
  DWORD thread_mgr_cookie_ = TF_INVALID_COOKIE;
  DWORD thread_focus_cookie_ = TF_INVALID_COOKIE;
  DWORD profile_cookie_ = TF_INVALID_COOKIE;
  DWORD open_close_cookie_ = TF_INVALID_COOKIE;
  DWORD conversion_cookie_ = TF_INVALID_COOKIE;
  DWORD layout_cookie_ = TF_INVALID_COOKIE;
  Microsoft::WRL::ComPtr<ITfContext> layout_context_;
  Microsoft::WRL::ComPtr<ITfContext> active_context_;  // context of the last key
  Microsoft::WRL::ComPtr<ITfComposition> composition_;
  TfGuidAtom display_atom_ = TF_INVALID_GUIDATOM;

  HostClient host_;
  struct Pending {
    bool valid = false;
    WPARAM vk = 0;
    bool up = false;
    BOOL eaten = FALSE;
  } pending_;
  ipc::Result state_;  // last state from the host (commit cleared)
  bool ascii_mode_ = false;
  bool setting_compartment_ = false;

  CandidateWindow window_;
  Microsoft::WRL::ComPtr<CandidateUI> ui_;
  DWORD ui_id_ = TF_INVALID_UIELEMENTID;
  BOOL ui_show_ = TRUE;
  RECT last_rect_{};
  bool has_rect_ = false;

  Microsoft::WRL::ComPtr<LangBarButton> lang_bar_;
};

}  // namespace t9ime::tip
