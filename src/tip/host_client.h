#pragma once
// The TIP's connection to T9Host (one per text service instance, i.e. per UI
// thread). Every call has a short timeout; while the host is unreachable calls
// fail fast so keys pass through to the application.

#include <windows.h>

#include <optional>

#include "pipe.h"
#include "protocol.h"

namespace t9ime::tip {

class HostClient {
 public:
  static constexpr DWORD kKeyTimeoutMs = 150;
  static constexpr DWORD kOtherTimeoutMs = 500;

  // Request answered by kResult.
  std::optional<ipc::Result> Call(ipc::Writer request, DWORD timeout_ms = kKeyTimeoutMs);
  // Request answered by kAck.
  bool Notify(ipc::Writer request, DWORD timeout_ms = kOtherTimeoutMs);
  void Disconnect() { pipe_.Close(); }

 private:
  bool EnsureConnected();
  bool Exchange(ipc::Writer& request, std::vector<uint8_t>* response, DWORD timeout_ms);
  void MaybeStartHost();

  ipc::PipeClient pipe_;
  ULONGLONG next_attempt_ = 0;   // no connection attempts before this tick
  ULONGLONG next_launch_ = 0;    // no host launch before this tick
  DWORD backoff_ms_ = 500;
};

}  // namespace t9ime::tip
