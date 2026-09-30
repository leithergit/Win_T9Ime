#include "candidate_ui.h"

#include "globals.h"

namespace t9ime::tip {

CandidateUI::CandidateUI() { DllAddRef(); }

CandidateUI::~CandidateUI() {
  if (doc_) doc_->Release();
  DllRelease();
}

STDMETHODIMP CandidateUI::QueryInterface(REFIID riid, void** ppv) {
  if (!ppv) return E_INVALIDARG;
  if (riid == IID_IUnknown || riid == IID_ITfUIElement || riid == IID_ITfCandidateListUIElement ||
      riid == IID_ITfCandidateListUIElementBehavior) {
    *ppv = static_cast<ITfCandidateListUIElementBehavior*>(this);
    AddRef();
    return S_OK;
  }
  *ppv = nullptr;
  return E_NOINTERFACE;
}

STDMETHODIMP_(ULONG) CandidateUI::Release() {
  const ULONG r = --refs_;
  if (r == 0) delete this;
  return r;
}

void CandidateUI::SetDocumentMgr(ITfDocumentMgr* doc) {
  if (doc) doc->AddRef();
  if (doc_) doc_->Release();
  doc_ = doc;
  updated_ |= TF_CLUIE_DOCUMENTMGR;
}

void CandidateUI::SetCandidates(std::vector<std::wstring> texts, UINT selection) {
  if (texts != texts_) updated_ |= TF_CLUIE_STRING | TF_CLUIE_COUNT | TF_CLUIE_PAGEINDEX | TF_CLUIE_CURRENTPAGE;
  if (selection != selection_) updated_ |= TF_CLUIE_SELECTION;
  texts_ = std::move(texts);
  selection_ = selection;
}

DWORD CandidateUI::TakeUpdatedFlags() {
  const DWORD f = updated_;
  updated_ = 0;
  return f;
}

STDMETHODIMP CandidateUI::GetDescription(BSTR* desc) {
  if (!desc) return E_INVALIDARG;
  *desc = SysAllocString(L"Candidate List");
  return *desc ? S_OK : E_OUTOFMEMORY;
}

STDMETHODIMP CandidateUI::GetGUID(GUID* guid) {
  if (!guid) return E_INVALIDARG;
  *guid = kGuidCandidateUI;
  return S_OK;
}

STDMETHODIMP CandidateUI::Show(BOOL show) {
  shown_ = show;
  return S_OK;
}

STDMETHODIMP CandidateUI::IsShown(BOOL* shown) {
  if (!shown) return E_INVALIDARG;
  *shown = shown_;
  return S_OK;
}

STDMETHODIMP CandidateUI::GetUpdatedFlags(DWORD* flags) {
  if (!flags) return E_INVALIDARG;
  *flags = updated_;
  return S_OK;
}

STDMETHODIMP CandidateUI::GetDocumentMgr(ITfDocumentMgr** doc) {
  if (!doc) return E_INVALIDARG;
  *doc = doc_;
  if (doc_) doc_->AddRef();
  return doc_ ? S_OK : E_FAIL;
}

STDMETHODIMP CandidateUI::GetCount(UINT* count) {
  if (!count) return E_INVALIDARG;
  *count = static_cast<UINT>(texts_.size());
  return S_OK;
}

STDMETHODIMP CandidateUI::GetSelection(UINT* index) {
  if (!index) return E_INVALIDARG;
  *index = selection_;
  return S_OK;
}

STDMETHODIMP CandidateUI::GetString(UINT index, BSTR* str) {
  if (!str) return E_INVALIDARG;
  if (index >= texts_.size()) return E_INVALIDARG;
  *str = SysAllocStringLen(texts_[index].c_str(), static_cast<UINT>(texts_[index].size()));
  return *str ? S_OK : E_OUTOFMEMORY;
}

// One page: the engine pages; the UI element exposes the current page only.
STDMETHODIMP CandidateUI::GetPageIndex(UINT* index, UINT size, UINT* page_count) {
  if (!page_count) return E_INVALIDARG;
  *page_count = 1;
  if (index && size >= 1) index[0] = 0;
  return S_OK;
}

STDMETHODIMP CandidateUI::SetPageIndex(UINT*, UINT) { return E_NOTIMPL; }

STDMETHODIMP CandidateUI::GetCurrentPage(UINT* page) {
  if (!page) return E_INVALIDARG;
  *page = 0;
  return S_OK;
}

STDMETHODIMP CandidateUI::SetSelection(UINT index) {
  if (index >= texts_.size()) return E_INVALIDARG;
  selection_ = index;
  return S_OK;
}

STDMETHODIMP CandidateUI::Finalize() {
  return Guard([&] {
    if (callbacks_.select) callbacks_.select(selection_);
    return S_OK;
  });
}

STDMETHODIMP CandidateUI::Abort() {
  return Guard([&] {
    if (callbacks_.abort) callbacks_.abort();
    return S_OK;
  });
}

}  // namespace t9ime::tip
