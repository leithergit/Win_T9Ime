#pragma once
// librime wrapper used by T9Host (and t9repl). librime is not thread safe: every
// call on an engine and its sessions must come from one thread.

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "t9/pinyin_bar.h"
#include "t9/t9_composer.h"

struct rime_api_t;

namespace t9ime {

struct Candidate {
  std::string text;
  std::string comment;
};

struct EngineState {
  std::string schema_id;
  bool ascii_mode = false;
  bool composing = false;
  std::string input;        // raw Rime input, e.g. "zhong'4486"
  std::string raw_preedit;  // preedit as returned by librime
  std::string preedit;      // display text; readable pinyin for nine-key schemas
  int cursor = -1;          // caret in `preedit`, UTF-8 byte offset (-1 = end)
  std::vector<t9::BarItem> pinyin_bar;  // nine-key schemas only
  std::vector<Candidate> page;          // current candidate page
  int page_no = 0;
  bool last_page = true;
  int highlighted = 0;
  std::string commit;       // text committed since the previous State() call
};

class RimeEngine {
 public:
  struct Options {
    std::string shared_dir;  // UTF-8; contains build/ with pre-deployed data
    std::string user_dir;    // UTF-8; created if missing
    std::string log_dir;     // empty: logging disabled
    std::string app_name = "rime.t9ime";
    // Run librime maintenance (redeploys when sources look newer than build/).
    // Off by default: the pre-deployed data is used as is, independent of file
    // timestamps (zip extraction and time zones change them). Redeploy explicitly
    // after user configuration changes.
    bool maintenance = false;
  };

  RimeEngine() = default;
  RimeEngine(const RimeEngine&) = delete;
  RimeEngine& operator=(const RimeEngine&) = delete;
  ~RimeEngine();

  // setup + initialize + maintenance (blocking). Returns false on failure.
  bool Initialize(const Options& options);
  void Finalize();
  bool initialized() const noexcept { return api_ != nullptr; }
  rime_api_t* api() const noexcept { return api_; }

  // Maintenance (blocking; destroy every Session first - the user dictionary
  // database is opened exclusively and schemas are recompiled).
  //
  // Recompiles the configuration including the user's *.custom.yaml (fuzzy
  // pinyin, page size) into user_dir/build. Takes seconds.
  bool Redeploy();
  // Learned words of dictionary `dict` (e.g. "rime_ice") as a text file.
  // Return the number of entries, or -1 on failure.
  int ExportUserDict(const std::string& dict, const std::string& file);
  int ImportUserDict(const std::string& dict, const std::string& file);
  // Deletes everything learned into `dict`.
  bool ClearUserDict(const std::string& dict);

 private:
  rime_api_t* api_ = nullptr;
  Options options_;
};

// One Rime session plus the nine-key front end (pinyin bar, readable preedit,
// Enter/BackSpace semantics that Hamster's t9_processor provides elsewhere).
class Session {
 public:
  Session(RimeEngine& engine, std::string_view schema_id);
  Session(const Session&) = delete;
  Session& operator=(const Session&) = delete;
  ~Session();

  bool SelectSchema(std::string_view schema_id);
  void SetOption(const char* option, bool value);

  // Panel keys.
  void Key(char key);        // '1'..'9' on nine-key schemas, any printable ASCII otherwise
  void Backspace();
  void Enter();
  void Space();
  void Escape();
  void Clear();              // drop the composition without side effects
  bool PickBar(size_t index);                // index into State().pinyin_bar
  bool SelectCandidate(size_t global_index);
  bool SelectOnPage(size_t index);
  bool ChangePage(bool backward);

  std::string Input() const;
  bool HasInput() const { return !Input().empty(); }

  // Raw X11 keysym (physical keyboard path).
  bool ProcessKey(int keycode, int mask = 0);

  EngineState State();
  // Up to `limit` candidates of the whole list (for the expanded grid).
  std::vector<Candidate> Candidates(size_t limit);

 private:
  bool NineKey() const;
  void SetInput(const std::string& input);
  void CollectCommit();

  rime_api_t* api_;
  uintptr_t id_ = 0;
  std::string schema_id_;
  t9::T9Composer composer_;
  std::vector<t9::BarItem> last_bar_;
  std::string pending_commit_;
};

}  // namespace t9ime
