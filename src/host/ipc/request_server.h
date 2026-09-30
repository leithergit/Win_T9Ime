#pragma once
// Request pipe: TIP requests (keys, focus, candidates). Every request runs on
// the engine thread (EngineThread::Invoke), one rime_ice session per client.

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "engine_thread.h"
#include "focus_registry.h"
#include "pipe_server.h"
#include "protocol.h"

namespace t9ime::ipc {

class RequestServer final : public PipeServer {
 public:
  // `pipe_name` defaults to PipeName(Endpoint::kRequest); tests pass their own.
  RequestServer(EngineThread& engine, FocusRegistry& focus, std::wstring pipe_name = {});
  // Extra diagnostics text (panel state), called on pipe threads.
  void SetDiagnostics(std::function<std::wstring()> f) { diagnostics_ = std::move(f); }
  ~RequestServer() override { Stop(); }

  bool Start() { return PipeServer::Start(name_); }

  // Builds the response for one request.
  std::vector<uint8_t> Handle(const Reader& request, uint64_t client);

 protected:
  void Serve(HANDLE pipe, uint64_t client, HANDLE stop) override;

 private:
  EngineThread& engine_;
  FocusRegistry& focus_;
  std::wstring name_;
  std::function<std::wstring()> diagnostics_;
};

// Events pipe: the host pushes panel output to the focused TIP. A client
// connects, identifies itself with kEventHello and then only reads.
class EventServer final : public PipeServer {
 public:
  EventServer(FocusRegistry& focus, std::wstring pipe_name = {});
  ~EventServer() override { Stop(); }
  bool Start() { return PipeServer::Start(name_); }

 protected:
  void Serve(HANDLE pipe, uint64_t connection, HANDLE stop) override;

 private:
  FocusRegistry& focus_;
  std::wstring name_;
};

}  // namespace t9ime::ipc
