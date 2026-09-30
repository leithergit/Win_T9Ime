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
  if (!session) {
    session = std::make_unique<Session>(*engine, "rime_ice");
    ApplyOptions(*session);
  }
  return session.get();
}

void EngineContext::ApplyOptions(Session& s) const {
  for (const auto& [name, value] : options) s.SetOption(name.c_str(), value);
}

void EngineThread::Maintain(Maintenance work, std::function<void(bool)> done) {
  {
    std::lock_guard lock(mutex_);
    Item item;
    item.maintenance = std::move(work);
    item.maintenance_done = std::move(done);
    commands_.push_back(std::move(item));
  }
  cv_.notify_one();
}

void EngineThread::SetOption(std::string name, bool value) {
  {
    std::lock_guard lock(mutex_);
    if (!thread_.joinable()) {  // not started yet
      initial_options_[name] = value;
      return;
    }
  }
  // Queued, not waited for: the UI must not block behind a redeploy.
  Item item;
  item.task = [name, value](EngineContext& ctx) {
    ctx.options[name] = value;
    if (ctx.panel) ctx.panel->SetOption(name.c_str(), value);
    for (auto& [id, session] : ctx.clients) session->SetOption(name.c_str(), value);
    ctx.panel_changed = true;  // e.g. candidates in traditional characters
  };
  {
    std::lock_guard lock(mutex_);
    commands_.push_back(std::move(item));
  }
  cv_.notify_one();
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
  ctx.options = initial_options_;
  // Windows 7 has no color emoji font: emoji candidates are disabled there.
  ctx.options["emoji"] = compat::Os().AtLeastWin10();
  if (deploy_on_start_) deploy_on_start_(engine.Redeploy());
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
    ready_ = true;
    auto session = std::make_unique<Session>(engine, schema);
    ctx.panel = session.get();
    ctx.ApplyOptions(*session);

    auto publish = [&](std::vector<Passthrough> passthrough) {
      EngineSnapshot snap;
      snap.passthrough = std::move(passthrough);
      snap.state = session->State();
      if (!snap.state.page.empty()) snap.candidates = session->Candidates(kCandidateLimit);
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
      if (item.maintenance) {
        // Every session closed (exclusive dictionary access, recompiled
        // schemas), then the panel session again; TIP sessions come back on
        // their next request.
        ctx.clients.clear();
        ctx.panel = nullptr;
        const std::string panel_schema = session->schema_id();
        session.reset();
        const bool ok = item.maintenance(engine);
        session = std::make_unique<Session>(engine, panel_schema.empty() ? schema : panel_schema);
        ctx.panel = session.get();
        ctx.ApplyOptions(*session);
        publish({});
        if (item.maintenance_done) item.maintenance_done(ok);
        continue;
      }
      if (item.task) {
        ctx.panel_changed = false;
        item.task(ctx);
        if (item.done) item.done->set_value();
        if (ctx.panel_changed) publish({});
        continue;
      }
      std::vector<Passthrough> passthrough;
      item.command(*session, passthrough);
      ctx.ApplyOptions(*session);  // schema switches reset switches
      publish(std::move(passthrough));
    }
    ctx.clients.clear();
    ctx.panel = nullptr;
  }
  engine.Finalize();
}

}  // namespace t9ime
