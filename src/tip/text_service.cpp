#include "text_service.h"

#include <InputScope.h>
#include <olectl.h>

#include <algorithm>

#include "display_attribute.h"
#include "edit_session.h"
#include "globals.h"

using Microsoft::WRL::ComPtr;

namespace t9ime::tip {

namespace {

constexpr DWORD kCmodeNative = 0x0001;  // IME_CMODE_NATIVE
constexpr UINT kPushMessage = WM_APP + 1;
constexpr UINT kTouchTapMessage = WM_APP + 2;  // press on the focused window
// Candidate window clicks / wheel: handled after the mouse message returns.
// Edit sessions requested while the application is still dispatching the
// mouse message may be deferred (CUAS on Windows 7: until the next key).
constexpr UINT kCandidateSelectMessage = WM_APP + 3;  // wParam = index
constexpr UINT kCandidatePageMessage = WM_APP + 4;    // wParam = backward
constexpr UINT kReconnectedMessage = WM_APP + 5;      // new host connection: report the focus again
constexpr wchar_t kMessageClass[] = L"T9Ime.TipEvents";

// {1713DD5A-68E7-4A5B-9AF6-592A595C778D} (InputScope.h; not in uuid.lib)
const GUID kGuidPropInputScope = {0x1713dd5a, 0x68e7, 0x4a5b, {0x9a, 0xf6, 0x59, 0x2a, 0x59, 0x5c, 0x77, 0x8d}};
const IID kIidTfInputScope = {0xfde1eaee, 0x6924, 0x4cdf, {0x91, 0xe7, 0xda, 0x38, 0xcf, 0xf5, 0x55, 0x9d}};

// Fields where Chinese composition makes no sense: keys go straight to the app.
constexpr uint32_t kPassThroughScopes[] = {
    IS_PASSWORD, IS_PRIVATE, IS_NUMERIC_PASSWORD, IS_NUMERIC_PIN, IS_ALPHANUMERIC_PIN,
    IS_URL, IS_EMAIL_USERNAME, IS_EMAIL_SMTPEMAILADDRESS, IS_EMAILNAME_OR_ADDRESS, IS_LOGINNAME,
    IS_DIGITS, IS_NUMBER, IS_NUMBER_FULLWIDTH, IS_TELEPHONE_FULLTELEPHONENUMBER,
    IS_TELEPHONE_COUNTRYCODE, IS_TELEPHONE_AREACODE, IS_TELEPHONE_LOCALNUMBER,
};

// InputScope of the selection in `context` (read lock required).
std::vector<uint32_t> ReadInputScopes(TfEditCookie ec, ITfContext* context) {
  std::vector<uint32_t> out;
  // InputScope is an application property (CUAS exposes SetInputScope() of
  // IMM32 windows there); some text stores also offer it as a regular property.
  ComPtr<ITfReadOnlyProperty> prop;
  if (FAILED(context->GetAppProperty(kGuidPropInputScope, &prop)) || !prop) {
    ComPtr<ITfProperty> regular;
    if (FAILED(context->GetProperty(kGuidPropInputScope, &regular)) || !regular) return out;
    prop = regular;
  }
  TF_SELECTION sel = {};
  ULONG fetched = 0;
  if (FAILED(context->GetSelection(ec, TF_DEFAULT_SELECTION, 1, &sel, &fetched)) || !fetched) return out;
  ComPtr<ITfRange> range;
  range.Attach(sel.range);
  VARIANT v;
  VariantInit(&v);
  if (prop->GetValue(ec, range.Get(), &v) == S_OK && v.vt == VT_UNKNOWN && v.punkVal) {
    ComPtr<ITfInputScope> scope;
    if (SUCCEEDED(v.punkVal->QueryInterface(kIidTfInputScope, reinterpret_cast<void**>(scope.GetAddressOf())))) {
      InputScope* scopes = nullptr;
      UINT count = 0;
      if (SUCCEEDED(scope->GetInputScopes(&scopes, &count)) && scopes) {
        for (UINT i = 0; i < count; ++i) out.push_back(static_cast<uint32_t>(scopes[i]));
        CoTaskMemFree(scopes);
      }
    }
  }
  VariantClear(&v);
  return out;
}

template <typename T>
ComPtr<T> Query(IUnknown* p) {
  ComPtr<T> out;
  if (p) p->QueryInterface(IID_PPV_ARGS(&out));
  return out;
}

bool AdviseOn(IUnknown* source_owner, REFIID riid, IUnknown* sink, DWORD* cookie) {
  auto source = Query<ITfSource>(source_owner);
  return source && SUCCEEDED(source->AdviseSink(riid, sink, cookie));
}

void UnadviseOn(IUnknown* source_owner, DWORD* cookie) {
  if (*cookie == TF_INVALID_COOKIE) return;
  if (auto source = Query<ITfSource>(source_owner)) source->UnadviseSink(*cookie);
  *cookie = TF_INVALID_COOKIE;
}

ComPtr<ITfCompartment> CompartmentOf(IUnknown* owner, REFGUID guid) {
  ComPtr<ITfCompartment> c;
  if (auto mgr = Query<ITfCompartmentMgr>(owner)) mgr->GetCompartment(guid, &c);
  return c;
}

}  // namespace

TextService::TextService() { DllAddRef(); }

TextService::~TextService() { DllRelease(); }

STDMETHODIMP_(ULONG) TextService::Release() {
  const ULONG r = --refs_;
  if (r == 0) delete this;
  return r;
}

STDMETHODIMP TextService::QueryInterface(REFIID riid, void** ppv) {
  if (!ppv) return E_INVALIDARG;
  *ppv = nullptr;
  if (riid == IID_IUnknown || riid == IID_ITfTextInputProcessor || riid == IID_ITfTextInputProcessorEx) {
    *ppv = static_cast<ITfTextInputProcessorEx*>(this);
  } else if (riid == IID_ITfThreadMgrEventSink) {
    *ppv = static_cast<ITfThreadMgrEventSink*>(this);
  } else if (riid == IID_ITfTextLayoutSink) {
    *ppv = static_cast<ITfTextLayoutSink*>(this);
  } else if (riid == IID_ITfKeyEventSink) {
    *ppv = static_cast<ITfKeyEventSink*>(this);
  } else if (riid == IID_ITfCompositionSink) {
    *ppv = static_cast<ITfCompositionSink*>(this);
  } else if (riid == IID_ITfThreadFocusSink) {
    *ppv = static_cast<ITfThreadFocusSink*>(this);
  } else if (riid == IID_ITfActiveLanguageProfileNotifySink) {
    *ppv = static_cast<ITfActiveLanguageProfileNotifySink*>(this);
  } else if (riid == IID_ITfCompartmentEventSink) {
    *ppv = static_cast<ITfCompartmentEventSink*>(this);
  } else if (riid == IID_ITfDisplayAttributeProvider) {
    *ppv = static_cast<ITfDisplayAttributeProvider*>(this);
  }
  if (!*ppv) return E_NOINTERFACE;
  AddRef();
  return S_OK;
}

// ---------------------------------------------------------------------------
// Activation

STDMETHODIMP TextService::ActivateEx(ITfThreadMgr* mgr, TfClientId id, DWORD flags) {
  return Guard([&]() -> HRESULT {
    thread_mgr_ = mgr;
    client_id_ = id;
    activate_flags_ = flags;

    ui_.Attach(new CandidateUI());
    ui_->SetCallbacks({[this](UINT index) { SelectCandidate(static_cast<int>(index)); }, [this] { Abort(); }});
    window_.SetCallbacks(
        [this](int index) { PostMessageW(message_window_, kCandidateSelectMessage, static_cast<WPARAM>(index), 0); },
        [this](bool backward) { PostMessageW(message_window_, kCandidatePageMessage, backward ? 1 : 0, 0); });
    lang_bar_.Attach(new LangBarButton([this] { SetAsciiModeInHost(!ascii_mode_); }));

    WNDCLASSEXW wc = {sizeof(wc)};
    if (!GetClassInfoExW(g_module, kMessageClass, &wc)) {
      wc = {sizeof(wc)};
      wc.lpfnWndProc = MessageWndProc;
      wc.hInstance = g_module;
      wc.lpszClassName = kMessageClass;
      RegisterClassExW(&wc);
    }
    message_window_ = CreateWindowExW(0, kMessageClass, L"", 0, 0, 0, 0, 0, HWND_MESSAGE, nullptr, g_module, nullptr);
    if (message_window_) SetWindowLongPtrW(message_window_, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(this));
    touch_.Install(message_window_, kTouchTapMessage);
    host_.SetOnConnected([this] {
      EnsureEvents();
      // A new host (or a dropped connection) knows nothing about our focus.
      // Posted: this runs inside a host call.
      if (host_.generation() > 1) PostMessageW(message_window_, kReconnectedMessage, 0, 0);
    });

    if (!AdviseSinks()) {
      Deactivate();
      return E_FAIL;
    }
    if (auto cat = [] {
          ComPtr<ITfCategoryMgr> c;
          CoCreateInstance(CLSID_TF_CategoryMgr, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&c));
          return c;
        }()) {
      cat->RegisterGUID(kGuidDisplayAttributeInput, &display_atom_);
    }
    ComPtr<ITfDocumentMgr> focus;
    if (SUCCEEDED(thread_mgr_->GetFocus(&focus)) && focus) {
      AdviseLayoutSink(focus.Get());
      ReportFocus(focus.Get());
    }

    DWORD open = 0;
    if (!GetCompartment(GUID_COMPARTMENT_KEYBOARD_OPENCLOSE, &open) || !open) {
      SetCompartment(GUID_COMPARTMENT_KEYBOARD_OPENCLOSE, 1);
    }
    if (auto r = host_.Call(ipc::Writer(ipc::MsgType::kQueryState))) SyncAsciiMode(r->ascii_mode);
    return S_OK;
  });
}

