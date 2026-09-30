#include "auto_show.h"

#include <algorithm>
#include <cwctype>

namespace t9ime::panel {

namespace {

// InputScope values (InputScope.h).
constexpr uint32_t kUrl = 1, kEmailUser = 4, kEmailAddress = 5, kLoginName = 6, kDigits = 28, kNumber = 29,
                   kPassword = 31, kPhoneFull = 32, kPhoneCountry = 33, kPhoneArea = 34, kPhoneLocal = 35,
                   kNumberFullWidth = 39, kEmailNameOrAddress = 60, kPrivate = 61, kNumericPassword = 63,
                   kNumericPin = 64, kAlphanumericPin = 65;

bool Has(const std::vector<uint32_t>& scopes, std::initializer_list<uint32_t> any) {
  return std::any_of(scopes.begin(), scopes.end(),
                     [&](uint32_t s) { return std::find(any.begin(), any.end(), s) != any.end(); });
}

// Shell surfaces above the normal window band (Windows 8+): a non-uiAccess
// topmost panel would pop up underneath them (ARCHITECTURE §6.6).
bool IsSystemOverlay(std::wstring exe) {
  std::transform(exe.begin(), exe.end(), exe.begin(), [](wchar_t c) { return static_cast<wchar_t>(std::towlower(c)); });
  for (const wchar_t* name : {L"searchhost.exe", L"searchapp.exe", L"searchui.exe", L"shellexperiencehost.exe",
                              L"startmenuexperiencehost.exe", L"lockapp.exe", L"textinputhost.exe"}) {
    if (exe == name) return true;
  }
  return false;
}

}  // namespace

Mode ModeForScopes(const std::vector<uint32_t>& scopes, Mode text_mode) {
  if (Has(scopes, {kDigits, kNumber, kNumberFullWidth, kPhoneFull, kPhoneCountry, kPhoneArea, kPhoneLocal,
                   kNumericPin, kNumericPassword})) {
    return Mode::kNumber;
  }
  // Passwords, addresses, user names: the QWERTY keyboard (letters typed directly).
  if (Has(scopes, {kPassword, kPrivate, kAlphanumericPin, kUrl, kEmailUser, kEmailAddress, kEmailNameOrAddress,
                   kLoginName})) {
    return Mode::kEnglish;
  }
  return text_mode;
}

AutoDecision DecideOnFocus(const FocusEvent& e, const AutoShowSettings& settings, Mode text_mode,
                           bool win8_or_later) {
  AutoDecision d;
  if (!e.focus_in || e.read_only) {
    d.action = AutoAction::kHide;
    d.now = e.deactivated;
    return d;
  }
  if (win8_or_later && IsSystemOverlay(e.exe)) return d;
  const bool by_focus = settings.auto_show && (e.touch || settings.always_show);
  if (!by_focus && !(e.switched && settings.show_on_switch)) return d;
  d.action = AutoAction::kShow;
  d.mode = ModeForScopes(e.scopes, text_mode);
  return d;
}

}  // namespace t9ime::panel
