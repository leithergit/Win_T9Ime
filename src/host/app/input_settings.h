#pragma once
// Input settings (SPEC §8): simplified / traditional, candidates per page on
// the physical keyboard, fuzzy pinyin. The last two change how schemas are
// compiled: they are written as the user's Rime customization files
// (<user>/t9.custom.yaml, rime_ice.custom.yaml = the shipped patch + the
// settings) and need a redeploy.

#include <filesystem>
#include <string>
#include <vector>

namespace t9ime {

struct InputSettings {
  bool traditional = false;  // traditional characters (runtime option, no redeploy)
  int page_size = 5;         // physical keyboard candidates per page, 5..9
  std::vector<std::string> fuzzy;  // FuzzyOption ids, in catalog order

  bool operator==(const InputSettings&) const = default;
  // The part that is compiled into the schemas.
  bool SameDeployment(const InputSettings& o) const { return page_size == o.page_size && fuzzy == o.fuzzy; }
};

struct FuzzyOption {
  const char* id;
  const wchar_t* label;             // e.g. L"z = zh"
  std::vector<const char*> rules;   // Rime spelling algebra
};
const std::vector<FuzzyOption>& FuzzyOptions();

constexpr int kMinPageSize = 5, kMaxPageSize = 9;

// [input] section of the settings file (panel.ini).
InputSettings LoadInputSettings(const std::wstring& ini_file);
void SaveInputSettings(const std::wstring& ini_file, const InputSettings& settings);

// Brings the user directory in line with `settings`: writes or removes the
// generated customization files, and removes compiled data that no longer
// matches the shipped data or the customizations. Returns true if the schemas
// must be (re)deployed before use.
bool PrepareUserData(const std::filesystem::path& shared_dir, const std::filesystem::path& user_dir,
                     const InputSettings& settings);
// After a successful deployment: remembers what the compiled data was built from.
void MarkDeployed(const std::filesystem::path& shared_dir, const std::filesystem::path& user_dir);

// For tests: the customization text generated for `schema` ("t9" / "rime_ice"),
// empty if the shipped patch applies unchanged.
std::string GenerateCustomization(const std::filesystem::path& shared_dir, const std::string& schema,
                                  const InputSettings& settings);

}  // namespace t9ime