STDMETHODIMP TextService::Deactivate() {
  return Guard([&]() -> HRESULT {
    Abort();
    host_.Notify(ipc::Writer(ipc::MsgType::kFocusOut), HostClient::kKeyTimeoutMs);
    events_.Stop();
    touch_.Uninstall();
    if (message_window_) {
      SetWindowLongPtrW(message_window_, GWLP_USERDATA, 0);
      DestroyWindow(message_window_);
      message_window_ = nullptr;
    }
    window_.Destroy();
    UnadviseSinks();
    UnadviseLayoutSink();
    if (lang_bar_) {
      if (auto items = Query<ITfLangBarItemMgr>(thread_mgr_.Get())) items->RemoveItem(lang_bar_.Get());
      lang_bar_->Detach();
      lang_bar_.Reset();
    }
    if (ui_) {
      ui_->SetCallbacks({});
      ui_->SetDocumentMgr(nullptr);
      ui_.Reset();
    }
    composition_.Reset();
    active_context_.Reset();
    host_.SetOnConnected(nullptr);
    host_.Disconnect();
    thread_mgr_.Reset();
    client_id_ = TF_CLIENTID_NULL;
    return S_OK;
  });
}

bool TextService::AdviseSinks() {
  IUnknown* tm = thread_mgr_.Get();
  if (!AdviseOn(tm, IID_ITfThreadMgrEventSink, static_cast<ITfThreadMgrEventSink*>(this), &thread_mgr_cookie_)) {
    return false;
  }
  AdviseOn(tm, IID_ITfThreadFocusSink, static_cast<ITfThreadFocusSink*>(this), &thread_focus_cookie_);
  AdviseOn(tm, IID_ITfActiveLanguageProfileNotifySink, static_cast<ITfActiveLanguageProfileNotifySink*>(this),
           &profile_cookie_);
  auto keystrokes = Query<ITfKeystrokeMgr>(tm);
  if (!keystrokes || FAILED(keystrokes->AdviseKeyEventSink(client_id_, static_cast<ITfKeyEventSink*>(this), TRUE))) {
    return false;
  }
  if (auto c = CompartmentOf(tm, GUID_COMPARTMENT_KEYBOARD_OPENCLOSE)) {
    AdviseOn(c.Get(), IID_ITfCompartmentEventSink, static_cast<ITfCompartmentEventSink*>(this), &open_close_cookie_);
  }
  if (auto c = CompartmentOf(tm, GUID_COMPARTMENT_KEYBOARD_INPUTMODE_CONVERSION)) {
    AdviseOn(c.Get(), IID_ITfCompartmentEventSink, static_cast<ITfCompartmentEventSink*>(this), &conversion_cookie_);
  }
  if (auto items = Query<ITfLangBarItemMgr>(tm)) items->AddItem(lang_bar_.Get());
  return true;
}

