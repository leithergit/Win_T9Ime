#pragma once
// Listener for one named pipe endpoint: accepts clients and runs Serve() for
// each on its own thread. Stop() cancels pending I/O and joins everything.

#include <windows.h>

#include <atomic>
#include <cstdint>
#include <list>
#include <mutex>
#include <string>
#include <thread>

namespace t9ime::ipc {

class PipeServer {
 public:
  PipeServer() = default;
  PipeServer(const PipeServer&) = delete;
  PipeServer& operator=(const PipeServer&) = delete;
  virtual ~PipeServer() = default;  // derived classes call Stop() in their destructor

  // Fails if the name is already owned (FILE_FLAG_FIRST_PIPE_INSTANCE).
  bool Start(const std::wstring& name);
  void Stop();

 protected:
  // Runs on the connection's thread; the pipe is closed after it returns.
  // `stop` is signaled when the server stops.
  virtual void Serve(HANDLE pipe, uint64_t connection, HANDLE stop) = 0;

 private:
  struct Connection {
    HANDLE pipe = INVALID_HANDLE_VALUE;
    std::thread thread;
    std::atomic<bool> done{false};
  };
  void Listen(HANDLE first);
  void ReapFinished();

  std::wstring name_;
  HANDLE stop_ = nullptr;
  std::thread listener_;
  std::mutex mutex_;
  std::list<Connection> connections_;
  std::atomic<uint64_t> next_connection_{1};
};

}  // namespace t9ime::ipc
