#pragma once
// Receives host pushes (touch panel output) on the events pipe. A background
// thread blocks on the pipe and hands messages to the UI thread by posting
// `message` to a window of that thread: TSF objects may only be used there.

#include <windows.h>

#include <cstdint>
#include <deque>
#include <mutex>
#include <string>
#include <thread>

namespace t9ime::tip {

class EventListener {
 public:
  EventListener() = default;
  EventListener(const EventListener&) = delete;
  EventListener& operator=(const EventListener&) = delete;
  ~EventListener() { Stop(); }

  // (Re)attaches to the host as `client`. Posts `message` to `notify` when
  // commits are queued.
  void Start(uint32_t client, HWND notify, UINT message);
  void Stop();
  uint32_t client() const { return client_; }

  // UI thread: takes the queued commit texts, oldest first.
  std::deque<std::wstring> Take();

 private:
  void Run(uint32_t client, HWND notify, UINT message);

  std::thread thread_;
  HANDLE stop_ = nullptr;
  uint32_t client_ = 0;
  std::mutex mutex_;
  std::deque<std::wstring> commits_;
};

}  // namespace t9ime::tip