void TextService::UnadviseSinks() {
  IUnknown* tm = thread_mgr_.Get();
  if (!tm) return;
  if (auto keystrokes = Query<ITfKeystrokeMgr>(tm)) keystrokes->UnadviseKeyEventSink(client_id_);
  if (auto c = CompartmentOf(tm, GUID_COMPARTMENT_KEYBOARD_OPENCLOSE)) UnadviseOn(c.Get(), &open_close_cookie_);
  if (auto c = CompartmentOf(tm, GUID_COMPARTMENT_KEYBOARD_INPUTMODE_CONVERSION)) UnadviseOn(c.Get(), &conversion_cookie_);
  UnadviseOn(tm, &profile_cookie_);
  UnadviseOn(tm, &thread_focus_cookie_);
  UnadviseOn(tm, &thread_mgr_cookie_);
}

void TextService::AdviseLayoutSink(ITfDocumentMgr* doc) {
  UnadviseLayoutSink();
  ComPtr<ITfContext> top;
  if (!doc || FAILED(doc->GetTop(&top)) || !top) return;
  if (AdviseOn(top.Get(), IID_ITfTextLayoutSink, static_cast<ITfTextLayoutSink*>(this), &layout_cookie_)) {
    layout_context_ = top;
  }
}

void TextService::UnadviseLayoutSink() {
  if (layout_context_) UnadviseOn(layout_context_.Get(), &layout_cookie_);
  layout_context_.Reset();
}

// ---------------------------------------------------------------------------
// Focus

STDMETHODIMP TextService::OnSetFocus(ITfDocumentMgr* focus, ITfDocumentMgr* previous) {
  return Guard([&] {
    if (focus != previous) Abort();
    AdviseLayoutSink(focus);
    ReportFocus(focus);
    return S_OK;
  });
}

STDMETHODIMP TextService::OnSetFocus(BOOL foreground) {
  return Guard([&] {
    if (!foreground) Abort();
    return S_OK;
  });
}

STDMETHODIMP TextService::OnSetThreadFocus() {
  return Guard([&] {
    if (auto r = host_.Call(ipc::Writer(ipc::MsgType::kQueryState))) SyncAsciiMode(r->ascii_mode);
    ComPtr<ITfDocumentMgr> focus;
    if (thread_mgr_ && SUCCEEDED(thread_mgr_->GetFocus(&focus))) ReportFocus(focus.Get());
    return S_OK;
  });
}

STDMETHODIMP TextService::OnKillThreadFocus() {
  return Guard([&] {
    Abort();
    ReportFocus(nullptr);
    return S_OK;
  });
}

