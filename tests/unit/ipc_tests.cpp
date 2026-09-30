#include <doctest/doctest.h>
#include <windows.h>

#include <filesystem>
#include <string>

#include "engine_thread.h"
#include "pipe.h"
#include "protocol.h"
#include "request_server.h"
#include "text_output.h"

using namespace t9ime;
using namespace t9ime::ipc;

TEST_CASE("protocol round trip") {
  Writer w(MsgType::kKey, 42);
  w.U32(kTagKeycode, 0xff0d).U32(kTagMask, 1 << 30).Str(kTagExe, L"记事本.exe").Bool(kTagTest, true);
  const auto bytes = w.Finish();
  Reader r(bytes.data(), bytes.size());
  REQUIRE(r.ok());
  CHECK(r.type() == MsgType::kKey);
  CHECK(r.seq() == 42);
  CHECK(r.U32Or(kTagKeycode, 0) == 0xff0d);
  CHECK(r.U32Or(kTagMask, 0) == 1u << 30);
  CHECK(r.StrOr(kTagExe) == L"记事本.exe");
  CHECK(r.BoolOr(kTagTest, false));
  CHECK_FALSE(r.U32(kTagIndex).has_value());

  Result res;
  res.eaten = true;
  res.commit = L"中国";
  res.preedit = L"zhong guo";
  res.cursor = 9;
  res.candidates = {L"中国", L"种过"};
  res.comments = {L"", L"zhong guo"};
  res.highlighted = 1;
  res.composing = true;
  const auto enc = res.Encode(7);
  auto dec = Result::Decode(Reader(enc.data(), enc.size()));
  REQUIRE(dec);
  CHECK(dec->eaten);
  CHECK(dec->commit == L"中国");
  CHECK(dec->preedit == L"zhong guo");
  CHECK(dec->cursor == 9);
  CHECK(dec->candidates == res.candidates);
  CHECK(dec->comments == res.comments);
  CHECK(dec->highlighted == 1);
  CHECK(dec->composing);
  CHECK_FALSE(dec->ascii_mode);
}

TEST_CASE("protocol rejects malformed messages") {
  Writer w(MsgType::kHello);
  w.Str(kTagExe, L"a.exe");
  auto bytes = w.Finish();
  CHECK(Reader(bytes.data(), bytes.size()).ok());
  CHECK_FALSE(Reader(bytes.data(), bytes.size() - 1).ok());  // truncated field
  CHECK_FALSE(Reader(bytes.data(), 8).ok());                  // truncated header
  auto bad = bytes;
  bad[0] ^= 0xFF;
  CHECK_FALSE(Reader(bad.data(), bad.size()).ok());           // magic
  auto lying = bytes;
  lying[20] = 0xFF;                                           // field length beyond the end
  CHECK_FALSE(Reader(lying.data(), lying.size()).ok());
  CHECK_FALSE(Reader(nullptr, 0).ok());
}

namespace {

std::optional<Result> Call(PipeClient& c, Writer w) {
  std::vector<uint8_t> resp;
  if (!c.Call(w.Finish(), &resp, 5000)) return std::nullopt;
  return Result::Decode(Reader(resp.data(), resp.size()));
}

std::optional<Result> Key(PipeClient& c, int keysym) {
  Writer w(MsgType::kKey);
  w.U32(kTagKeycode, keysym).U32(kTagMask, 0);
  return Call(c, std::move(w));
}

std::optional<Result> Type(PipeClient& c, const char* keys) {
  std::optional<Result> last;
  for (const char* p = keys; *p; ++p) last = Key(c, *p);
  return last;
}

}  // namespace

