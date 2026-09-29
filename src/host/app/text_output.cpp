#include "text_output.h"

#include <string>
#include <vector>

namespace t9ime {

bool SendText(std::wstring_view text) {
  if (text.empty()) return true;
  std::vector<INPUT> inputs;
  inputs.reserve(text.size() * 2);
  for (wchar_t c : text) {
    INPUT in = {};
    in.type = INPUT_KEYBOARD;
    in.ki.wScan = c;
    in.ki.dwFlags = KEYEVENTF_UNICODE;
    inputs.push_back(in);
    in.ki.dwFlags |= KEYEVENTF_KEYUP;
    inputs.push_back(in);
  }
  const UINT sent = SendInput(static_cast<UINT>(inputs.size()), inputs.data(), sizeof(INPUT));
  return sent == inputs.size();
}

bool SendVirtualKey(WORD vk) {
  INPUT inputs[2] = {};
  inputs[0].type = inputs[1].type = INPUT_KEYBOARD;
  inputs[0].ki.wVk = inputs[1].ki.wVk = vk;
  inputs[1].ki.dwFlags = KEYEVENTF_KEYUP;
  return SendInput(2, inputs, sizeof(INPUT)) == 2;
}

std::wstring Utf8ToWide(std::string_view s) {
  if (s.empty()) return {};
  const int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0);
  std::wstring w(n, L'\0');
  MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), w.data(), n);
  return w;
}

std::string WideToUtf8(std::wstring_view w) {
  if (w.empty()) return {};
  const int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()), nullptr, 0, nullptr, nullptr);
  std::string s(n, '\0');
  WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()), s.data(), n, nullptr, nullptr);
  return s;
}

}  // namespace t9ime