STDMETHODIMP TextService::OnActivated(REFCLSID clsid, REFGUID, BOOL activated) {
  return Guard([&] {
    if (clsid == kClsidTextService && !activated) Abort();
    return S_OK;
  });
}

// ---------------------------------------------------------------------------
// Keys
//
// Some applications call OnTestKeyDown several times for one key, others only
// OnKeyDown. As in Weasel, the test call already sends the key and remembers
// the result; the matching OnKeyDown returns it without sending again. The
// cache is per instance (per thread), not process-wide as in Weasel.

STDMETHODIMP TextService::OnTestKeyDown(ITfContext* c, WPARAM w, LPARAM l, BOOL* eaten) {
  return Guard([&] { return HandleKey(c, w, l, false, true, eaten); });
}
STDMETHODIMP TextService::OnKeyDown(ITfContext* c, WPARAM w, LPARAM l, BOOL* eaten) {
  return Guard([&] { return HandleKey(c, w, l, false, false, eaten); });
}
STDMETHODIMP TextService::OnTestKeyUp(ITfContext* c, WPARAM w, LPARAM l, BOOL* eaten) {
  return Guard([&] { return HandleKey(c, w, l, true, true, eaten); });
}
STDMETHODIMP TextService::OnKeyUp(ITfContext* c, WPARAM w, LPARAM l, BOOL* eaten) {
  return Guard([&] { return HandleKey(c, w, l, true, false, eaten); });
}

STDMETHODIMP TextService::OnPreservedKey(ITfContext*, REFGUID, BOOL* eaten) {
  if (eaten) *eaten = FALSE;
  return S_OK;
}

bool TextService::KeyboardUsable(ITfContext* context) {
  DWORD v = 0;
  if (GetCompartment(GUID_COMPARTMENT_KEYBOARD_OPENCLOSE, &v) && !v) return false;
  if (context) {
    if (GetCompartment(GUID_COMPARTMENT_KEYBOARD_DISABLED, &v, context) && v) return false;
    if (GetCompartment(GUID_COMPARTMENT_EMPTYCONTEXT, &v, context) && v) return false;
  }
  return true;
}

HRESULT TextService::HandleKey(ITfContext* context, WPARAM vk, LPARAM lp, bool up, bool test, BOOL* eaten) {
  if (!eaten) return E_INVALIDARG;
  *eaten = FALSE;
  if (pending_.valid && pending_.vk == vk && pending_.up == up) {
    *eaten = pending_.eaten;
    if (!test) pending_.valid = false;  // the real event consumes the cached test result
    return S_OK;
  }
  pending_.valid = false;
  if (!KeyboardUsable(context) || PassThroughScope()) return S_OK;

  BYTE state[256];
  if (!GetKeyboardState(state)) return S_OK;
  KeyEvent ke;
  if (!ConvertKey(vk, lp, state, up, &ke)) return S_OK;
  // Windows-key combinations and the Windows keys belong to the shell.
  if ((state[VK_LWIN] & 0x80) || (state[VK_RWIN] & 0x80)) return S_OK;
  // Ctrl / Alt shortcuts pass through unless a composition is active.
  if ((ke.mask & (mask::kControl | mask::kAlt)) && !IsModifier(ke.keycode) && !state_.composing) return S_OK;

  ipc::Writer req(ipc::MsgType::kKey);
  req.U32(ipc::kTagKeycode, ke.keycode).U32(ipc::kTagMask, ke.mask).Bool(ipc::kTagTest, test);
  auto result = host_.Call(std::move(req));
  EnsureEvents();
  if (!result) return S_OK;  // host unavailable: the key goes to the application
  *eaten = result->eaten;
  if (test) pending_ = {true, vk, up, *eaten};
  Apply(context, *result);
  return S_OK;
}

// ---------------------------------------------------------------------------
// Applying results

void TextService::Apply(ITfContext* context, const ipc::Result& result, bool outside_key_event) {
  if (context) active_context_ = context;
  state_ = result;
  state_.commit.clear();
  if (result.ascii_mode != ascii_mode_) SyncAsciiMode(result.ascii_mode);
  if (result.commit.empty() && !result.composing && !composition_) {
    HideCandidates();
    return;
  }
  if (!active_context_) return;
  ComPtr<TextService> self(this);
  ComPtr<ITfContext> ctx = active_context_;
  auto session = [self, ctx, result](TfEditCookie ec) { return self->ApplyInSession(ec, ctx.Get(), result); };
  if (outside_key_event) {
    const HRESULT hr = RequestEditSession(ctx.Get(), client_id_, TF_ES_SYNC | TF_ES_READWRITE, session);
    if (hr != TF_E_SYNCHRONOUS && hr != TF_E_LOCKED) return;  // done (or failed for good)
  }
  RequestEditSession(ctx.Get(), client_id_, TF_ES_ASYNCDONTCARE | TF_ES_READWRITE, session);
}

