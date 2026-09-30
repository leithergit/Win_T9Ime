#pragma once
// T9Ime's TSF identity and switching between input methods. Shared by the
// host and T9Ctl (the TIP keeps its own copy in globals.cpp).

#include <windows.h>
#include <msctf.h>

namespace t9ime {

extern const CLSID kClsidT9Tip;       // {BB2F3BA4-3B7A-414A-A98F-08C100E5447E}
extern const GUID kGuidT9Profile;     // {3EA4FF8C-CAF7-456F-9542-1C0F1EF2635E}
constexpr LANGID kT9LangId = MAKELANGID(LANG_CHINESE, SUBLANG_CHINESE_SIMPLIFIED);
// Keyboard layout of the T9Ime language (Chinese, Simplified).
inline HKL T9LanguageHkl() { return reinterpret_cast<HKL>(static_cast<ULONG_PTR>(0x08040804)); }

// The input method to switch to when leaving T9Ime: the first enabled
// keyboard layout of another language, else the first enabled input method
// that is not T9Ime. COM must be initialized. False if there is none.
bool FindOtherProfile(TF_INPUTPROCESSORPROFILE* out);

// ITfInputProcessorProfileMgr::ActivateProfile for `profile` with `flags`
// (TF_IPPMF_FORPROCESS / TF_IPPMF_FORSESSION ...). COM must be initialized.
bool ActivateProfile(const TF_INPUTPROCESSORPROFILE& profile, DWORD flags);
// The T9Ime profile.
TF_INPUTPROCESSORPROFILE T9Profile();

}  // namespace t9ime