TEST_CASE("request server: physical keyboard sessions over the pipe") {
  const std::filesystem::path user =
      std::filesystem::temp_directory_path() / ("t9ime-ipc-test-" + std::to_string(GetCurrentProcessId()));
  std::filesystem::remove_all(user);
  EngineThread engine;
  RimeEngine::Options opt;
  opt.shared_dir = T9_TEST_DATA_DIR;
  opt.user_dir = WideToUtf8(user.wstring());
  engine.Start(opt, "t9", nullptr, 0);

  const std::wstring name = L"\\\\.\\pipe\\T9Ime.test." + std::to_wstring(GetCurrentProcessId());
  FocusRegistry focus;
  RequestServer server(engine, focus, name);
  REQUIRE(server.Start());
  RequestServer second(engine, focus, name);
  CHECK_FALSE(second.Start());  // FILE_FLAG_FIRST_PIPE_INSTANCE

  PipeClient a, b;
  REQUIRE(a.Connect(name, 1000));
  REQUIRE(b.Connect(name, 1000));

  Writer hello(MsgType::kHello);
  hello.U32(kTagPid, GetCurrentProcessId()).Str(kTagExe, L"ipc_tests.exe");
  std::vector<uint8_t> resp;
  REQUIRE(a.Call(hello.Finish(), &resp, 5000));
  CHECK(Reader(resp.data(), resp.size()).type() == MsgType::kAck);

  auto r = Type(a, "nihao");
  REQUIRE(r);
  CHECK(r->eaten);
  CHECK(r->composing);
  CHECK(r->preedit == L"ni hao");
  REQUIRE(!r->candidates.empty());
  CHECK(r->candidates[0] == L"你好");
  CHECK(r->schema == L"rime_ice");

  // The second client has its own composition.
  auto rb = Type(b, "zhongguo");
  REQUIRE(rb);
  CHECK(rb->candidates[0] == L"中国");
  r = Call(a, Writer(MsgType::kQueryState));
  REQUIRE(r);
  CHECK(r->preedit == L"ni hao");

  // Space commits the first candidate.
  r = Key(a, 0x20);
  REQUIRE(r);
  CHECK(r->commit == L"你好");
  CHECK_FALSE(r->composing);

  // Keys the engine does not want are not eaten.
  r = Key(a, 0xff0d);  // Return without composition
  REQUIRE(r);
  CHECK_FALSE(r->eaten);

  // Select by index on the current page.
  Writer sel(MsgType::kSelectCandidate);
  sel.U32(kTagIndex, 0);
  r = Call(b, std::move(sel));
  REQUIRE(r);
  CHECK(r->commit == L"中国");

  // Focus out drops the composition.
  Type(a, "shi");
  REQUIRE(a.Call(Writer(MsgType::kFocusOut).Finish(), &resp, 5000));
  r = Call(a, Writer(MsgType::kQueryState));
  REQUIRE(r);
  CHECK_FALSE(r->composing);

  // ASCII mode (IMM32 conversion status / Shift toggle).
  Writer ascii(MsgType::kSetAsciiMode);
  ascii.Bool(kTagValue, true);
  r = Call(a, std::move(ascii));
  REQUIRE(r);
  CHECK(r->ascii_mode);
  r = Key(a, 'a');
  REQUIRE(r);
  CHECK_FALSE(r->eaten);

  // Garbage closes the connection; the client notices and can reconnect.
  std::vector<uint8_t> junk = {1, 2, 3};
  CHECK_FALSE(b.Call(junk, &resp, 2000));
  CHECK_FALSE(b.connected());
  REQUIRE(b.Connect(name, 1000));
  CHECK(Type(b, "a").has_value());

  a.Close();
  b.Close();
  server.Stop();
  engine.Stop();
  std::error_code ec;
  std::filesystem::remove_all(user, ec);
}

TEST_CASE("pipe client times out instead of blocking") {
  const std::wstring name = L"\\\\.\\pipe\\T9Ime.test.silent." + std::to_wstring(GetCurrentProcessId());
  HANDLE server = CreatePipeInstance(name, true);
  REQUIRE(server != INVALID_HANDLE_VALUE);
  PipeClient c;
  REQUIRE(c.Connect(name, 1000));
  std::vector<uint8_t> resp;
  const ULONGLONG start = GetTickCount64();
  CHECK_FALSE(c.Call(Writer(MsgType::kQueryState).Finish(), &resp, 150));  // server never answers
  const ULONGLONG spent = GetTickCount64() - start;
  CHECK(spent < 1000);
  CHECK_FALSE(c.connected());
  CloseHandle(server);
  CHECK_FALSE(c.Connect(L"\\\\.\\pipe\\T9Ime.test.nobody", 100));
}