HRESULT TextService::ApplyInSession(TfEditCookie ec, ITfContext* context, const ipc::Result& r) {
  if (!r.commit.empty()) {
    if (!composition_) StartComposition(ec, context);
    ComPtr<ITfRange> range;
    if (composition_ && SUCCEEDED(composition_->GetRange(&range))) {
      range->SetText(ec, 0, r.commit.c_str(), static_cast<LONG>(r.commit.size()));
      EndComposition(ec, context, false);
    } else if (auto insert = Query<ITfInsertAtSelection>(context)) {
      // No composition possible (e.g. transitory context): plain insertion.
      insert->InsertTextAtSelection(ec, 0, r.commit.c_str(), static_cast<LONG>(r.commit.size()), &range);
    }
    if (range) {
      range->Collapse(ec, TF_ANCHOR_END);
      TF_SELECTION sel = {range.Get(), {TF_AE_NONE, FALSE}};
      context->SetSelection(ec, 1, &sel);
    }
  }

  if (r.composing) {
    if (!r.preedit.empty()) {
      if (!composition_) StartComposition(ec, context);
      ComPtr<ITfRange> range;
      if (composition_ && SUCCEEDED(composition_->GetRange(&range))) {
        range->SetText(ec, 0, r.preedit.c_str(), static_cast<LONG>(r.preedit.size()));
        SetDisplayAttribute(ec, context, range.Get());
        ComPtr<ITfRange> caret;
        range->Clone(&caret);
        if (r.cursor >= 0 && r.cursor <= static_cast<int>(r.preedit.size())) {
          caret->Collapse(ec, TF_ANCHOR_START);
          LONG moved = 0;
          caret->ShiftStart(ec, r.cursor, &moved, nullptr);
          caret->Collapse(ec, TF_ANCHOR_START);
        } else {
          caret->Collapse(ec, TF_ANCHOR_END);
        }
        TF_SELECTION sel = {caret.Get(), {TF_AE_NONE, FALSE}};
        context->SetSelection(ec, 1, &sel);
      }
    }
    UpdateCandidates(ec, context, r);
  } else {
    if (composition_) EndComposition(ec, context, true);
    HideCandidates();
  }
  return S_OK;
}

bool TextService::StartComposition(TfEditCookie ec, ITfContext* context) {
  auto insert = Query<ITfInsertAtSelection>(context);
  auto compose = Query<ITfContextComposition>(context);
  ComPtr<ITfRange> range;
  if (!insert || !compose || FAILED(insert->InsertTextAtSelection(ec, TF_IAS_QUERYONLY, nullptr, 0, &range))) {
    return false;
  }
  ComPtr<ITfComposition> composition;
  if (FAILED(compose->StartComposition(ec, range.Get(), static_cast<ITfCompositionSink*>(this), &composition)) ||
      !composition) {
    return false;
  }
  composition_ = composition;
  return true;
}

void TextService::EndComposition(TfEditCookie ec, ITfContext* context, bool clear) {
  // Drop ownership first: some hosts call OnCompositionTerminated synchronously
  // from EndComposition, which must not look like an external abort.
  ComPtr<ITfComposition> composition = std::move(composition_);
  if (!composition) return;
  ComPtr<ITfRange> range;
  if (SUCCEEDED(composition->GetRange(&range))) {
    if (auto prop = [&] {
          ComPtr<ITfProperty> p;
          context->GetProperty(GUID_PROP_ATTRIBUTE, &p);
          return p;
        }()) {
      prop->Clear(ec, range.Get());
    }
    if (clear) range->SetText(ec, 0, L"", 0);
  }
  composition->EndComposition(ec);
}

void TextService::SetDisplayAttribute(TfEditCookie ec, ITfContext* context, ITfRange* range) {
  if (display_atom_ == TF_INVALID_GUIDATOM) return;
  ComPtr<ITfProperty> prop;
  if (FAILED(context->GetProperty(GUID_PROP_ATTRIBUTE, &prop))) return;
  VARIANT v;
  VariantInit(&v);
  v.vt = VT_I4;
  v.lVal = static_cast<LONG>(display_atom_);
  prop->SetValue(ec, range, &v);
}

STDMETHODIMP TextService::OnCompositionTerminated(TfEditCookie, ITfComposition* composition) {
  return Guard([&] {
    if (!composition_ || composition_.Get() != composition) return S_OK;
    // The application ended our composition (click elsewhere, etc.).
    composition_.Reset();
    if (state_.composing) {
      host_.Call(ipc::Writer(ipc::MsgType::kClearComposition), HostClient::kOtherTimeoutMs);
      state_.composing = false;
    }
    HideCandidates();
    return S_OK;
  });
}

void TextService::Abort() {
  if (state_.composing || composition_) {
    host_.Call(ipc::Writer(ipc::MsgType::kClearComposition), HostClient::kKeyTimeoutMs);
    state_.composing = false;
  }
  if (composition_ && active_context_) {
    ComPtr<TextService> self(this);
    ComPtr<ITfContext> ctx = active_context_;
    RequestEditSession(ctx.Get(), client_id_, TF_ES_ASYNCDONTCARE | TF_ES_READWRITE, [self, ctx](TfEditCookie ec) {
      self->EndComposition(ec, ctx.Get(), true);
      return S_OK;
    });
  }
  HideCandidates();
}

// ---------------------------------------------------------------------------
// Host pushes and focus reports

