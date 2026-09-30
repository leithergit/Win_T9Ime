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
  // Release Invoke() callers whose tasks never ran.
  std::lock_guard lock(mutex_);
  for (Item& item : commands_) {
    if (item.done) item.done->set_value();
  }
  commands_.clear();
}

Session* EngineContext::Client(uint64_t id) {
  if (!engine) return nullptr;
  auto& session = clients[id];
  if (!session) session = std::make_unique<Session>(*engine, "rime_ice");
  return session.get();
}

void EngineThread::Post(Command cmd) {
  {
    std::lock_guard lock(mutex_);
    commands_.push_back({std::move(cmd), nullptr, nullptr});
  }
  cv_.notify_one();
}

bool EngineThread::Invoke(Task task) {
  std::promise<void> done;
  auto future = done.get_future();
  {
    std::lock_guard lock(mutex_);
    if (stop_ || !thread_.joinable()) return false;
    commands_.push_back({nullptr, std::move(task), &done});
  }
  cv_.notify_one();
  future.wait();
  return true;
}

std::deque<EngineSnapshot> EngineThread::Take() {
  std::lock_guard lock(mutex_);
  return std::exchange(snapshots_, {});
}

void EngineThread::Run(RimeEngine::Options options, std::string schema) {
  RimeEngine engine;
  EngineContext ctx;
  if (!engine.Initialize(options)) {
    PostMessageW(notify_hwnd_, notify_msg_, 1, 0);
    // Keep serving Invoke() so IPC callers get an answer (ctx.engine == null).
    for (;;) {
      Item item;
      {
        std::unique_lock lock(mutex_);
        cv_.wait(lock, [&] { return stop_ || !commands_.empty(); });
        if (stop_) break;
        item = std::move(commands_.front());
        commands_.pop_front();
      }
      if (item.task) item.task(ctx);
      if (item.done) item.done->set_value();
    }
    return;
  }
  ctx.engine = &engine;
  {
    // Warm-up: the first lookup maps the dictionaries (tens of MB) from disk,
    // which on a cold start exceeds the TIP's 150 ms key timeout. Touch both
    // schemas before serving requests.
    {
      Session warm(engine, "rime_ice");
      for (char c : std::string("nihao")) warm.Key(c);
      warm.Candidates(10);
      warm.SelectSchema("t9");
      for (char c : std::string("94664486")) warm.Key(c);
      warm.Candidates(10);
    }
    Session session(engine, schema);
    ctx.panel = &session;
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
      Item item;
      {
        std::unique_lock lock(mutex_);
        cv_.wait(lock, [&] { return stop_ || !commands_.empty(); });
        if (stop_) break;
        item = std::move(commands_.front());
        commands_.pop_front();
      }
      if (item.task) {
        ctx.panel_changed = false;
        item.task(ctx);
        if (item.done) item.done->set_value();
        if (ctx.panel_changed) publish({});
        continue;
      }
      std::vector<Passthrough> passthrough;
      item.command(session, passthrough);
      session.SetOption("emoji", emoji);  // schema switches reset switches
      publish(std::move(passthrough));
    }
    ctx.clients.clear();
    ctx.panel = nullptr;
  }
  engine.Finalize();
}

}  // namespace t9ime
