#include "request_server.h"

#include "pipe.h"
#include "text_output.h"

namespace t9ime::ipc {

namespace {

// UTF-16 length of the first `bytes` bytes of a UTF-8 string.
int Utf16Offset(const std::string& utf8, int bytes) {
  if (bytes < 0 || bytes > static_cast<int>(utf8.size())) return -1;
  return static_cast<int>(Utf8ToWide(std::string_view(utf8).substr(0, bytes)).size());
}

Result ToResult(Session& s, bool eaten) {
  const EngineState st = s.State();
  Result r;
  r.eaten = eaten;
  r.commit = Utf8ToWide(st.commit);
  r.preedit = Utf8ToWide(st.preedit);
  r.cursor = st.cursor < 0 ? -1 : Utf16Offset(st.preedit, st.cursor);
  for (const Candidate& c : st.page) {
    r.candidates.push_back(Utf8ToWide(c.text));
    r.comments.push_back(Utf8ToWide(c.comment));
  }
  r.highlighted = st.highlighted;
  r.page_no = st.page_no;
  r.last_page = st.last_page;
  r.composing = st.composing;
  r.ascii_mode = st.ascii_mode;
  r.schema = Utf8ToWide(st.schema_id);
  return r;
}

}  // namespace

RequestServer::RequestServer(EngineThread& engine, FocusRegistry& focus, std::wstring pipe_name)
    : engine_(engine),
      focus_(focus),
      name_(pipe_name.empty() ? PipeName(Endpoint::kRequest) : std::move(pipe_name)) {}

void RequestServer::Serve(HANDLE pipe, uint64_t client, HANDLE stop) {
  std::vector<uint8_t> request;
  while (ReadMessage(pipe, &request, INFINITE, stop)) {
    const Reader reader(request.data(), request.size());
    if (!reader.ok()) break;
    const std::vector<uint8_t> response = Handle(reader, client);
    if (!WriteMessage(pipe, response, 1000, stop)) break;
  }
  focus_.Unregister(client);
  engine_.Invoke([client](EngineContext& ctx) { ctx.clients.erase(client); });
}

EventServer::EventServer(FocusRegistry& focus, std::wstring pipe_name)
    : focus_(focus), name_(pipe_name.empty() ? PipeName(Endpoint::kEvents) : std::move(pipe_name)) {}

void EventServer::Serve(HANDLE pipe, uint64_t, HANDLE stop) {
  std::vector<uint8_t> msg;
  if (!ReadMessage(pipe, &msg, 2000, stop)) return;
  const Reader hello(msg.data(), msg.size());
  if (!hello.ok() || hello.type() != MsgType::kEventHello) return;
  const uint64_t client = hello.U32Or(kTagClient, 0);
  focus_.SetEventPipe(client, pipe);
  // The client never writes again; this read ends when it disconnects.
  while (ReadMessage(pipe, &msg, INFINITE, stop)) {
  }
  focus_.ClearEventPipe(client, pipe);
}

std::vector<uint8_t> RequestServer::Handle(const Reader& req, uint64_t client) {
  std::vector<uint8_t> out;
  const uint32_t seq = req.seq();
  if (req.type() == MsgType::kDiagnostics) {
    Writer w(MsgType::kAck, seq);
    w.Str(kTagText, focus_.Describe() + (diagnostics_ ? diagnostics_() : std::wstring()));
    return w.Finish();
  }
  engine_.Invoke([&](EngineContext& ctx) {
    Session* s = ctx.Client(client);
    if (!s) {
      out = Writer(MsgType::kError, seq).Finish();
      return;
    }
    bool eaten = false;
    switch (req.type()) {
      case MsgType::kHello: {
        focus_.Register(client, req.U32Or(kTagPid, 0), req.U32Or(kTagTid, 0), req.StrOr(kTagExe));
        Writer ack(MsgType::kAck, seq);
        ack.U32(kTagClient, static_cast<uint32_t>(client));
        out = ack.Finish();
        return;
      }
      case MsgType::kFocusIn:
        focus_.FocusIn(client, reinterpret_cast<HWND>(static_cast<uintptr_t>(req.U32Or(kTagHwnd, 0))),
                       req.U32s(kTagInputScope), req.BoolOr(kTagTouch, false), req.BoolOr(kTagReadOnly, false),
                       req.StrOr(kTagText));
        out = Writer(MsgType::kAck, seq).Finish();
        return;
      case MsgType::kFocusOut:
        s->Clear();
        focus_.FocusOut(client);
        out = Writer(MsgType::kAck, seq).Finish();
        return;
      case MsgType::kKey:
        if (ctx.panel && ctx.panel->HasInput()) {
          ctx.panel->Clear();
          ctx.panel_changed = true;
        }
        eaten = s->ProcessKey(static_cast<int>(req.U32Or(kTagKeycode, 0)),
                              static_cast<int>(req.U32Or(kTagMask, 0)));
        break;
      case MsgType::kSelectCandidate:
        eaten = s->SelectOnPage(req.U32Or(kTagIndex, 0));
        break;
      case MsgType::kChangePage:
        eaten = s->ChangePage(req.BoolOr(kTagBackward, false));
        break;
      case MsgType::kClearComposition:
        s->Clear();
        break;
      case MsgType::kSetAsciiMode:
        s->SetOption("ascii_mode", req.BoolOr(kTagValue, false));
        break;
      case MsgType::kQueryState:
        break;
      default:
        out = Writer(MsgType::kError, seq).Finish();
        return;
    }
    out = ToResult(*s, eaten).Encode(seq);
  });
  if (out.empty()) out = Writer(MsgType::kError, seq).Finish();
  return out;
}

}  // namespace t9ime::ipc