ITfContext* TextService::FocusedContext(ComPtr<ITfContext>* holder) {
  ComPtr<ITfDocumentMgr> doc;
  if (!thread_mgr_ || FAILED(thread_mgr_->GetFocus(&doc)) || !doc) return nullptr;
  if (FAILED(doc->GetTop(holder->ReleaseAndGetAddressOf()))) return nullptr;
  return holder->Get();
}

// Password edit controls: the system disables IMM/TSF in them, so the focus
// arrives here without a document. They still need the (letters) panel.
static bool IsPasswordEdit(HWND hwnd) {
  if (!hwnd) return false;
  wchar_t cls[64] = {};
  GetClassNameW(hwnd, cls, 64);
  if (lstrcmpiW(cls, L"Edit") != 0 && !wcsstr(cls, L"RichEdit") && !wcsstr(cls, L"RICHEDIT")) return false;
  return (GetWindowLongW(hwnd, GWL_STYLE) & ES_PASSWORD) != 0;
}

// Tells the host which field of this thread has the focus (nullptr: none), so
// panel output can be pushed here. Also (re)attaches the events pipe.
void TextService::ReportFocus(ITfDocumentMgr* doc) {
  ComPtr<ITfContext> top;
  if (!doc || FAILED(doc->GetTop(&top)) || !top) {
    focus_scopes_.clear();
    HWND focus = GetFocus();
    if (IsPasswordEdit(focus)) {
      SendFocusIn(focus, {IS_PASSWORD}, touch_.FocusFromTouch(), false, touch_.Describe(), true);
      return;
    }
    host_.Notify(ipc::Writer(ipc::MsgType::kFocusOut), HostClient::kKeyTimeoutMs);
    EnsureEvents();
    return;
  }
  // Computed now: it describes the input message being processed.
  const bool touch = touch_.FocusFromTouch();
  const std::wstring touch_debug = touch_.Describe();
  HWND hwnd = nullptr;
  ComPtr<ITfContextView> view;
  if (SUCCEEDED(top->GetActiveView(&view)) && view) view->GetWnd(&hwnd);
  TF_STATUS status = {};
  const bool read_only = SUCCEEDED(top->GetStatus(&status)) && (status.dwDynamicFlags & TS_SD_READONLY);

  // InputScope needs a read lock. Try a synchronous session; if the document
  // is busy, report now without it and again once the async session runs.
  std::vector<uint32_t> scopes;
  bool have_scopes = false;
  RequestEditSession(top.Get(), client_id_, TF_ES_SYNC | TF_ES_READ, [&](TfEditCookie ec) {
    scopes = ReadInputScopes(ec, top.Get());
    have_scopes = true;
    return S_OK;
  });
  focus_scopes_ = scopes;
  SendFocusIn(hwnd, scopes, touch, read_only, touch_debug);
  if (!have_scopes) {
    ComPtr<TextService> self(this);
    RequestEditSession(top.Get(), client_id_, TF_ES_ASYNC | TF_ES_READ, [self, top, hwnd, touch, read_only](TfEditCookie ec) {
      auto late = ReadInputScopes(ec, top.Get());
      if (late != self->focus_scopes_) {
        self->focus_scopes_ = late;
        self->SendFocusIn(hwnd, late, touch, read_only);
      }
      return S_OK;
    });
  }
}

void TextService::SendFocusIn(HWND hwnd, const std::vector<uint32_t>& scopes, bool touch, bool read_only,
                              const std::wstring& touch_debug, bool no_context) {
  ipc::Writer req(ipc::MsgType::kFocusIn);
  req.U32(ipc::kTagTid, GetCurrentThreadId());
  req.U32(ipc::kTagHwnd, static_cast<uint32_t>(reinterpret_cast<uintptr_t>(hwnd)));
  for (uint32_t s : scopes) req.U32(ipc::kTagInputScope, s);
  req.Bool(ipc::kTagTouch, touch).Bool(ipc::kTagReadOnly, read_only);
  if (!touch_debug.empty()) req.Str(ipc::kTagText, touch_debug);
  if (no_context) req.U32(ipc::kTagFlags, ipc::kFocusNoContext);
  host_.Notify(std::move(req), HostClient::kKeyTimeoutMs);
  EnsureEvents();
}

bool TextService::PassThroughScope() const {
  for (uint32_t s : focus_scopes_) {
    if (std::find(std::begin(kPassThroughScopes), std::end(kPassThroughScopes), s) != std::end(kPassThroughScopes)) {
      return true;
    }
  }
  return false;
}

void TextService::EnsureEvents() {
  if (!message_window_ || host_.generation() == events_generation_ || !host_.client_id()) return;
  events_generation_ = host_.generation();
  events_.Start(host_.client_id(), message_window_, kPushMessage);
}

