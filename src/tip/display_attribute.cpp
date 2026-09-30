#include "display_attribute.h"

#include <atomic>
#include <new>

#include "globals.h"

namespace t9ime::tip {

namespace {

constexpr TF_DISPLAYATTRIBUTE kInputAttribute = {
    {TF_CT_NONE, {0}},  // text color
    {TF_CT_NONE, {0}},  // background
    TF_LS_DOT,          // underline style
    FALSE,              // bold underline
    {TF_CT_NONE, {0}},  // underline color
    TF_ATTR_INPUT,
};

class InputAttributeInfo final : public ITfDisplayAttributeInfo {
 public:
  InputAttributeInfo() { DllAddRef(); }
  ~InputAttributeInfo() { DllRelease(); }

  STDMETHODIMP QueryInterface(REFIID riid, void** ppv) override {
    if (!ppv) return E_INVALIDARG;
    if (riid == IID_IUnknown || riid == IID_ITfDisplayAttributeInfo) {
      *ppv = static_cast<ITfDisplayAttributeInfo*>(this);
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
  STDMETHODIMP GetGUID(GUID* guid) override {
    if (!guid) return E_INVALIDARG;
    *guid = kGuidDisplayAttributeInput;
    return S_OK;
  }
  STDMETHODIMP GetDescription(BSTR* desc) override {
    if (!desc) return E_INVALIDARG;
    *desc = SysAllocString(L"T9Ime composition");
    return *desc ? S_OK : E_OUTOFMEMORY;
  }
  STDMETHODIMP GetAttributeInfo(TF_DISPLAYATTRIBUTE* attr) override {
    if (!attr) return E_INVALIDARG;
    *attr = kInputAttribute;
    return S_OK;
  }
  STDMETHODIMP SetAttributeInfo(const TF_DISPLAYATTRIBUTE*) override { return E_NOTIMPL; }
  STDMETHODIMP Reset() override { return S_OK; }

 private:
  std::atomic<ULONG> refs_{1};
};

class AttributeEnum final : public IEnumTfDisplayAttributeInfo {
 public:
  explicit AttributeEnum(ULONG index = 0) : index_(index) { DllAddRef(); }
  ~AttributeEnum() { DllRelease(); }

  STDMETHODIMP QueryInterface(REFIID riid, void** ppv) override {
    if (!ppv) return E_INVALIDARG;
    if (riid == IID_IUnknown || riid == IID_IEnumTfDisplayAttributeInfo) {
      *ppv = static_cast<IEnumTfDisplayAttributeInfo*>(this);
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
  STDMETHODIMP Clone(IEnumTfDisplayAttributeInfo** out) override {
    if (!out) return E_INVALIDARG;
    *out = new (std::nothrow) AttributeEnum(index_);
    return *out ? S_OK : E_OUTOFMEMORY;
  }
  STDMETHODIMP Next(ULONG count, ITfDisplayAttributeInfo** infos, ULONG* fetched) override {
    if (!infos) return E_INVALIDARG;
    ULONG n = 0;
    while (n < count && index_ < 1) {
      infos[n] = CreateInputDisplayAttributeInfo();
      if (!infos[n]) break;
      ++n;
      ++index_;
    }
    if (fetched) *fetched = n;
    return n == count ? S_OK : S_FALSE;
  }
  STDMETHODIMP Reset() override {
    index_ = 0;
    return S_OK;
  }
  STDMETHODIMP Skip(ULONG count) override {
    const bool enough = index_ + count <= 1;
    index_ = enough ? index_ + count : 1;
    return enough ? S_OK : S_FALSE;
  }

 private:
  std::atomic<ULONG> refs_{1};
  ULONG index_;
};

}  // namespace

IEnumTfDisplayAttributeInfo* CreateDisplayAttributeEnum() { return new (std::nothrow) AttributeEnum(); }
ITfDisplayAttributeInfo* CreateInputDisplayAttributeInfo() { return new (std::nothrow) InputAttributeInfo(); }

}  // namespace t9ime::tip
