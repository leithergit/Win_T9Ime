#pragma once
// ITfEditSession around a callable. The callable must own copies of all the
// data it needs: TSF may run it later (TF_ES_ASYNCDONTCARE).

#include <msctf.h>
#include <wrl/client.h>

#include <atomic>
#include <new>
#include <utility>

#include "globals.h"

namespace t9ime::tip {

template <typename F>
class EditSession final : public ITfEditSession {
 public:
  explicit EditSession(F f) : f_(std::move(f)) { DllAddRef(); }
  ~EditSession() { DllRelease(); }

  STDMETHODIMP QueryInterface(REFIID riid, void** ppv) override {
    if (!ppv) return E_INVALIDARG;
    if (riid == IID_IUnknown || riid == IID_ITfEditSession) {
      *ppv = static_cast<ITfEditSession*>(this);
      AddRef();
      return S_OK;
    }
    *ppv = nullptr;
    return E_NOINTERFACE;
  }
  STDMETHODIMP_(ULONG) AddRef() override { return ++refs_; }
  STDMETHODIMP_(ULONG) Release() override {
    const ULONG r = --refs_;
    if (r == 0) delete this;
    return r;
  }
  STDMETHODIMP DoEditSession(TfEditCookie ec) override {
    return Guard([&] { return f_(ec); });
  }

 private:
  F f_;
  std::atomic<ULONG> refs_{1};
};

// Requests an edit session on `context`. Returns the session HRESULT (or the
// request failure).
template <typename F>
HRESULT RequestEditSession(ITfContext* context, TfClientId client, DWORD flags, F&& f) {
  if (!context) return E_INVALIDARG;
  auto* session = new (std::nothrow) EditSession<std::decay_t<F>>(std::forward<F>(f));
  if (!session) return E_OUTOFMEMORY;
  HRESULT hr_session = E_FAIL;
  const HRESULT hr = context->RequestEditSession(client, session, flags, &hr_session);
  session->Release();
  return FAILED(hr) ? hr : hr_session;
}

}  // namespace t9ime::tip