LRESULT CALLBACK TextService::MessageWndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
  if (msg == kReconnectedMessage) {
    if (auto* self = reinterpret_cast<TextService*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA))) {
      Guard([&] {
        ComPtr<ITfDocumentMgr> focus;
        if (self->thread_mgr_ && SUCCEEDED(self->thread_mgr_->GetFocus(&focus)) && focus) self->ReportFocus(focus.Get());
        return S_OK;
      });
    }
    return 0;
  }
  if (msg == kCandidateSelectMessage || msg == kCandidatePageMessage) {
    if (auto* self = reinterpret_cast<TextService*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA))) {
      Guard([&] {
        if (msg == kCandidateSelectMessage) self->SelectCandidate(static_cast<int>(wp));
        else self->ChangePage(wp != 0);
        return S_OK;
      });
    }
    return 0;
  }
  if (msg == kPushMessage || msg == kTouchTapMessage) {
    if (auto* self = reinterpret_cast<TextService*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA))) {
      Guard([&] {
        if (msg == kPushMessage) {
          self->OnPushes();
        } else if (self->thread_mgr_) {
          // Pressed the field that already had the focus: report it again
          // (touch or not, from the press just recorded) so the panel can pop up.
          ComPtr<ITfDocumentMgr> focus;
          if (SUCCEEDED(self->thread_mgr_->GetFocus(&focus)) && focus) self->ReportFocus(focus.Get());
        }
        return S_OK;
      });
    }
    return 0;
  }
  return DefWindowProcW(hwnd, msg, wp, lp);
}

// Touch panel output: commit into the focused field. A physical-keyboard
// composition in progress is replaced (and dropped in the host).
void TextService::OnPushes() {
  for (std::wstring& text : events_.Take()) {
    ComPtr<ITfContext> holder;
    ITfContext* context = FocusedContext(&holder);
    if (!context || text.empty()) continue;
    if (state_.composing || composition_) {
      host_.Call(ipc::Writer(ipc::MsgType::kClearComposition), HostClient::kKeyTimeoutMs);
    }
    ipc::Result r;
    r.commit = std::move(text);
    r.ascii_mode = ascii_mode_;
    Apply(context, r, true);
  }
}

// ---------------------------------------------------------------------------
// Candidates

bool TextService::CompositionRect(TfEditCookie ec, ITfContext* context, RECT* rect) {
  ComPtr<ITfContextView> view;
  if (FAILED(context->GetActiveView(&view)) || !view) return false;
  ComPtr<ITfRange> range;
  if (composition_) {
    ComPtr<ITfRange> comp;
    if (SUCCEEDED(composition_->GetRange(&comp))) comp->Clone(&range);
  }
  if (range) {
    range->Collapse(ec, TF_ANCHOR_START);
  } else {
    TF_SELECTION sel = {};
    ULONG fetched = 0;
    if (FAILED(context->GetSelection(ec, TF_DEFAULT_SELECTION, 1, &sel, &fetched)) || !fetched) return false;
    range.Attach(sel.range);
  }
  BOOL clipped = FALSE;
  // TS_E_NOLAYOUT (layout not ready yet) -> keep the cached position.
  if (view->GetTextExt(ec, range.Get(), rect, &clipped) != S_OK) return false;
  return rect->left || rect->top || rect->right || rect->bottom;
}

void TextService::UpdateCandidates(TfEditCookie ec, ITfContext* context, const ipc::Result& r) {
  if (r.candidates.empty()) {
    HideCandidates();
    return;
  }
  RECT rc;
  if (CompositionRect(ec, context, &rc)) {
    last_rect_ = rc;
    has_rect_ = true;
  } else if (!has_rect_) {
    GUITHREADINFO gti = {sizeof(gti)};
    if (GetGUIThreadInfo(GetCurrentThreadId(), &gti) && gti.hwndCaret) {
      last_rect_ = gti.rcCaret;
      MapWindowPoints(gti.hwndCaret, nullptr, reinterpret_cast<POINT*>(&last_rect_), 2);
      has_rect_ = true;
    }
  }

  // UIElement: applications drawing their own candidate list say show=FALSE.
  if (auto mgr = Query<ITfUIElementMgr>(thread_mgr_.Get())) {
    ComPtr<ITfDocumentMgr> doc;
    context->GetDocumentMgr(&doc);
    ui_->SetDocumentMgr(doc.Get());
    ui_->SetCandidates(r.candidates, static_cast<UINT>(r.highlighted));
    if (ui_id_ == TF_INVALID_UIELEMENTID) {
      ui_show_ = TRUE;
      if (FAILED(mgr->BeginUIElement(ui_.Get(), &ui_show_, &ui_id_))) {
        ui_id_ = TF_INVALID_UIELEMENTID;
        ui_show_ = TRUE;
      }
      ui_->SetShown(ui_show_);
    } else {
      mgr->UpdateUIElement(ui_id_);
    }
  }
  if (!ui_show_) {
    window_.Hide();
    return;
  }
  HWND owner = nullptr;
  ComPtr<ITfContextView> view;
  if (SUCCEEDED(context->GetActiveView(&view)) && view) view->GetWnd(&owner);
  CandidateView cv;
  cv.texts = r.candidates;
  cv.comments = r.comments;
  cv.highlighted = r.highlighted;
  cv.has_prev = r.page_no > 0;
  cv.has_next = !r.last_page;
  window_.Show(cv, last_rect_, owner);
}

void TextService::HideCandidates() {
  window_.Hide();
  if (ui_id_ != TF_INVALID_UIELEMENTID) {
    if (auto mgr = Query<ITfUIElementMgr>(thread_mgr_.Get())) mgr->EndUIElement(ui_id_);
    ui_id_ = TF_INVALID_UIELEMENTID;
  }
}

