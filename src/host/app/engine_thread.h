#pragma once
// Runs librime on a dedicated thread. The UI posts commands; after each command
// the engine publishes a snapshot, and the UI is notified with a window message.

#include <windows.h>

#include <condition_variable>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <thread>

#include "rime_engine.h"

namespace t9ime {

// Output that bypasses the engine (keys the application should receive as is).
// Routed through the engine thread so it stays ordered with committed text.
struct Passthrough {
  WORD vk = 0;        // virtual key, or 0 for text
  std::wstring text;
};

struct EngineSnapshot {
  EngineState state;
  std::vector<Candidate> candidates;  // first part of the whole candidate list
  std::vector<Passthrough> passthrough;  // sent after state.commit
};

class EngineThread {
 public:
  using Command = std::function<void(Session&, std::vector<Passthrough>&)>;

  EngineThread() = default;
  EngineThread(const EngineThread&) = delete;
  EngineThread& operator=(const EngineThread&) = delete;
  ~EngineThread() { Stop(); }

  // Starts the thread; initialization happens there. `notify_msg` is posted to
  // `notify_hwnd` whenever snapshots are available (wParam 1 = init failed).
  void Start(RimeEngine::Options options, std::string schema, HWND notify_hwnd, UINT notify_msg);
  void Stop();

  // Runs `cmd` on the engine thread and publishes a snapshot afterwards.
  void Post(Command cmd);

  // UI thread: takes all snapshots published so far, oldest first.
  std::deque<EngineSnapshot> Take();

  static constexpr size_t kCandidateLimit = 120;

 private:
  void Run(RimeEngine::Options options, std::string schema);

  HWND notify_hwnd_ = nullptr;
  UINT notify_msg_ = 0;
  std::thread thread_;
  std::mutex mutex_;
  std::condition_variable cv_;
  std::deque<Command> commands_;
  std::deque<EngineSnapshot> snapshots_;
  bool stop_ = false;
};

}  // namespace t9ime
