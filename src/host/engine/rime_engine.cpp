#include "rime_engine.h"

#include <rime_api.h>

#include <algorithm>
#include <filesystem>
#include <system_error>

#include "t9/preedit_formatter.h"
#include "t9/syllable_index.h"

namespace t9ime {

namespace {

constexpr int kBackSpace = 0xff08;
constexpr int kReturn = 0xff0d;
constexpr int kEscape = 0xff1b;
constexpr int kSpace = 0x20;

std::string Str(const char* s) { return s ? s : ""; }

bool IsNineKeySchema(std::string_view id) { return id == "t9"; }

// Comment of the highlighted candidate, or of the next candidate on the page
// that has one (emoji candidates carry no pinyin).
std::string GuideComment(const RimeContext& ctx) {
  const RimeMenu& m = ctx.menu;
  for (int i = m.highlighted_candidate_index; i >= 0 && i < m.num_candidates; ++i) {
    if (m.candidates[i].comment && *m.candidates[i].comment) return m.candidates[i].comment;
  }
  return {};
}

// librime shows already selected words at the start of the preedit ("中486").
// Splits that prefix from the part still being typed.
std::string_view SelectedPrefix(std::string_view raw_preedit) {
  size_t i = 0;
  while (i < raw_preedit.size() && static_cast<unsigned char>(raw_preedit[i]) >= 0x80) ++i;
  return raw_preedit.substr(0, i);
}

}  // namespace

RimeEngine::~RimeEngine() { Finalize(); }

bool RimeEngine::Initialize(const Options& options) {
  if (api_) return true;
  std::error_code ec;
  const std::u8string user_dir(options.user_dir.begin(), options.user_dir.end());
  std::filesystem::create_directories(std::filesystem::path(user_dir), ec);
  if (ec) return false;

  RimeApi* api = rime_get_api();
  if (!api) return false;

  const std::string prebuilt = options.shared_dir + "/build";
  const std::string staging = options.user_dir + "/build";
  RIME_STRUCT(RimeTraits, traits);
  traits.shared_data_dir = options.shared_dir.c_str();
  traits.user_data_dir = options.user_dir.c_str();
  traits.prebuilt_data_dir = prebuilt.c_str();
  traits.staging_dir = staging.c_str();
  traits.distribution_name = "T9Ime";
  traits.distribution_code_name = "t9ime";
  traits.distribution_version = "0.1";
  traits.app_name = options.app_name.c_str();
  traits.log_dir = options.log_dir.c_str();         // "" = stderr only
  traits.min_log_level = options.log_dir.empty() ? 3 : 0;  // FATAL only when disabled

  api->setup(&traits);
  api->initialize(&traits);
  if (options.maintenance && api->start_maintenance(False)) api->join_maintenance_thread();
  api_ = api;
  return true;
}

void RimeEngine::Finalize() {
  if (!api_) return;
  api_->cleanup_all_sessions();
  api_->finalize();
  api_ = nullptr;
}

Session::Session(RimeEngine& engine, std::string_view schema_id) : api_(engine.api()) {
  id_ = api_->create_session();
  SelectSchema(schema_id);
}

Session::~Session() {
  if (id_) api_->destroy_session(id_);
}

bool Session::SelectSchema(std::string_view schema_id) {
  schema_id_ = schema_id;
  composer_.Reset();
  last_bar_.clear();
  return api_->select_schema(id_, schema_id_.c_str()) != False;
}

void Session::SetOption(const char* option, bool value) {
  api_->set_option(id_, option, value ? True : False);
}

bool Session::NineKey() const { return IsNineKeySchema(schema_id_); }

std::string Session::Input() const { return Str(api_->get_input(id_)); }

void Session::SetInput(const std::string& input) { api_->set_input(id_, input.c_str()); }

void Session::CollectCommit() {
  RIME_STRUCT(RimeCommit, commit);
  if (api_->get_commit(id_, &commit)) {
    pending_commit_ += Str(commit.text);
    api_->free_commit(&commit);
  }
}

bool Session::ProcessKey(int keycode, int mask) {
  const bool handled = api_->process_key(id_, keycode, mask) != False;
  CollectCommit();
  return handled;
}

void Session::Key(char key) { ProcessKey(static_cast<unsigned char>(key)); }

void Session::Backspace() {
  if (NineKey()) {
    if (auto undone = composer_.UndoLast(Input())) {
      SetInput(*undone);
      return;
    }
  }
  ProcessKey(kBackSpace);
}

void Session::Enter() {
  const std::string input = Input();
  if (!NineKey() || input.empty()) {
    ProcessKey(kReturn);
    return;
  }
  // Commit readable pinyin instead of the raw digits librime would commit.
  RIME_STRUCT(RimeContext, ctx);
  std::string text;
  if (api_->get_context(id_, &ctx)) {
    const std::string raw = Str(ctx.composition.preedit);
    std::string rest = raw.substr(SelectedPrefix(raw).size());
    std::erase(rest, ' ');
    const std::string typed =
        rest.size() <= input.size() ? input.substr(input.size() - rest.size()) : input;
    text = std::string(SelectedPrefix(raw)) + t9::EnterText(typed, GuideComment(ctx));
    api_->free_context(&ctx);
  }
  api_->clear_composition(id_);
  composer_.Reset();
  pending_commit_ += text;
}

void Session::Space() { ProcessKey(kSpace); }

void Session::Escape() {
  ProcessKey(kEscape);
  composer_.Reset();
}

bool Session::PickBar(size_t index) {
  if (!NineKey() || index >= last_bar_.size()) return false;
  auto next = composer_.Pick(Input(), last_bar_[index]);
  if (!next) return false;
  SetInput(*next);
  return true;
}

bool Session::SelectCandidate(size_t global_index) {
  const bool ok = api_->select_candidate(id_, global_index) != False;
  CollectCommit();
  return ok;
}

bool Session::SelectOnPage(size_t index) {
  const bool ok = api_->select_candidate_on_current_page(id_, index) != False;
  CollectCommit();
  return ok;
}

bool Session::ChangePage(bool backward) {
  return api_->change_page(id_, backward ? True : False) != False;
}

EngineState Session::State() {
  EngineState s;
  s.schema_id = schema_id_;
  s.input = Input();
  s.commit = std::move(pending_commit_);
  pending_commit_.clear();

  RIME_STRUCT(RimeStatus, status);
  if (api_->get_status(id_, &status)) {
    s.ascii_mode = status.is_ascii_mode != False;
    s.composing = status.is_composing != False;
    api_->free_status(&status);
  }

  bool partially_selected = false;
  RIME_STRUCT(RimeContext, ctx);
  if (api_->get_context(id_, &ctx)) {
    s.raw_preedit = Str(ctx.composition.preedit);
    partially_selected = !SelectedPrefix(s.raw_preedit).empty();
    s.page_no = ctx.menu.page_no;
    s.last_page = ctx.menu.is_last_page != False;
    s.highlighted = ctx.menu.highlighted_candidate_index;
    for (int i = 0; i < ctx.menu.num_candidates; ++i) {
      s.page.push_back({Str(ctx.menu.candidates[i].text), Str(ctx.menu.candidates[i].comment)});
    }
    s.preedit = s.raw_preedit;
    if (schema_id_ == "t9_eng" && !s.page.empty()) {
      // English nine-key: the digits mean nothing to the user; show the word.
      const int h = std::clamp(s.highlighted, 0, static_cast<int>(s.page.size()) - 1);
      s.preedit = s.page[h].text;
    } else if (NineKey() && !s.input.empty()) {
      const std::string_view prefix = SelectedPrefix(s.raw_preedit);
      std::string rest(s.raw_preedit.substr(prefix.size()));
      std::erase(rest, ' ');
      const std::string typed =
          rest.size() <= s.input.size() ? s.input.substr(s.input.size() - rest.size()) : s.input;
      s.preedit = std::string(prefix) + t9::FormatPreedit(typed, GuideComment(ctx));
    }
    api_->free_context(&ctx);
  }

  // Pinyin bar. Hidden after a partial candidate selection: picking would call
  // set_input, which drops the words already selected.
  if (NineKey() && !partially_selected) {
    composer_.Sync(s.input);
    if (auto seg = composer_.PendingSegment(s.input)) {
      // Syllables the top candidates use at this position come first.
      std::vector<std::string> preferred;
      const size_t k = composer_.confirmed_count();
      for (const Candidate& c : s.page) {
        size_t i = 0, start = 0;
        const std::string& cm = c.comment;
        for (size_t pos = 0; pos <= cm.size(); ++pos) {
          if (pos == cm.size() || cm[pos] == ' ') {
            if (i++ == k && pos > start) preferred.push_back(cm.substr(start, pos - start));
            start = pos + 1;
          }
        }
      }
      s.pinyin_bar = t9::BuildPinyinBar(t9::SyllableIndex::Builtin(),
                                        std::string_view(s.input).substr(seg->start, seg->length),
                                        preferred);
    }
  }
  last_bar_ = s.pinyin_bar;
  return s;
}

std::vector<Candidate> Session::Candidates(size_t limit) {
  std::vector<Candidate> out;
  RimeCandidateListIterator it = {};
  if (!api_->candidate_list_begin(id_, &it)) return out;
  while (out.size() < limit && api_->candidate_list_next(&it)) {
    out.push_back({Str(it.candidate.text), Str(it.candidate.comment)});
  }
  api_->candidate_list_end(&it);
  return out;
}

}  // namespace t9ime