void TextService::SelectCandidate(int index) {
  ipc::Writer req(ipc::MsgType::kSelectCandidate);
  req.U32(ipc::kTagIndex, static_cast<uint32_t>(index));
  if (auto r = host_.Call(std::move(req), HostClient::kOtherTimeoutMs)) Apply(active_context_.Get(), *r, true);
}

void TextService::ChangePage(bool backward) {
  ipc::Writer req(ipc::MsgType::kChangePage);
  req.Bool(ipc::kTagBackward, backward);
  if (auto r = host_.Call(std::move(req), HostClient::kOtherTimeoutMs)) Apply(active_context_.Get(), *r, true);
}

STDMETHODIMP TextService::OnLayoutChange(ITfContext* context, TfLayoutCode code, ITfContextView*) {
  return Guard([&] {
    if (code != TF_LC_CHANGE || !window_.visible() || context != active_context_.Get()) return S_OK;
    ComPtr<TextService> self(this);
    ComPtr<ITfContext> ctx(context);
    // Layout callbacks may not request synchronous sessions.
    RequestEditSession(ctx.Get(), client_id_, TF_ES_ASYNCDONTCARE | TF_ES_READ, [self, ctx](TfEditCookie ec) {
      RECT rc;
      if (self->CompositionRect(ec, ctx.Get(), &rc)) {
        self->last_rect_ = rc;
        self->window_.Move(rc);
      }
      return S_OK;
    });
    return S_OK;
  });
}

// ---------------------------------------------------------------------------
// Compartments and input mode

bool TextService::GetCompartment(REFGUID guid, DWORD* value, ITfContext* context) {
  auto c = CompartmentOf(context ? static_cast<IUnknown*>(context) : thread_mgr_.Get(), guid);
  if (!c) return false;
  VARIANT v;
  VariantInit(&v);
  if (c->GetValue(&v) != S_OK || v.vt != VT_I4) {  // S_FALSE: never set
    VariantClear(&v);
    return false;
  }
  *value = static_cast<DWORD>(v.lVal);
  return true;
}

void TextService::SetCompartment(REFGUID guid, DWORD value) {
  auto c = CompartmentOf(thread_mgr_.Get(), guid);
  if (!c) return;
  VARIANT v;
  VariantInit(&v);
  v.vt = VT_I4;
  v.lVal = static_cast<LONG>(value);
  setting_compartment_ = true;
  c->SetValue(client_id_, &v);
  setting_compartment_ = false;
}

// Host state -> indicator and the IMM32-visible conversion mode.
void TextService::SyncAsciiMode(bool ascii) {
  ascii_mode_ = ascii;
  if (lang_bar_) lang_bar_->SetAsciiMode(ascii);
  DWORD conversion = 0;
  GetCompartment(GUID_COMPARTMENT_KEYBOARD_INPUTMODE_CONVERSION, &conversion);
  const DWORD wanted = ascii ? (conversion & ~kCmodeNative) : (conversion | kCmodeNative);
  if (wanted != conversion) SetCompartment(GUID_COMPARTMENT_KEYBOARD_INPUTMODE_CONVERSION, wanted);
}

void TextService::SetAsciiModeInHost(bool ascii) {
  if (state_.composing) Abort();
  ipc::Writer req(ipc::MsgType::kSetAsciiMode);
  req.Bool(ipc::kTagValue, ascii);
  if (auto r = host_.Call(std::move(req), HostClient::kOtherTimeoutMs)) SyncAsciiMode(r->ascii_mode);
}

// Application -> us (ImmSetOpenStatus / ImmSetConversionStatus via CUAS, or
// other TSF clients).
STDMETHODIMP TextService::OnChange(REFGUID guid) {
  return Guard([&] {
    if (setting_compartment_) return S_OK;
    DWORD v = 0;
    if (guid == GUID_COMPARTMENT_KEYBOARD_OPENCLOSE) {
      if (GetCompartment(guid, &v) && !v) Abort();
    } else if (guid == GUID_COMPARTMENT_KEYBOARD_INPUTMODE_CONVERSION) {
      if (GetCompartment(guid, &v)) {
        const bool ascii = !(v & kCmodeNative);
        if (ascii != ascii_mode_) SetAsciiModeInHost(ascii);
      }
    }
    return S_OK;
  });
}

// ---------------------------------------------------------------------------
// Display attributes

STDMETHODIMP TextService::EnumDisplayAttributeInfo(IEnumTfDisplayAttributeInfo** out) {
  if (!out) return E_INVALIDARG;
  *out = CreateDisplayAttributeEnum();
  return *out ? S_OK : E_OUTOFMEMORY;
}

STDMETHODIMP TextService::GetDisplayAttributeInfo(REFGUID guid, ITfDisplayAttributeInfo** out) {
  if (!out) return E_INVALIDARG;
  *out = nullptr;
  if (guid != kGuidDisplayAttributeInput) return E_INVALIDARG;
  *out = CreateInputDisplayAttributeInfo();
  return *out ? S_OK : E_OUTOFMEMORY;
}

}  // namespace t9ime::tip
