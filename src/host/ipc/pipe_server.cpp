#include "pipe_server.h"

#include "pipe.h"

namespace t9ime::ipc {

bool PipeServer::Start(const std::wstring& name) {
  name_ = name;
  HANDLE first = CreatePipeInstance(name_, true);
  if (first == INVALID_HANDLE_VALUE) return false;
  stop_ = CreateEventW(nullptr, TRUE, FALSE, nullptr);
  listener_ = std::thread(&PipeServer::Listen, this, first);
  return true;
}

void PipeServer::Stop() {
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

void PipeServer::ReapFinished() {
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

void PipeServer::Listen(HANDLE pipe) {
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
    // The next instance first: a client connecting right after this one must
    // find a listening instance (otherwise it gets ERROR_FILE_NOT_FOUND).
    HANDLE next = WaitForSingleObject(stop_, 0) == WAIT_OBJECT_0 ? INVALID_HANDLE_VALUE
                                                                  : CreatePipeInstance(name_, false);
    if (ok) {
      ReapFinished();
      std::lock_guard lock(mutex_);
      Connection& c = connections_.emplace_back();
      c.pipe = pipe;
      const uint64_t id = next_connection_++;
      c.thread = std::thread([this, &c, id] {
        Serve(c.pipe, id, stop_);
        DisconnectNamedPipe(c.pipe);
        CloseHandle(c.pipe);
        c.done = true;
      });
    } else {
      CloseHandle(pipe);
    }
    pipe = next;
  }
  CloseHandle(connected);
}

}  // namespace t9ime::ipc
