#pragma once
// Named pipes between the TIP and T9Host.
//   \\.\pipe\T9Ime.<SessionId>.<UserSid>.<endpoint>
// The server grants SYSTEM, the user, AppContainers (ALL APPLICATION PACKAGES
// and ALL RESTRICTED APPLICATION PACKAGES, Windows 8+) and low integrity
// clients. Clients never block longer than their timeout.

#include <windows.h>

#include <cstdint>
#include <string>
#include <vector>

namespace t9ime::ipc {

enum class Endpoint { kRequest, kEvents, kControl };

// Pipe name for the calling process's session and user (AppContainer processes
// report the logged-on user, so they compute the same name).
std::wstring PipeName(Endpoint endpoint);
std::wstring CurrentUserSid();

// Server: a new message-mode, overlapped pipe instance. `first` adds
// FILE_FLAG_FIRST_PIPE_INSTANCE (fails if someone else owns the name).
HANDLE CreatePipeInstance(const std::wstring& name, bool first);

// Blocking helpers for an overlapped handle; false on error or timeout (the
// pending I/O is cancelled). `stop` (optional) aborts the wait when signaled.
bool ReadMessage(HANDLE pipe, std::vector<uint8_t>* out, DWORD timeout_ms, HANDLE stop = nullptr);
bool WriteMessage(HANDLE pipe, const std::vector<uint8_t>& data, DWORD timeout_ms, HANDLE stop = nullptr);

class PipeClient {
 public:
  PipeClient() = default;
  PipeClient(const PipeClient&) = delete;
  PipeClient& operator=(const PipeClient&) = delete;
  ~PipeClient() { Close(); }

  bool Connect(const std::wstring& name, DWORD timeout_ms);
  // Sends a request and waits for the response. Each call gets its own
  // sequence number, so a late answer to a timed-out call is skipped by the
  // next one and the connection survives a slow server. Closes the
  // connection when the pipe breaks or after kMaxTimeouts timeouts in a row.
  bool Call(const std::vector<uint8_t>& request, std::vector<uint8_t>* response, DWORD timeout_ms);
  // One-way messages (events pipe). Close the connection on failure.
  bool Send(const std::vector<uint8_t>& message, DWORD timeout_ms);
  bool Receive(std::vector<uint8_t>* message, DWORD timeout_ms, HANDLE stop = nullptr);
  void Close();
  bool connected() const { return pipe_ != INVALID_HANDLE_VALUE; }
  // After a failed Call: true if it timed out (server alive but slow), false
  // if the pipe broke (server gone).
  bool last_failure_timed_out() const { return timed_out_; }

 private:
  static constexpr int kMaxTimeouts = 3;
  HANDLE pipe_ = INVALID_HANDLE_VALUE;
  bool timed_out_ = false;
  int timeouts_ = 0;
  uint32_t seq_ = 0;
};

}  // namespace t9ime::ipc
