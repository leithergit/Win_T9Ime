#include <doctest/doctest.h>

#include "auto_show.h"

using namespace t9ime::panel;

namespace {
FocusEvent In(bool touch, std::vector<uint32_t> scopes = {}, std::wstring exe = L"notepad.exe") {
  FocusEvent e;
  e.focus_in = true;
  e.touch = touch;
  e.scopes = std::move(scopes);
  e.exe = std::move(exe);
  return e;
}
}  // namespace

TEST_CASE("auto show: touch focus shows, other focus does not") {
  AutoShowSettings s;
  CHECK(DecideOnFocus(In(true), s, Mode::kChinese, true).action == AutoAction::kShow);
  CHECK(DecideOnFocus(In(false), s, Mode::kChinese, true).action == AutoAction::kNone);
  s.always_show = true;
  CHECK(DecideOnFocus(In(false), s, Mode::kChinese, true).action == AutoAction::kShow);
  s.auto_show = false;
  CHECK(DecideOnFocus(In(true), s, Mode::kChinese, true).action == AutoAction::kNone);
}

TEST_CASE("auto show: focus out and read-only fields hide") {
  AutoShowSettings s;
  FocusEvent out;
  CHECK(DecideOnFocus(out, s, Mode::kChinese, true).action == AutoAction::kHide);
  FocusEvent ro = In(true);
  ro.read_only = true;
  CHECK(DecideOnFocus(ro, s, Mode::kChinese, true).action == AutoAction::kHide);
}

TEST_CASE("auto show: InputScope picks the layout") {
  AutoShowSettings s;
  CHECK(DecideOnFocus(In(true, {29}), s, Mode::kChinese, true).mode == Mode::kNumber);       // IS_NUMBER
  CHECK(DecideOnFocus(In(true, {32}), s, Mode::kChinese, true).mode == Mode::kNumber);       // telephone
  CHECK(DecideOnFocus(In(true, {1}), s, Mode::kChinese, true).mode == Mode::kEnglish);       // IS_URL
  CHECK(DecideOnFocus(In(true, {5}), s, Mode::kChinese, true).mode == Mode::kEnglish);       // e-mail
  CHECK(DecideOnFocus(In(true, {0}), s, Mode::kEnglish, true).mode == Mode::kEnglish);       // default: text mode
  const AutoDecision pw = DecideOnFocus(In(true, {31}), s, Mode::kChinese, true);  // IS_PASSWORD
  CHECK(pw.action == AutoAction::kShow);
  CHECK(pw.mode == Mode::kLetters);
  CHECK(DecideOnFocus(In(true, {63}), s, Mode::kChinese, true).mode == Mode::kNumber);       // numeric password
}

TEST_CASE("auto show: never under system overlays on Windows 10+") {
  AutoShowSettings s;
  CHECK(DecideOnFocus(In(true, {}, L"SearchHost.exe"), s, Mode::kChinese, true).action == AutoAction::kNone);
  CHECK(DecideOnFocus(In(true, {}, L"StartMenuExperienceHost.exe"), s, Mode::kChinese, true).action ==
        AutoAction::kNone);
  // Windows 7 has no such surfaces.
  CHECK(DecideOnFocus(In(true, {}, L"SearchHost.exe"), s, Mode::kChinese, false).action == AutoAction::kShow);
}
