#pragma once
// The TIP's connection to T9Host (one per text service instance, i.e. per UI
// thread). Every call has a short timeout; while the host is unreachable calls
// fail fast so keys pass through to the application.

#include <windows.h>

#include <functional>
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
  // Identity assigned by the host for the current connection; changes on
  // every reconnect (the events pipe must then be re-attached).
  uint32_t client_id() const { return client_id_; }
  uint32_t generation() const { return generation_; }
  // Called on the calling (UI) thread right after a new connection is made.
  void SetOnConnected(std::function<void()> f) { on_connected_ = std::move(f); }
  // Called when a call drops the connection (host gone, or several timeouts).
  void SetOnDisconnected(std::function<void()> f) { on_disconnected_ = std::move(f); }
  bool connected() const { return pipe_.connected(); }

 private:
  bool EnsureConnected();
  bool Exchange(ipc::Writer& request, std::vector<uint8_t>* response, DWORD timeout_ms);
  void MaybeStartHost();

  ipc::PipeClient pipe_;
  ULONGLONG next_attempt_ = 0;   // no connection attempts before this tick
  ULONGLONG slow_until_ = 0;     // host slow (warm-up, long lookup): engine requests fail fast until then
  ULONGLONG next_launch_ = 0;    // no host launch before this tick
  DWORD backoff_ms_ = 500;
  uint32_t client_id_ = 0;
  uint32_t generation_ = 0;
  std::function<void()> on_connected_;
  std::function<void()> on_disconnected_;
};

}  // namespace t9ime::tip
