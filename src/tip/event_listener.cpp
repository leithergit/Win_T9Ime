#include "event_listener.h"

#include "pipe.h"
#include "protocol.h"

namespace t9ime::tip {

void EventListener::Start(uint32_t client, HWND notify, UINT message) {
  Stop();
  client_ = client;
  stop_ = CreateEventW(nullptr, TRUE, FALSE, nullptr);
  if (!stop_) return;
  thread_ = std::thread(&EventListener::Run, this, client, notify, message);
}

void EventListener::Stop() {
  if (stop_) SetEvent(stop_);
  if (thread_.joinable()) thread_.join();
  if (stop_) CloseHandle(stop_);
  stop_ = nullptr;
  client_ = 0;
}

std::deque<std::wstring> EventListener::Take() {
  std::lock_guard lock(mutex_);
  return std::exchange(commits_, {});
}

void EventListener::Run(uint32_t client, HWND notify, UINT message) {
  ipc::PipeClient pipe;
  // The host may still be creating its pipes: retry for a few seconds.
  for (int attempt = 0; !pipe.Connect(ipc::PipeName(ipc::Endpoint::kEvents), 200); ++attempt) {
    if (attempt >= 15 || WaitForSingleObject(stop_, 200) == WAIT_OBJECT_0) return;
  }
  // The host never answers kEventHello; from here on it only pushes.
  ipc::Writer hello(ipc::MsgType::kEventHello);
  hello.U32(ipc::kTagClient, client);
  if (!pipe.Send(hello.Finish(), 500)) return;
  std::vector<uint8_t> msg;
  while (pipe.Receive(&msg, INFINITE, stop_)) {
    const ipc::Reader r(msg.data(), msg.size());
    if (!r.ok()) break;
    if (r.type() == ipc::MsgType::kPushCommit) {
      {
        std::lock_guard lock(mutex_);
        commits_.push_back(r.StrOr(ipc::kTagCommit));
      }
      PostMessageW(notify, message, 0, 0);
    }
  }
}

}  // namespace t9ime::tip
