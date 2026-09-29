#include "engine_thread.h"

#include "win_compat.h"

namespace t9ime {

void EngineThread::Start(RimeEngine::Options options, std::string schema, HWND notify_hwnd,
                         UINT notify_msg) {
  notify_hwnd_ = notify_hwnd;
  notify_msg_ = notify_msg;
  stop_ = false;
  thread_ = std::thread(&EngineThread::Run, this, std::move(options), std::move(schema));
}

void EngineThread::Stop() {
  {
    std::lock_guard lock(mutex_);
    stop_ = true;
  }
  cv_.notify_all();
  if (thread_.joinable()) thread_.join();
}

void EngineThread::Post(Command cmd) {
  {
    std::lock_guard lock(mutex_);
    commands_.push_back(std::move(cmd));
  }
  cv_.notify_one();
}

std::deque<EngineSnapshot> EngineThread::Take() {
  std::lock_guard lock(mutex_);
  return std::exchange(snapshots_, {});
}

void EngineThread::Run(RimeEngine::Options options, std::string schema) {
  RimeEngine engine;
  if (!engine.Initialize(options)) {
    PostMessageW(notify_hwnd_, notify_msg_, 1, 0);
    return;
  }
  {
    Session session(engine, schema);
    // Windows 7 has no color emoji font: emoji candidates are disabled there.
    const bool emoji = compat::Os().AtLeastWin10();
    session.SetOption("emoji", emoji);

    auto publish = [&](std::vector<Passthrough> passthrough) {
      EngineSnapshot snap;
      snap.passthrough = std::move(passthrough);
      snap.state = session.State();
      if (!snap.state.page.empty()) snap.candidates = session.Candidates(kCandidateLimit);
      {
        std::lock_guard lock(mutex_);
        snapshots_.push_back(std::move(snap));
      }
      PostMessageW(notify_hwnd_, notify_msg_, 0, 0);
    };
    publish({});

    for (;;) {
      Command cmd;
      {
        std::unique_lock lock(mutex_);
        cv_.wait(lock, [&] { return stop_ || !commands_.empty(); });
        if (stop_) break;
        cmd = std::move(commands_.front());
        commands_.pop_front();
      }
      std::vector<Passthrough> passthrough;
      cmd(session, passthrough);
      session.SetOption("emoji", emoji);  // schema switches reset switches
      publish(std::move(passthrough));
    }
  }
  engine.Finalize();
}

}  // namespace t9ime
