#include "host_client.h"

#include <string>

#include "globals.h"

namespace t9ime::tip {

namespace {

constexpr DWORD kMaxBackoffMs = 5000;
constexpr DWORD kLaunchIntervalMs = 10000;

// Only a normal (medium integrity, not elevated, not AppContainer) process may
// start the host: anything else would give it the wrong token (SPEC §3).
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
    if (rid != SECURITY_MANDATORY_MEDIUM_RID) ok = false;
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
    if (pipe_.Call(hello.Finish(), &resp, kOtherTimeoutMs)) return true;
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
        if (CreateProcessW(p->data(), nullptr, nullptr, nullptr, FALSE, DETACHED_PROCESS, nullptr, nullptr, &si,
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
  if (pipe_.Call(request.Finish(), response, timeout_ms)) return true;
  // Timed out or broken: stay away for a moment so a hung host does not delay
  // every key by the full timeout. A broken pipe usually means the host died:
  // try to start it (rate limited; the host itself is single instance).
  next_attempt_ = GetTickCount64() + 2000;
  MaybeStartHost();
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
