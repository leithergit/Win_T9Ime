#include "ime_profile.h"

namespace t9ime {

const CLSID kClsidT9Tip = {0xbb2f3ba4, 0x3b7a, 0x414a, {0xa9, 0x8f, 0x08, 0xc1, 0x00, 0xe5, 0x44, 0x7e}};
const GUID kGuidT9Profile = {0x3ea4ff8c, 0xcaf7, 0x456f, {0x95, 0x42, 0x1c, 0x0f, 0x1e, 0xf2, 0x63, 0x5e}};

namespace {

ITfInputProcessorProfileMgr* ProfileMgr() {
  ITfInputProcessorProfileMgr* mgr = nullptr;
  if (FAILED(CoCreateInstance(CLSID_TF_InputProcessorProfiles, nullptr, CLSCTX_INPROC_SERVER,
                              IID_ITfInputProcessorProfileMgr, reinterpret_cast<void**>(&mgr)))) {
    return nullptr;
  }
  return mgr;
}

}  // namespace

TF_INPUTPROCESSORPROFILE T9Profile() {
  TF_INPUTPROCESSORPROFILE p = {};
  p.dwProfileType = TF_PROFILETYPE_INPUTPROCESSOR;
  p.langid = kT9LangId;
  p.clsid = kClsidT9Tip;
  p.guidProfile = kGuidT9Profile;
  return p;
}

bool FindOtherProfile(TF_INPUTPROCESSORPROFILE* out) {
  ITfInputProcessorProfileMgr* mgr = ProfileMgr();
  if (!mgr) return false;
  bool found = false, found_layout = false;
  IEnumTfInputProcessorProfiles* profiles = nullptr;
  if (SUCCEEDED(mgr->EnumProfiles(0, &profiles)) && profiles) {
    TF_INPUTPROCESSORPROFILE p;
    ULONG n = 0;
    while (!found_layout && profiles->Next(1, &p, &n) == S_OK && n == 1) {
      if (!(p.dwFlags & TF_IPP_FLAG_ENABLED)) continue;
      if (p.dwProfileType == TF_PROFILETYPE_INPUTPROCESSOR && IsEqualCLSID(p.clsid, kClsidT9Tip)) continue;
      const bool layout = p.dwProfileType == TF_PROFILETYPE_KEYBOARDLAYOUT && p.langid != kT9LangId;
      if (!found || layout) {
        *out = p;
        found = true;
        found_layout = layout;
      }
    }
    profiles->Release();
  }
  mgr->Release();
  return found;
}

bool ActivateProfile(const TF_INPUTPROCESSORPROFILE& p, DWORD flags) {
  ITfInputProcessorProfileMgr* mgr = ProfileMgr();
  if (!mgr) return false;
  const HRESULT hr = mgr->ActivateProfile(p.dwProfileType, p.langid, p.clsid, p.guidProfile, p.hkl,
                                          flags | TF_IPPMF_DONTCARECURRENTINPUTLANGUAGE);
  mgr->Release();
  return SUCCEEDED(hr);
}

}  // namespace t9ime
