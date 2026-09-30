// RimeEngine maintenance functions on the real pre-deployed data: user
// dictionary export / import / clear, redeploy.
#include <doctest/doctest.h>
#include <windows.h>

#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

#include "rime_engine.h"
#include "text_output.h"

using namespace t9ime;

namespace {

std::filesystem::path TempDir(const char* name) {
  auto dir = std::filesystem::temp_directory_path() /
             (std::string("t9ime-engine-") + name + "-" + std::to_string(GetCurrentProcessId()));
  std::filesystem::remove_all(dir);
  return dir;
}

RimeEngine::Options Opt(const std::filesystem::path& user) {
  RimeEngine::Options opt;
  opt.shared_dir = T9_TEST_DATA_DIR;
  opt.user_dir = WideToUtf8(user.wstring());
  return opt;
}

std::string ReadFile(const std::filesystem::path& p) {
  std::ifstream f(p, std::ios::binary);
  std::stringstream s;
  s << f.rdbuf();
  return s.str();
}

// Types `keys` and selects the candidate `text` (anywhere in the first 30),
// which makes librime learn it. True if found.
bool Learn(Session& s, const char* keys, const std::string& text) {
  s.Clear();
  for (const char* p = keys; *p; ++p) s.Key(*p);
  const auto list = s.Candidates(30);
  for (size_t i = 0; i < list.size(); ++i) {
    if (list[i].text == text) return s.SelectCandidate(i);
  }
  return false;
}

}  // namespace

TEST_CASE("engine: export, clear and import the user dictionary") {
  const auto user = TempDir("dict");
  const auto file = user.parent_path() / ("t9ime-dict-" + std::to_string(GetCurrentProcessId()) + ".txt");
  RimeEngine engine;
  REQUIRE(engine.Initialize(Opt(user)));
  {
    Session s(engine, "rime_ice");
    REQUIRE(Learn(s, "nihao", "拟好"));
    s.State();  // take the commit
  }
  const int exported = engine.ExportUserDict("rime_ice", WideToUtf8(file.wstring()));
  CHECK(exported >= 1);
  CHECK(ReadFile(file).find("拟好") != std::string::npos);

  REQUIRE(engine.ClearUserDict("rime_ice"));
  const auto empty = file.parent_path() / ("t9ime-dict-empty-" + std::to_string(GetCurrentProcessId()) + ".txt");
  CHECK(engine.ExportUserDict("rime_ice", WideToUtf8(empty.wstring())) <= 0);  // nothing learned (no database yet: -1)

  CHECK(engine.ImportUserDict("rime_ice", WideToUtf8(file.wstring())) >= 1);
  const auto again = file.parent_path() / ("t9ime-dict-again-" + std::to_string(GetCurrentProcessId()) + ".txt");
  CHECK(engine.ExportUserDict("rime_ice", WideToUtf8(again.wstring())) >= 1);
  CHECK(ReadFile(again).find("拟好") != std::string::npos);

  {
    Session s(engine, "rime_ice");  // the engine still works after clearing
    s.Key('n');
    CHECK_FALSE(s.Candidates(5).empty());
  }
  engine.Finalize();
  std::error_code ec;
  for (const auto& p : {file, empty, again}) std::filesystem::remove(p, ec);
  std::filesystem::remove_all(user, ec);
}
