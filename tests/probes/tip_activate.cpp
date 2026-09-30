// Probe / test helper: switch the input method of the whole desktop session
// (TF_IPPMF_FORSESSION) to T9Ime, and restore the previous one.
//   tip_activate save <file>      write the active keyboard profile to <file>
//   tip_activate on               activate T9Ime for the session
//   tip_activate restore <file>   re-activate the saved profile
// Exit code 0 on success.
#include <windows.h>
#include <msctf.h>

#include <cstdio>

namespace {

const CLSID kClsidT9Tip = {0xbb2f3ba4, 0x3b7a, 0x414a, {0xa9, 0x8f, 0x08, 0xc1, 0x00, 0xe5, 0x44, 0x7e}};
const GUID kGuidT9Profile = {0x3ea4ff8c, 0xcaf7, 0x456f, {0x95, 0x42, 0x1c, 0x0f, 0x1e, 0xf2, 0x63, 0x5e}};

ITfInputProcessorProfileMgr* Mgr() {
  ITfInputProcessorProfileMgr* mgr = nullptr;
  CoCreateInstance(CLSID_TF_InputProcessorProfiles, nullptr, CLSCTX_INPROC_SERVER, IID_ITfInputProcessorProfileMgr,
                   reinterpret_cast<void**>(&mgr));
  return mgr;
}

}  // namespace

int wmain(int argc, wchar_t** argv) {
  if (argc < 2) return 2;
  CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
  ITfInputProcessorProfileMgr* mgr = Mgr();
  if (!mgr) return 3;
  HRESULT hr = E_FAIL;
  const DWORD flags = TF_IPPMF_FORSESSION | TF_IPPMF_DONTCARECURRENTINPUTLANGUAGE;
  if (!lstrcmpW(argv[1], L"on")) {
    hr = mgr->ActivateProfile(TF_PROFILETYPE_INPUTPROCESSOR, MAKELANGID(LANG_CHINESE, SUBLANG_CHINESE_SIMPLIFIED),
                              kClsidT9Tip, kGuidT9Profile, nullptr, flags);
  } else if (!lstrcmpW(argv[1], L"save") && argc > 2) {
    TF_INPUTPROCESSORPROFILE p = {};
    hr = mgr->GetActiveProfile(GUID_TFCAT_TIP_KEYBOARD, &p);
    if (SUCCEEDED(hr)) {
      FILE* f = nullptr;
      if (_wfopen_s(&f, argv[2], L"wb") || !f) return 4;
      fwrite(&p, sizeof(p), 1, f);
      fclose(f);
    }
  } else if (!lstrcmpW(argv[1], L"restore") && argc > 2) {
    TF_INPUTPROCESSORPROFILE p = {};
    FILE* f = nullptr;
    if (_wfopen_s(&f, argv[2], L"rb") || !f) return 4;
    const size_t n = fread(&p, sizeof(p), 1, f);
    fclose(f);
    // Without DONTCARECURRENTINPUTLANGUAGE the input language itself switches;
    // with it, a profile of another language is only marked, not activated.
    if (n == 1) {
      hr = mgr->ActivateProfile(p.dwProfileType, p.langid, p.clsid, p.guidProfile, p.hkl, TF_IPPMF_FORSESSION);
      if (FAILED(hr)) hr = mgr->ActivateProfile(p.dwProfileType, p.langid, p.clsid, p.guidProfile, p.hkl, flags);
    }
  }
  mgr->Release();
  CoUninitialize();
  std::printf("hr=0x%08lx\n", static_cast<unsigned long>(hr));
  return SUCCEEDED(hr) ? 0 : 1;
}
