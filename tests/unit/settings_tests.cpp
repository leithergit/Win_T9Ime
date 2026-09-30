// Input settings: generated Rime customizations, the settings file, and the
// effect of a redeploy on the real data (fuzzy pinyin).
#include <doctest/doctest.h>
#include <windows.h>

#include <filesystem>
#include <string>

#include "input_settings.h"
#include "rime_engine.h"
#include "text_output.h"

using namespace t9ime;

namespace {

const std::filesystem::path kShared = std::filesystem::path(T9_TEST_DATA_DIR);

std::filesystem::path TempDir(const char* name) {
  auto dir = std::filesystem::temp_directory_path() /
             (std::string("t9ime-settings-") + name + "-" + std::to_string(GetCurrentProcessId()));
  std::filesystem::remove_all(dir);
  return dir;
}

std::string FirstCandidate(RimeEngine& engine, const char* schema, const char* keys) {
  Session s(engine, schema);
  for (const char* p = keys; *p; ++p) s.Key(*p);
  const auto list = s.Candidates(1);
  return list.empty() ? std::string() : list[0].text;
}

}  // namespace

TEST_CASE("input settings: defaults use the shipped patch") {
  const InputSettings defaults;
  CHECK(GenerateCustomization(kShared, "t9", defaults).empty());
  CHECK(GenerateCustomization(kShared, "rime_ice", defaults).empty());
}

TEST_CASE("input settings: fuzzy pinyin goes before the nine-key digit mapping") {
  InputSettings s;
  s.fuzzy = {"nl"};
  const std::string t9 = GenerateCustomization(kShared, "t9", s);
  REQUIRE_FALSE(t9.empty());
  CHECK(t9.find("engine/processors") != std::string::npos);  // the shipped patch is kept
  const size_t fuzzy = t9.find("- derive/^n/l/");
  const size_t digits = t9.find("- derive/[abc]/2/");
  REQUIRE(fuzzy != std::string::npos);
  REQUIRE(digits != std::string::npos);
  CHECK(fuzzy < digits);
  CHECK(t9.find("menu/page_size") == std::string::npos);

  const std::string full = GenerateCustomization(kShared, "rime_ice", s);
  CHECK(full.find("  speller/algebra:\n    - derive/^n/l/") != std::string::npos);  // first rule
}

TEST_CASE("input settings: page size only for the physical keyboard") {
  InputSettings s;
  s.page_size = 7;
  CHECK(GenerateCustomization(kShared, "t9", s).empty());
  const std::string full = GenerateCustomization(kShared, "rime_ice", s);
  CHECK(full.find("menu/page_size: 7") != std::string::npos);
  CHECK(full.find("speller/algebra") == std::string::npos);
}

TEST_CASE("input settings: settings file round trip") {
  const auto dir = TempDir("ini");
  std::filesystem::create_directories(dir);
  const std::wstring ini = (dir / "panel.ini").wstring();
  InputSettings s;
  s.traditional = true;
  s.page_size = 8;
  s.fuzzy = {"z", "nl", "in"};
  SaveInputSettings(ini, s);
  CHECK(LoadInputSettings(ini) == s);
  CHECK(LoadInputSettings((dir / "missing.ini").wstring()) == InputSettings{});
  std::error_code ec;
  std::filesystem::remove_all(dir, ec);
}

TEST_CASE("input settings: redeploy applies fuzzy pinyin; defaults drop the compiled data") {
  const auto user = TempDir("deploy");
  InputSettings s;
  s.fuzzy = {"nl"};
  CHECK(PrepareUserData(kShared, user, s));  // needs a deployment
  RimeEngine engine;
  RimeEngine::Options opt;
  opt.shared_dir = T9_TEST_DATA_DIR;
  opt.user_dir = WideToUtf8(user.wstring());
  REQUIRE(engine.Initialize(opt));
  REQUIRE(engine.Redeploy());
  MarkDeployed(kShared, user);
  CHECK(FirstCandidate(engine, "t9", "54426") == "你好");     // lihao ~ nihao
  CHECK(FirstCandidate(engine, "rime_ice", "lihao") == "你好");
  CHECK_FALSE(PrepareUserData(kShared, user, s));             // up to date

  // Back to defaults: generated files and compiled data removed, the shipped
  // data applies again (after the engine restarts).
  engine.Finalize();
  CHECK_FALSE(PrepareUserData(kShared, user, InputSettings{}));
  CHECK_FALSE(std::filesystem::exists(user / "t9.custom.yaml"));
  CHECK_FALSE(std::filesystem::exists(user / "build"));
  REQUIRE(engine.Initialize(opt));
  CHECK(FirstCandidate(engine, "t9", "54426") != "你好");
  engine.Finalize();
  std::error_code ec;
  std::filesystem::remove_all(user, ec);
}
