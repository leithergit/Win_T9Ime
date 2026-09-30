#include "focus_registry.h"

#include "pipe.h"
#include "protocol.h"

namespace t9ime::ipc {

void FocusRegistry::Register(uint64_t client, DWORD pid, DWORD tid, std::wstring exe) {
  std::lock_guard lock(mutex_);
  Client& c = clients_[client];
  c.info.client = client;
  c.info.pid = pid;
  c.info.tid = tid;
  c.info.exe = std::move(exe);
}

void FocusRegistry::Unregister(uint64_t client) {
  bool was_focused = false;
  {
    std::lock_guard lock(mutex_);
    auto it = clients_.find(client);
    if (it == clients_.end()) return;
    was_focused = it->second.info.focused;
    clients_.erase(it);
  }
  if (was_focused) Notify();
}

void FocusRegistry::SetEventPipe(uint64_t client, HANDLE pipe) {
  std::lock_guard lock(mutex_);
  auto it = clients_.find(client);
  if (it != clients_.end()) it->second.events = pipe;
}

void FocusRegistry::ClearEventPipe(uint64_t client, HANDLE pipe) {
  std::lock_guard lock(mutex_);
  auto it = clients_.find(client);
  if (it != clients_.end() && it->second.events == pipe) it->second.events = nullptr;
}

void FocusRegistry::FocusIn(uint64_t client, HWND hwnd, std::vector<uint32_t> scopes, bool touch, bool read_only) {
  {
    std::lock_guard lock(mutex_);
    auto it = clients_.find(client);
    if (it == clients_.end()) return;
    FocusInfo& f = it->second.info;
    f.hwnd = hwnd;
    f.scopes = std::move(scopes);
    f.touch = touch;
    f.read_only = read_only;
    f.focused = true;
    // One focused field per thread (another TIP instance on the same thread
    // cannot exist, but clear stale state from reconnects of that thread).
    for (auto& [id, other] : clients_) {
      if (id != client && other.info.tid == f.tid) other.info.focused = false;
    }
  }
  Notify();
}

void FocusRegistry::FocusOut(uint64_t client) {
  {
    std::lock_guard lock(mutex_);
    auto it = clients_.find(client);
    if (it == clients_.end() || !it->second.info.focused) return;
    it->second.info.focused = false;
  }
  Notify();
}

bool FocusRegistry::Foreground(FocusInfo* out) const {
  const DWORD tid = GetWindowThreadProcessId(GetForegroundWindow(), nullptr);
  std::lock_guard lock(mutex_);
  for (const auto& [id, c] : clients_) {
    if (c.info.focused && c.info.tid == tid) {
      if (out) *out = c.info;
      return true;
    }
  }
  return false;
}

bool FocusRegistry::PushCommit(const std::wstring& text) {
  const DWORD tid = GetWindowThreadProcessId(GetForegroundWindow(), nullptr);
  std::lock_guard lock(mutex_);
  for (const auto& [id, c] : clients_) {
    if (!c.info.focused || c.info.tid != tid || !c.events) continue;
    Writer w(MsgType::kPushCommit);
    w.Str(kTagCommit, text);
    return WriteMessage(c.events, w.Finish(), 200);
  }
  return false;
}

void FocusRegistry::SetListener(std::function<void()> listener) {
  std::lock_guard lock(mutex_);
  listener_ = std::move(listener);
}

void FocusRegistry::Notify() {
  std::function<void()> listener;
  {
    std::lock_guard lock(mutex_);
    listener = listener_;
  }
  if (listener) listener();
}

}  // namespace t9ime::ipc
