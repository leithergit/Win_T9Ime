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
  FocusInfo info;
  {
    std::lock_guard lock(mutex_);
    auto it = clients_.find(client);
    if (it == clients_.end()) return;
    info = it->second.info;
    clients_.erase(it);
  }
  if (info.focused) {
    info.focused = false;
    Notify(info);
  }
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

void FocusRegistry::FocusIn(uint64_t client, HWND hwnd, std::vector<uint32_t> scopes, bool touch, bool read_only,
                            std::wstring touch_debug, bool no_context) {
  FocusInfo info;
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
    f.no_context = no_context;
    if (!touch_debug.empty()) f.touch_debug = std::move(touch_debug);
    // One focused field per thread (another TIP instance on the same thread
    // cannot exist, but clear stale state from reconnects of that thread).
    for (auto& [id, other] : clients_) {
      if (id != client && other.info.tid == f.tid) other.info.focused = false;
    }
    info = f;
  }
  Notify(info);
}

void FocusRegistry::FocusOut(uint64_t client) {
  FocusInfo info;
  {
    std::lock_guard lock(mutex_);
    auto it = clients_.find(client);
    if (it == clients_.end() || !it->second.info.focused) return;
    it->second.info.focused = false;
    info = it->second.info;
  }
  Notify(info);
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
    if (c.info.no_context) return false;  // e.g. password edit: SendInput reaches it, TSF cannot
    Writer w(MsgType::kPushCommit);
    w.Str(kTagCommit, text);
    return WriteMessage(c.events, w.Finish(), 200);
  }
  return false;
}

std::wstring FocusRegistry::Describe() const {
  const DWORD fg_tid = GetWindowThreadProcessId(GetForegroundWindow(), nullptr);
  std::lock_guard lock(mutex_);
  std::wstring out = L"foreground thread " + std::to_wstring(fg_tid) + L", " + std::to_wstring(clients_.size()) +
                     L" TIP client(s)\n";
  for (const auto& [id, c] : clients_) {
    const FocusInfo& f = c.info;
    out += L"  client " + std::to_wstring(id) + L": " + f.exe.substr(f.exe.find_last_of(L"\\/") + 1) + L" pid " +
           std::to_wstring(f.pid) + L" tid " + std::to_wstring(f.tid) + (f.focused ? L" FOCUSED" : L"") +
           (f.touch ? L" touch" : L"") + (f.read_only ? L" read-only" : L"") + (f.no_context ? L" no-tsf" : L"") + (c.events ? L" events" : L" NO-EVENTS") +
           L" scopes=";
    for (uint32_t s : f.scopes) out += std::to_wstring(s) + L",";
    out += L"\n";
    if (!f.touch_debug.empty()) out += L"    touch: " + f.touch_debug + L"\n";
  }
  return out;
}

void FocusRegistry::SetListener(std::function<void(const FocusInfo&)> listener) {
  std::lock_guard lock(mutex_);
  listener_ = std::move(listener);
}

void FocusRegistry::Notify(const FocusInfo& info) {
  std::function<void(const FocusInfo&)> listener;
  {
    std::lock_guard lock(mutex_);
    listener = listener_;
  }
  if (listener) listener(info);
}

}  // namespace t9ime::ipc
