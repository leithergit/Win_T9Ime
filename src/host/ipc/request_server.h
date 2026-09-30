#pragma once
// Serves TIP requests on the request pipe. One thread per connection; every
// request runs on the engine thread (EngineThread::Invoke), one rime_ice
// session per connection.

#include <windows.h>

#include <atomic>
#include <cstdint>
#include <list>
#include <string>
#include <mutex>
#include <thread>
#include <vector>

#include "engine_thread.h"
#include "protocol.h"

namespace t9ime::ipc {

class RequestServer {
 public:
  // `pipe_name` defaults to PipeName(Endpoint::kRequest); tests pass their own.
  explicit RequestServer(EngineThread& engine, std::wstring pipe_name = {});
  RequestServer(const RequestServer&) = delete;
  RequestServer& operator=(const RequestServer&) = delete;
  ~RequestServer() { Stop(); }

  // Fails if the pipe name is already owned (another host is running).
  bool Start();
  void Stop();

  // Builds the response for one request (engine thread context via Invoke).
  std::vector<uint8_t> Handle(const Reader& request, uint64_t client);

 private:
  struct Connection {
    HANDLE pipe = INVALID_HANDLE_VALUE;
    std::thread thread;
    std::atomic<bool> done{false};
  };

  void Listen(HANDLE first);
  void Serve(Connection* c, uint64_t client);
  void ReapFinished();

  EngineThread& engine_;
  std::wstring name_;
  HANDLE stop_ = nullptr;
  std::thread listener_;
  std::mutex mutex_;
  std::list<Connection> connections_;
  std::atomic<uint64_t> next_client_{1};
};

}  // namespace t9ime::ipc
