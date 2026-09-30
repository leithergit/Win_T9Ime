#pragma once
// T9Tip.dll globals: module handle, DLL reference count, GUIDs.
// The TIP structure follows Weasel's WeaselTSF (GPL-3.0, rime/weasel@d73f629);
// see third_party/weasel/UPSTREAM.md.

#include <windows.h>
#include <msctf.h>

#include <atomic>

namespace t9ime::tip {

extern HINSTANCE g_module;
extern std::atomic<long> g_dll_refs;

inline void DllAddRef() { ++g_dll_refs; }
inline void DllRelease() { --g_dll_refs; }

constexpr LANGID kLangId = MAKELANGID(LANG_CHINESE, SUBLANG_CHINESE_SIMPLIFIED);  // 0x0804
constexpr wchar_t kDescription[] = L"T9Ime 九宫格输入法";

extern const CLSID kClsidTextService;
extern const GUID kGuidProfile;
extern const GUID kGuidLangBarButton;
extern const GUID kGuidDisplayAttributeInput;
extern const GUID kGuidCandidateUI;

// Declared in newer SDK headers but not always present in uuid.lib.
extern const GUID kGuidTfcatTipcapImmersiveSupport;
extern const GUID kGuidTfcatTipcapSystraySupport;
extern const GUID kGuidLbiInputMode;


// Wraps a COM method body: exceptions never cross the COM boundary.
template <typename F>
HRESULT Guard(F&& f) noexcept {
  try {
    return f();
  } catch (...) {
    return E_FAIL;
  }
}

}  // namespace t9ime::tip
