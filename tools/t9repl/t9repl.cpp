// t9repl: command-line driver for the T9Ime engine (librime + nine-key front end).
//
//   t9repl --data <shared_dir> --user <user_dir> [--schema t9] [--fresh] [--log <dir>]
//          [--script <file>] [--maintenance]
//
// Commands (one per line; '#' starts a comment):
//   k <keys>      type keys; on nine-key schemas 1-9, otherwise ASCII
//   bs | enter | space | esc
//   bar           show the whole pinyin bar
//   pick <n>      pick pinyin bar item n (1-based)
//   sel <n>       select candidate n of the whole list (1-based)
//   page+ | page-
//   list <n>      show the first n candidates of the whole list
//   schema <id>   switch schema (t9, t9_eng, rime_ice)
//   opt <name> <0|1>
//   show          print the state again
// After every command the state is printed on one line.

#include <windows.h>

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

#include "rime_engine.h"

namespace {

std::string Utf8(const std::wstring& w) {
  if (w.empty()) return {};
  int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()), nullptr, 0, nullptr, nullptr);
  std::string s(n, '\0');
  WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()), s.data(), n, nullptr, nullptr);
  return s;
}

void Print(const t9ime::EngineState& s, size_t max_cands = 5) {
  std::ostringstream o;
  o << "  input=[" << s.input << "] preedit=[" << s.preedit << "]";
  if (!s.pinyin_bar.empty()) {
    o << " bar=";
    for (size_t i = 0; i < s.pinyin_bar.size() && i < 8; ++i) o << (i ? "," : "") << s.pinyin_bar[i].spelling;
    if (s.pinyin_bar.size() > 8) o << ",...";
  }
  if (!s.page.empty()) {
    o << " cands=";
    for (size_t i = 0; i < s.page.size() && i < max_cands; ++i) {
      o << (i ? " " : "") << s.page[i].text;
      if (!s.page[i].comment.empty()) o << "(" << s.page[i].comment << ")";
    }
  }
  if (!s.commit.empty()) o << " commit=[" << s.commit << "]";
  std::fputs((o.str() + "\n").c_str(), stdout);
}

bool Run(t9ime::Session& session, const std::string& line) {
  std::istringstream in(line);
  std::string cmd, arg, arg2;
  in >> cmd;
  std::getline(in >> std::ws, arg);
  if (cmd.empty() || cmd[0] == '#') return true;
  std::fputs(("> " + line + "\n").c_str(), stdout);

  if (cmd == "k") {
    for (char c : arg) {
      if (c != ' ') session.Key(c);
    }
  } else if (cmd == "bs") {
    session.Backspace();
  } else if (cmd == "enter") {
    session.Enter();
  } else if (cmd == "space") {
    session.Space();
  } else if (cmd == "esc") {
    session.Escape();
  } else if (cmd == "pick") {
    if (!session.PickBar(std::stoul(arg) - 1)) std::fputs("  (pick failed)\n", stdout);
  } else if (cmd == "sel") {
    if (!session.SelectCandidate(std::stoul(arg) - 1)) std::fputs("  (select failed)\n", stdout);
  } else if (cmd == "page+" || cmd == "page-") {
    session.ChangePage(cmd == "page-");
  } else if (cmd == "schema") {
    if (!session.SelectSchema(arg)) std::fputs("  (schema failed)\n", stdout);
  } else if (cmd == "opt") {
    std::istringstream a(arg);
    std::string name;
    int v = 0;
    a >> name >> v;
    session.SetOption(name.c_str(), v != 0);
  } else if (cmd == "bar") {
    auto s = session.State();
    std::string out = "  bar:";
    for (const auto& b : s.pinyin_bar) out += " " + b.spelling + "/" + std::to_string(b.consumed);
    std::fputs((out + "\n").c_str(), stdout);
    return true;
  } else if (cmd == "list") {
    std::string out = "  list:";
    for (const auto& c : session.Candidates(std::stoul(arg))) out += " " + c.text;
    std::fputs((out + "\n").c_str(), stdout);
    return true;
  } else if (cmd == "quit" || cmd == "exit") {
    return false;
  } else if (cmd != "show") {
    std::fputs(("  unknown command: " + cmd + "\n").c_str(), stdout);
    return true;
  }
  Print(session.State());
  return true;
}

}  // namespace

int wmain(int argc, wchar_t** argv) {
  SetConsoleOutputCP(CP_UTF8);
  t9ime::RimeEngine::Options opt;
  std::string schema = "t9", script;
  bool fresh = false;
  for (int i = 1; i < argc; ++i) {
    std::wstring a = argv[i];
    auto next = [&] { return i + 1 < argc ? Utf8(argv[++i]) : std::string(); };
    if (a == L"--data") opt.shared_dir = next();
    else if (a == L"--user") opt.user_dir = next();
    else if (a == L"--schema") schema = next();
    else if (a == L"--log") opt.log_dir = next();
    else if (a == L"--script") script = next();
    else if (a == L"--fresh") fresh = true;
    else if (a == L"--maintenance") opt.maintenance = true;
    else {
      std::fputs("usage: t9repl --data <dir> --user <dir> [--schema id] [--fresh] [--log dir] [--script file]\n", stderr);
      return 2;
    }
  }
  if (opt.shared_dir.empty() || opt.user_dir.empty()) {
    std::fputs("--data and --user are required\n", stderr);
    return 2;
  }
  if (fresh) {
    std::error_code ec;
    std::filesystem::remove_all(std::filesystem::path(std::u8string(opt.user_dir.begin(), opt.user_dir.end())), ec);
  }

  t9ime::RimeEngine engine;
  const ULONGLONG start = GetTickCount64();
  const bool ok = engine.Initialize(opt);
  std::fprintf(stderr, "engine init: %llu ms\n", GetTickCount64() - start);
  if (!ok) {
    std::fputs("engine initialization failed\n", stderr);
    return 1;
  }
  t9ime::Session session(engine, schema);

  std::ifstream file;
  std::istream* in = &std::cin;
  if (!script.empty()) {
    file.open(std::filesystem::path(std::u8string(script.begin(), script.end())));
    if (!file) {
      std::fputs("cannot open script\n", stderr);
      return 2;
    }
    in = &file;
  }
  std::string line;
  while (std::getline(*in, line)) {
    if (!line.empty() && line.back() == '\r') line.pop_back();
    if (!Run(session, line)) break;
  }
  return 0;
}
