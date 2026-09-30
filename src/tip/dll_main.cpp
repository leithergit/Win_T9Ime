// DLL entry points and the class factory. Unlike Weasel, DllMain does nothing
// but record the module handle (no crash handler, no work under the loader lock).

#include <windows.h>
#include <olectl.h>

#include <new>

#include "globals.h"
#include "register.h"
#include "text_service.h"

using namespace t9ime::tip;

namespace {

class ClassFactory final : public IClassFactory {
 public:
  STDMETHODIMP QueryInterface(REFIID riid, void** ppv) override {
    if (!ppv) return E_INVALIDARG;
    if (riid == IID_IUnknown || riid == IID_IClassFactory) {
      *ppv = static_cast<IClassFactory*>(this);
      AddRef();
      return S_OK;
    }
    *ppv = nullptr;
    return E_NOINTERFACE;
  }
  // Static object: the reference count only keeps the DLL loaded.
  STDMETHODIMP_(ULONG) AddRef() override {
    DllAddRef();
    return 2;
  }
  STDMETHODIMP_(ULONG) Release() override {
    DllRelease();
    return 1;
  }
  STDMETHODIMP CreateInstance(IUnknown* outer, REFIID riid, void** ppv) override {
    return Guard([&]() -> HRESULT {
      if (!ppv) return E_INVALIDARG;
      *ppv = nullptr;
      if (outer) return CLASS_E_NOAGGREGATION;
      auto* service = new (std::nothrow) TextService();
      if (!service) return E_OUTOFMEMORY;
      const HRESULT hr = service->QueryInterface(riid, ppv);
      service->Release();
      return hr;
    });
  }
  STDMETHODIMP LockServer(BOOL lock) override {
    lock ? DllAddRef() : DllRelease();
    return S_OK;
  }
};

ClassFactory g_factory;

}  // namespace

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID) {
  if (reason == DLL_PROCESS_ATTACH) {
    g_module = instance;
    DisableThreadLibraryCalls(instance);
  }
  return TRUE;
}

STDAPI DllGetClassObject(REFCLSID clsid, REFIID riid, void** ppv) {
  if (!ppv) return E_INVALIDARG;
  *ppv = nullptr;
  if (clsid != kClsidTextService) return CLASS_E_CLASSNOTAVAILABLE;
  return g_factory.QueryInterface(riid, ppv);
}

STDAPI DllCanUnloadNow() { return g_dll_refs.load() <= 0 ? S_OK : S_FALSE; }

STDAPI DllRegisterServer() {
  return Guard([] { return RegisterAll(); });
}

STDAPI DllUnregisterServer() {
  return Guard([] { return UnregisterAll(); });
}
