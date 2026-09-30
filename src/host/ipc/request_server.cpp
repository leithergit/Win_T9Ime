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

RequestServer::RequestServer(EngineThread& engine, std::wstring pipe_name)
    : engine_(engine), name_(pipe_name.empty() ? PipeName(Endpoint::kRequest) : std::move(pipe_name)) {}

bool RequestServer::Start() {
  HANDLE first = CreatePipeInstance(name_, true);
  if (first == INVALID_HANDLE_VALUE) return false;
  stop_ = CreateEventW(nullptr, TRUE, FALSE, nullptr);
  listener_ = std::thread(&RequestServer::Listen, this, first);
  return true;
}

void RequestServer::Stop() {
  if (!stop_) return;
  SetEvent(stop_);
  if (listener_.joinable()) listener_.join();
  {
    std::lock_guard lock(mutex_);
    for (Connection& c : connections_) CancelIoEx(c.pipe, nullptr);
  }
  for (Connection& c : connections_) {
    if (c.thread.joinable()) c.thread.join();
  }
  connections_.clear();
  CloseHandle(stop_);
  stop_ = nullptr;
}

void RequestServer::ReapFinished() {
  std::lock_guard lock(mutex_);
  for (auto it = connections_.begin(); it != connections_.end();) {
    if (it->done) {
      it->thread.join();
      it = connections_.erase(it);
    } else {
      ++it;
    }
  }
}

void RequestServer::Listen(HANDLE pipe) {
  HANDLE connected = CreateEventW(nullptr, TRUE, FALSE, nullptr);
  while (pipe != INVALID_HANDLE_VALUE) {
    OVERLAPPED ov = {};
    ov.hEvent = connected;
    ResetEvent(connected);
    bool ok = ConnectNamedPipe(pipe, &ov) != FALSE;
    const DWORD err = ok ? 0 : GetLastError();
    if (err == ERROR_IO_PENDING) {
      HANDLE handles[2] = {connected, stop_};
      if (WaitForMultipleObjects(2, handles, FALSE, INFINITE) != WAIT_OBJECT_0) {
        CancelIoEx(pipe, &ov);
        DWORD dummy;
        GetOverlappedResult(pipe, &ov, &dummy, TRUE);
        CloseHandle(pipe);
        break;
      }
      DWORD dummy;
      ok = GetOverlappedResult(pipe, &ov, &dummy, FALSE) != FALSE;
    } else {
      ok = ok || err == ERROR_PIPE_CONNECTED;
    }
    if (ok) {
      ReapFinished();
      std::lock_guard lock(mutex_);
      Connection& c = connections_.emplace_back();
      c.pipe = pipe;
      c.thread = std::thread(&RequestServer::Serve, this, &c, next_client_++);
    } else {
      CloseHandle(pipe);
    }
    if (WaitForSingleObject(stop_, 0) == WAIT_OBJECT_0) break;
    pipe = CreatePipeInstance(name_, false);
  }
  CloseHandle(connected);
}

void RequestServer::Serve(Connection* c, uint64_t client) {
  std::vector<uint8_t> request;
  while (ReadMessage(c->pipe, &request, INFINITE, stop_)) {
    const Reader reader(request.data(), request.size());
    if (!reader.ok()) break;
    const std::vector<uint8_t> response = Handle(reader, client);
    if (!WriteMessage(c->pipe, response, 1000, stop_)) break;
  }
  engine_.Invoke([client](EngineContext& ctx) { ctx.clients.erase(client); });
  DisconnectNamedPipe(c->pipe);
  CloseHandle(c->pipe);
  c->done = true;
}

std::vector<uint8_t> RequestServer::Handle(const Reader& req, uint64_t client) {
  std::vector<uint8_t> out;
  const uint32_t seq = req.seq();
  engine_.Invoke([&](EngineContext& ctx) {
    Session* s = ctx.Client(client);
    if (!s) {
      out = Writer(MsgType::kError, seq).Finish();
      return;
    }
    bool eaten = false;
    switch (req.type()) {
      case MsgType::kHello:
      case MsgType::kFocusIn:
        out = Writer(MsgType::kAck, seq).Finish();
        return;
      case MsgType::kFocusOut:
        s->Clear();
        out = Writer(MsgType::kAck, seq).Finish();
        return;
      case MsgType::kKey:
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
