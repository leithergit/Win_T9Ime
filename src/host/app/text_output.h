#pragma once
// Output to the focused application. M2 path: SendInput. From M4 on this is only
// the fallback for windows without a TSF connection.

#include <windows.h>

#include <string>
#include <string_view>

namespace t9ime {

// Types UTF-16 text with KEYEVENTF_UNICODE. Returns false if input was blocked
// (UIPI: e.g. the foreground window is elevated).
bool SendText(std::wstring_view text);
// Presses and releases a virtual key (VK_BACK, VK_RETURN, ...).
bool SendVirtualKey(WORD vk);

std::wstring Utf8ToWide(std::string_view s);
std::string WideToUtf8(std::wstring_view w);

}  // namespace t9ime
