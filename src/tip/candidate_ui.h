#pragma once
// ITfCandidateListUIElementBehavior: lets applications that draw their own
// candidate UI (games, full-screen apps; BeginUIElement returns show=FALSE)
// read and drive the candidate list. Adapted from Weasel WeaselTSF/CandidateList.

#include <msctf.h>

#include <atomic>
#include <functional>
#include <string>
#include <vector>

namespace t9ime::tip {

class CandidateUI final : public ITfCandidateListUIElementBehavior {
 public:
  struct Callbacks {
    std::function<void(UINT index)> select;  // select + finalize
    std::function<void()> abort;
  };

  CandidateUI();
  ~CandidateUI();

  void SetCallbacks(Callbacks cb) { callbacks_ = std::move(cb); }
  void SetDocumentMgr(ITfDocumentMgr* doc);
  void SetCandidates(std::vector<std::wstring> texts, UINT selection);
  void SetShown(BOOL shown) { shown_ = shown; }
  DWORD TakeUpdatedFlags();

  // IUnknown
  STDMETHODIMP QueryInterface(REFIID riid, void** ppv) override;
  STDMETHODIMP_(ULONG) AddRef() override { return ++refs_; }
  STDMETHODIMP_(ULONG) Release() override;
  // ITfUIElement
  STDMETHODIMP GetDescription(BSTR* desc) override;
  STDMETHODIMP GetGUID(GUID* guid) override;
  STDMETHODIMP Show(BOOL show) override;
  STDMETHODIMP IsShown(BOOL* shown) override;
  // ITfCandidateListUIElement
  STDMETHODIMP GetUpdatedFlags(DWORD* flags) override;
  STDMETHODIMP GetDocumentMgr(ITfDocumentMgr** doc) override;
  STDMETHODIMP GetCount(UINT* count) override;
  STDMETHODIMP GetSelection(UINT* index) override;
  STDMETHODIMP GetString(UINT index, BSTR* str) override;
  STDMETHODIMP GetPageIndex(UINT* index, UINT size, UINT* page_count) override;
  STDMETHODIMP SetPageIndex(UINT* index, UINT page_count) override;
  STDMETHODIMP GetCurrentPage(UINT* page) override;
  // ITfCandidateListUIElementBehavior
  STDMETHODIMP SetSelection(UINT index) override;
  STDMETHODIMP Finalize() override;
  STDMETHODIMP Abort() override;

 private:
  std::atomic<ULONG> refs_{1};
  Callbacks callbacks_;
  ITfDocumentMgr* doc_ = nullptr;
  std::vector<std::wstring> texts_;
  UINT selection_ = 0;
  BOOL shown_ = TRUE;
  DWORD updated_ = 0;
};

}  // namespace t9ime::tip
