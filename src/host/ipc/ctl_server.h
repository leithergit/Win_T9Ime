#pragma once
// Control pipe (T9Ctl.dll / t9ctl.exe, ARCHITECTURE §7): request -> response.
// The handler runs on the connection's thread; the host marshals it to its UI
// thread.

#include <functional>
#include <string>
#include <vector>

#include "pipe_server.h"
#include "protocol.h"

namespace t9ime::ipc {

class CtlServer : public PipeServer {
 public:
  // Gets the raw request (validated); an empty answer becomes kError.
  using Handler = std::function<std::vector<uint8_t>(const std::vector<uint8_t>& request)>;
  CtlServer(Handler handler, std::wstring pipe_name = {});
  ~CtlServer() override { Stop(); }
  bool Start() { return PipeServer::Start(name_); }

 protected:
  void Serve(HANDLE pipe, uint64_t connection, HANDLE stop) override;

 private:
  Handler handler_;
  std::wstring name_;
};

}  // namespace t9ime::ipc
