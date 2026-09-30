// COM server, TSF profile and category registration. Idempotent: registering
// twice or unregistering something absent both succeed.
// Adapted from Weasel WeaselTSF/Register.cpp; only the zh-CN profile and the
// categories required by SPEC §4 are registered.

#include "register.h"

#include <msctf.h>
#include <olectl.h>
#include <wrl/client.h>

#include <string>

#include "globals.h"

using Microsoft::WRL::ComPtr;

namespace t9ime::tip {

namespace {

const GUID* const kCategories[] = {
    &GUID_TFCAT_TIP_KEYBOARD,
    &GUID_TFCAT_TIPCAP_UIELEMENTENABLED,
    &GUID_TFCAT_TIPCAP_INPUTMODECOMPARTMENT,
    &GUID_TFCAT_DISPLAYATTRIBUTEPROVIDER,
    &kGuidTfcatTipcapImmersiveSupport,
    &kGuidTfcatTipcapSystraySupport,
};

std::wstring GuidString(REFGUID guid) {
  wchar_t buf[40];
  StringFromGUID2(guid, buf, ARRAYSIZE(buf));
  return buf;
}

std::wstring ModulePath() {
  wchar_t path[MAX_PATH];
  const DWORD n = GetModuleFileNameW(g_module, path, MAX_PATH);
  return std::wstring(path, n);
}

bool SetString(HKEY key, const wchar_t* name, const std::wstring& value) {
  return RegSetValueExW(key, name, 0, REG_SZ, reinterpret_cast<const BYTE*>(value.c_str()),
                        static_cast<DWORD>((value.size() + 1) * sizeof(wchar_t))) == ERROR_SUCCESS;
}

// HKCR\CLSID\{...} is the per-machine view for the calling bitness (the WOW64
// view for the x86 DLL), which is what the x86 and x64 builds each need.
std::wstring ClsidKey() { return L"CLSID\\" + GuidString(kClsidTextService); }

bool RegisterServer() {
  HKEY key = nullptr;
  if (RegCreateKeyExW(HKEY_CLASSES_ROOT, ClsidKey().c_str(), 0, nullptr, 0, KEY_WRITE, nullptr, &key, nullptr) !=
      ERROR_SUCCESS) {
    return false;
  }
  bool ok = SetString(key, nullptr, kDescription);
  HKEY inproc = nullptr;
  if (RegCreateKeyExW(key, L"InprocServer32", 0, nullptr, 0, KEY_WRITE, nullptr, &inproc, nullptr) == ERROR_SUCCESS) {
    ok = SetString(inproc, nullptr, ModulePath()) && ok;
    ok = SetString(inproc, L"ThreadingModel", L"Apartment") && ok;
    RegCloseKey(inproc);
  } else {
    ok = false;
  }
  RegCloseKey(key);
  return ok;
}

void UnregisterServer() { RegDeleteTreeW(HKEY_CLASSES_ROOT, ClsidKey().c_str()); }

bool RegisterProfile() {
  ComPtr<ITfInputProcessorProfileMgr> mgr;
  if (FAILED(CoCreateInstance(CLSID_TF_InputProcessorProfiles, nullptr, CLSCTX_INPROC_SERVER,
                              IID_PPV_ARGS(&mgr)))) {
    return false;
  }
  const std::wstring icon = ModulePath();
  return SUCCEEDED(mgr->RegisterProfile(kClsidTextService, kLangId, kGuidProfile, kDescription,
                                        static_cast<ULONG>(wcslen(kDescription)), icon.c_str(),
                                        static_cast<ULONG>(icon.size()), 0, nullptr, 0, TRUE, 0));
}

void UnregisterProfile() {
  ComPtr<ITfInputProcessorProfileMgr> mgr;
  if (SUCCEEDED(CoCreateInstance(CLSID_TF_InputProcessorProfiles, nullptr, CLSCTX_INPROC_SERVER,
                                 IID_PPV_ARGS(&mgr)))) {
    mgr->UnregisterProfile(kClsidTextService, kLangId, kGuidProfile, 0);
  }
}

bool RegisterCategories() {
  ComPtr<ITfCategoryMgr> mgr;
  if (FAILED(CoCreateInstance(CLSID_TF_CategoryMgr, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&mgr)))) {
    return false;
  }
  for (const GUID* cat : kCategories) {
    if (FAILED(mgr->RegisterCategory(kClsidTextService, *cat, kClsidTextService))) return false;
  }
  return true;
}

void UnregisterCategories() {
  ComPtr<ITfCategoryMgr> mgr;
  if (FAILED(CoCreateInstance(CLSID_TF_CategoryMgr, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&mgr)))) return;
  for (const GUID* cat : kCategories) mgr->UnregisterCategory(kClsidTextService, *cat, kClsidTextService);
}

}  // namespace

HRESULT RegisterAll() {
  // Start from a clean state so repeated registration converges.
  UnregisterAll();
  if (!RegisterServer() || !RegisterProfile() || !RegisterCategories()) {
    UnregisterAll();
    return SELFREG_E_CLASS;
  }
  return S_OK;
}

HRESULT UnregisterAll() {
  UnregisterCategories();
  UnregisterProfile();
  UnregisterServer();
  return S_OK;
}

}  // namespace t9ime::tip
