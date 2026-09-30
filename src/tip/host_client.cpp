#include "host_client.h"

#include <string>

#include "globals.h"

namespace t9ime::tip {

namespace {

constexpr DWORD kMaxBackoffMs = 5000;
constexpr DWORD kLaunchIntervalMs = 10000;

// Only a process with the user's normal token may start the host: not an
// AppContainer / low integrity process, not an elevated process of a UAC
// split-token admin. With UAC off (typical for Windows 7 VMs) every process is
// high integrity with elevation type "default": that is the normal token there.
bool MayLaunchHost() {
  HANDLE token = nullptr;
  if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token)) return false;
  bool ok = true;
  DWORD len = 0;
  DWORD is_container = 0;
  constexpr auto kTokenIsAppContainer = static_cast<TOKEN_INFORMATION_CLASS>(29);  // Windows 8+
  if (GetTokenInformation(token, kTokenIsAppContainer, &is_container, sizeof(is_container), &len) && is_container) {
    ok = false;
  }
  TOKEN_ELEVATION_TYPE elevation = TokenElevationTypeDefault;
  if (GetTokenInformation(token, TokenElevationType, &elevation, sizeof(elevation), &len) &&
      elevation == TokenElevationTypeFull) {
    ok = false;
  }
  BYTE buf[64];
  if (GetTokenInformation(token, TokenIntegrityLevel, buf, sizeof(buf), &len)) {
    auto* label = reinterpret_cast<TOKEN_MANDATORY_LABEL*>(buf);
    const DWORD rid = *GetSidSubAuthority(label->Label.Sid, *GetSidSubAuthorityCount(label->Label.Sid) - 1);
    if (rid < SECURITY_MANDATORY_MEDIUM_RID || rid >= SECURITY_MANDATORY_SYSTEM_RID) ok = false;
  }
  CloseHandle(token);
  return ok;
}

std::wstring HostPath() {
  wchar_t path[MAX_PATH];
  const DWORD n = GetModuleFileNameW(g_module, path, MAX_PATH);
  std::wstring s(path, n);
  return s.substr(0, s.find_last_of(L"\\/") + 1) + L"T9Host.exe";
}

}  // namespace

bool HostClient::EnsureConnected() {
  if (pipe_.connected()) return true;
  const ULONGLONG now = GetTickCount64();
  if (now < next_attempt_) return false;
  if (pipe_.Connect(ipc::PipeName(ipc::Endpoint::kRequest), 20)) {
    backoff_ms_ = 500;
    ipc::Writer hello(ipc::MsgType::kHello);
    wchar_t exe[MAX_PATH];
    const DWORD n = GetModuleFileNameW(nullptr, exe, MAX_PATH);
    hello.U32(ipc::kTagPid, GetCurrentProcessId()).U32(ipc::kTagTid, GetCurrentThreadId());
    hello.Str(ipc::kTagExe, std::wstring_view(exe, n));
    std::vector<uint8_t> resp;
    if (pipe_.Call(hello.Finish(), &resp, kOtherTimeoutMs)) {
      client_id_ = ipc::Reader(resp.data(), resp.size()).U32Or(ipc::kTagClient, 0);
      ++generation_;
      if (on_connected_) on_connected_();
      return true;
    }
  }
  next_attempt_ = now + backoff_ms_;
  backoff_ms_ = std::min(backoff_ms_ * 2, kMaxBackoffMs);
  MaybeStartHost();
  return false;
}

void HostClient::MaybeStartHost() {
  const ULONGLONG now = GetTickCount64();
  if (now < next_launch_) return;
  next_launch_ = now + kLaunchIntervalMs;
  if (!MayLaunchHost()) return;  // AppContainer / elevated: wait for the watchdog
  // CreateProcess off the UI thread; the host enforces a single instance. The
  // thread holds a module reference so the DLL cannot unload under it.
  HMODULE self = nullptr;
  if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS, reinterpret_cast<LPCWSTR>(&MayLaunchHost),
                          &self)) {
    return;
  }
  auto* path = new std::wstring(HostPath());
  HANDLE thread = CreateThread(
      nullptr, 64 * 1024,
      [](void* param) -> DWORD {
        auto* p = static_cast<std::wstring*>(param);
        STARTUPINFOW si = {sizeof(si)};
        PROCESS_INFORMATION pi = {};
        // --background: exit quietly if another TIP started the host first.
        std::wstring cmd = L"\"" + *p + L"\" --background";
        if (CreateProcessW(p->data(), cmd.data(), nullptr, nullptr, FALSE, DETACHED_PROCESS, nullptr, nullptr, &si,
                           &pi)) {
          CloseHandle(pi.hThread);
          CloseHandle(pi.hProcess);
        }
        delete p;
        FreeLibraryAndExitThread(g_module, 0);
      },
      path, STACK_SIZE_PARAM_IS_A_RESERVATION, nullptr);
  if (thread) {
    CloseHandle(thread);
  } else {
    delete path;
    FreeLibrary(self);
  }
}

bool HostClient::Exchange(ipc::Writer& request, std::vector<uint8_t>* response, DWORD timeout_ms) {
  if (!EnsureConnected()) return false;
  // Focus reports are answered without the engine: always sent, so the host
  // keeps knowing the focus while it is busy. Engine requests (keys, state)
  // fail fast for a moment after a timeout: keys pass through, and timeouts do
  // not pile up into a dropped connection (which the host sees as focus out).
  const ipc::MsgType type = request.type();
  const bool registry_only = type == ipc::MsgType::kFocusIn || type == ipc::MsgType::kFocusOut;
  if (!registry_only && GetTickCount64() < slow_until_) return false;
  if (pipe_.Call(request.Finish(), response, timeout_ms)) return true;
  if (pipe_.connected()) {
    // Host alive but slow: the connection is kept (the late answer is
    // skipped by the next call).
    slow_until_ = GetTickCount64() + 1000;
  } else if (pipe_.last_failure_timed_out()) {
    // Several timeouts in a row: stay away for a moment so a stuck host does
    // not delay every key by the full timeout. Do not start another one.
    next_attempt_ = GetTickCount64() + 2000;
  } else {
    // Broken pipe: the host is gone. Reconnect right away; EnsureConnected
    // starts a new host if nobody listens.
    next_attempt_ = 0;
  }
  if (!pipe_.connected() && on_disconnected_) on_disconnected_();
  return false;
}

std::optional<ipc::Result> HostClient::Call(ipc::Writer request, DWORD timeout_ms) {
  std::vector<uint8_t> resp;
  if (!Exchange(request, &resp, timeout_ms)) return std::nullopt;
  return ipc::Result::Decode(ipc::Reader(resp.data(), resp.size()));
}

bool HostClient::Notify(ipc::Writer request, DWORD timeout_ms) {
  std::vector<uint8_t> resp;
  if (!Exchange(request, &resp, timeout_ms)) return false;
  return ipc::Reader(resp.data(), resp.size()).type() == ipc::MsgType::kAck;
}

}  // namespace t9ime::tip
