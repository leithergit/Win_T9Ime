#pragma once
// Runs librime on a dedicated thread. The UI posts commands; after each command
// the engine publishes a snapshot, and the UI is notified with a window message.

#include <windows.h>

#include <atomic>
#include <condition_variable>
#include <deque>
#include <functional>
#include <future>
#include <map>
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

// State owned by the engine thread, reachable from Invoke().
struct EngineContext {
  RimeEngine* engine = nullptr;  // null if initialization failed
  Session* panel = nullptr;      // the touch panel's session
  bool panel_changed = false;    // set by tasks that modify `panel`: a snapshot is published
  // Physical-keyboard sessions (rime_ice), one per TIP connection.
  std::map<uint64_t, std::unique_ptr<Session>> clients;
  Session* Client(uint64_t id);
};

class EngineThread {
 public:
  using Command = std::function<void(Session&, std::vector<Passthrough>&)>;
  using Task = std::function<void(EngineContext&)>;

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

  // Runs `task` on the engine thread and waits until it has finished (IPC
  // handler threads). Returns false if the engine thread is stopping.
  bool Invoke(Task task);

  // UI thread: takes all snapshots published so far, oldest first.
  std::deque<EngineSnapshot> Take();

  static constexpr size_t kCandidateLimit = 120;

  // True once the dictionaries are warmed up (any thread; diagnostics, tests).
  bool ready() const { return ready_.load(); }

 private:
  std::atomic<bool> ready_{false};
  void Run(RimeEngine::Options options, std::string schema);

  HWND notify_hwnd_ = nullptr;
  UINT notify_msg_ = 0;
  std::thread thread_;
  std::mutex mutex_;
  std::condition_variable cv_;
  struct Item {
    Command command;                 // panel command (publishes a snapshot)
    Task task;                       // or an Invoke() task
    std::promise<void>* done = nullptr;
  };
  std::deque<Item> commands_;
  std::deque<EngineSnapshot> snapshots_;
  bool stop_ = false;
};

}  // namespace t9ime
