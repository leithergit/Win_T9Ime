#include "ctl_server.h"

#include "pipe.h"

namespace t9ime::ipc {

CtlServer::CtlServer(Handler handler, std::wstring pipe_name)
    : handler_(std::move(handler)), name_(pipe_name.empty() ? PipeName(Endpoint::kControl) : std::move(pipe_name)) {}

void CtlServer::Serve(HANDLE pipe, uint64_t, HANDLE stop) {
  std::vector<uint8_t> request;
  while (ReadMessage(pipe, &request, INFINITE, stop)) {
    const Reader reader(request.data(), request.size());
    if (!reader.ok()) break;
    std::vector<uint8_t> response = handler_(request);
    if (response.empty()) response = Writer(MsgType::kError, reader.seq()).Finish();
    if (!WriteMessage(pipe, response, 1000, stop)) break;
  }
}

}  // namespace t9ime::ipc
