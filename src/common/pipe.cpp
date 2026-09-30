#include "pipe.h"

#include <sddl.h>

#include "win_compat.h"

namespace t9ime::ipc {

namespace {

constexpr DWORD kBufferSize = 64 * 1024;
constexpr size_t kMaxRead = 1 << 20;

// Completes an overlapped operation started with `started`/`err` (its return
// value and GetLastError()). Returns the final error (0 = success) and the byte
// count; on timeout or `stop` the I/O is cancelled and WAIT_TIMEOUT returned.
DWORD Complete(HANDLE pipe, OVERLAPPED* ov, BOOL started, DWORD* transferred, DWORD timeout_ms, HANDLE stop) {
  DWORD err = started ? 0 : GetLastError();
  if (err == ERROR_IO_PENDING) {
    HANDLE handles[2] = {ov->hEvent, stop};
    if (WaitForMultipleObjects(stop ? 2 : 1, handles, FALSE, timeout_ms) != WAIT_OBJECT_0) {
      CancelIoEx(pipe, ov);
      GetOverlappedResult(pipe, ov, transferred, TRUE);  // wait for the cancellation
      return WAIT_TIMEOUT;
    }
  } else if (err != 0 && err != ERROR_MORE_DATA) {
    return err;
  }
  return GetOverlappedResult(pipe, ov, transferred, FALSE) ? 0 : GetLastError();
}

struct Event {
  HANDLE h = CreateEventW(nullptr, TRUE, FALSE, nullptr);
  ~Event() {
    if (h) CloseHandle(h);
  }
};

}  // namespace

std::wstring CurrentUserSid() {
  static const std::wstring sid = [] {
    std::wstring s;
    HANDLE token = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token)) return s;
    BYTE buf[256];
    DWORD len = 0;
    if (GetTokenInformation(token, TokenUser, buf, sizeof(buf), &len)) {
      LPWSTR str = nullptr;
      if (ConvertSidToStringSidW(reinterpret_cast<TOKEN_USER*>(buf)->User.Sid, &str)) {
        s = str;
        LocalFree(str);
      }
    }
    CloseHandle(token);
    return s;
  }();
  return sid;
}

std::wstring PipeName(Endpoint endpoint) {
  DWORD session = 0;
  ProcessIdToSessionId(GetCurrentProcessId(), &session);
  const wchar_t* suffix = endpoint == Endpoint::kRequest ? L"req" : endpoint == Endpoint::kEvents ? L"evt" : L"ctl";
  return L"\\\\.\\pipe\\T9Ime." + std::to_wstring(session) + L"." + CurrentUserSid() + L"." + suffix;
}

HANDLE CreatePipeInstance(const std::wstring& name, bool first) {
  // SYSTEM: all; user, AppContainers (Windows 8+): read/write; low integrity may write.
  std::wstring sddl = L"D:P(A;;GA;;;SY)(A;;GRGW;;;" + CurrentUserSid() + L")";
  if (compat::Os().major > 6 || (compat::Os().major == 6 && compat::Os().minor >= 2)) {
    sddl += L"(A;;GRGW;;;S-1-15-2-1)(A;;GRGW;;;S-1-15-2-2)";
  }
  sddl += L"S:(ML;;NW;;;LW)";
  PSECURITY_DESCRIPTOR sd = nullptr;
  if (!ConvertStringSecurityDescriptorToSecurityDescriptorW(sddl.c_str(), SDDL_REVISION_1, &sd, nullptr)) {
    return INVALID_HANDLE_VALUE;
  }
  SECURITY_ATTRIBUTES sa = {sizeof(sa), sd, FALSE};
  DWORD open_mode = PIPE_ACCESS_DUPLEX | FILE_FLAG_OVERLAPPED;
  if (first) open_mode |= FILE_FLAG_FIRST_PIPE_INSTANCE;
  HANDLE pipe = CreateNamedPipeW(name.c_str(), open_mode,
                                 PIPE_TYPE_MESSAGE | PIPE_READMODE_MESSAGE | PIPE_WAIT | PIPE_REJECT_REMOTE_CLIENTS,
                                 PIPE_UNLIMITED_INSTANCES, kBufferSize, kBufferSize, 0, &sa);
  LocalFree(sd);
  return pipe;
}

thread_local bool g_timed_out = false;  // set by ReadMessage / WriteMessage

bool ReadMessage(HANDLE pipe, std::vector<uint8_t>* out, DWORD timeout_ms, HANDLE stop) {
  g_timed_out = false;
  Event ev;
  out->clear();
  for (;;) {
    const size_t offset = out->size();
    out->resize(offset + kBufferSize);
    OVERLAPPED ov = {};
    ov.hEvent = ev.h;
    DWORD got = 0;
    const BOOL ok = ReadFile(pipe, out->data() + offset, kBufferSize, &got, &ov);
    const DWORD err = Complete(pipe, &ov, ok, &got, timeout_ms, stop);
    g_timed_out = err == WAIT_TIMEOUT;
    out->resize(offset + got);
    if (err == ERROR_MORE_DATA && out->size() < kMaxRead) continue;  // rest of the message
    if (err != 0 || out->empty()) {
      out->clear();
      return false;
    }
    return true;
  }
}

bool WriteMessage(HANDLE pipe, const std::vector<uint8_t>& data, DWORD timeout_ms, HANDLE stop) {
  Event ev;
  OVERLAPPED ov = {};
  ov.hEvent = ev.h;
  DWORD written = 0;
  const BOOL ok = WriteFile(pipe, data.data(), static_cast<DWORD>(data.size()), &written, &ov);
  const DWORD err = Complete(pipe, &ov, ok, &written, timeout_ms, stop);
  g_timed_out = err == WAIT_TIMEOUT;
  return err == 0 && written == data.size();
}

bool PipeClient::Connect(const std::wstring& name, DWORD timeout_ms) {
  Close();
  const ULONGLONG deadline = GetTickCount64() + timeout_ms;
  for (int attempt = 0; attempt < 50; ++attempt) {
    pipe_ = CreateFileW(name.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING,
                        FILE_FLAG_OVERLAPPED | SECURITY_SQOS_PRESENT | SECURITY_IDENTIFICATION, nullptr);
    if (pipe_ != INVALID_HANDLE_VALUE) break;
    const DWORD err = GetLastError();
    if (err == ERROR_PIPE_BUSY) {
      if (!WaitNamedPipeW(name.c_str(), timeout_ms)) return false;
      continue;
    }
    // Not found: no server, or the server is between two instances (it just
    // accepted another client). Retry briefly within the timeout.
    if (err != ERROR_FILE_NOT_FOUND || GetTickCount64() + 10 > deadline) return false;
    Sleep(10);
  }
  if (pipe_ == INVALID_HANDLE_VALUE) return false;
  DWORD mode = PIPE_READMODE_MESSAGE;
  if (!SetNamedPipeHandleState(pipe_, &mode, nullptr, nullptr)) {
    Close();
    return false;
  }
  return true;
}

bool PipeClient::Call(const std::vector<uint8_t>& request, std::vector<uint8_t>* response, DWORD timeout_ms) {
  if (!connected()) return false;
  timed_out_ = false;
  if (++seq_ == 0) ++seq_;
  const uint32_t seq = seq_;
  std::vector<uint8_t> message = request;
  if (message.size() >= 12) {  // header: magic u32 | version u16 | type u16 | seq u32 (little endian)
    for (int i = 0; i < 4; ++i) message[8 + i] = static_cast<uint8_t>(seq >> (8 * i));
  }
  const ULONGLONG start = GetTickCount64();
  if (!WriteMessage(pipe_, message, timeout_ms)) {
    timed_out_ = g_timed_out;
    Close();
    return false;
  }
  for (;;) {
    const ULONGLONG spent = GetTickCount64() - start;
    const DWORD left = spent >= timeout_ms ? 1 : static_cast<DWORD>(timeout_ms - spent);
    if (!ReadMessage(pipe_, response, left)) {
      timed_out_ = g_timed_out;
      if (!timed_out_ || ++timeouts_ >= kMaxTimeouts) Close();
      return false;
    }
    const uint32_t got = response->size() >= 12 ? static_cast<uint32_t>((*response)[8]) |
                                                      static_cast<uint32_t>((*response)[9]) << 8 |
                                                      static_cast<uint32_t>((*response)[10]) << 16 |
                                                      static_cast<uint32_t>((*response)[11]) << 24
                                                : seq;
    if (got == seq) break;
    // A late answer to an earlier, timed-out call: skip it.
  }
  timeouts_ = 0;
  return true;
}

bool PipeClient::Send(const std::vector<uint8_t>& message, DWORD timeout_ms) {
  if (!connected()) return false;
  if (WriteMessage(pipe_, message, timeout_ms)) return true;
  Close();
  return false;
}

bool PipeClient::Receive(std::vector<uint8_t>* message, DWORD timeout_ms, HANDLE stop) {
  if (!connected()) return false;
  if (ReadMessage(pipe_, message, timeout_ms, stop)) return true;
  Close();
  return false;
}

void PipeClient::Close() {
  timeouts_ = 0;
  if (pipe_ != INVALID_HANDLE_VALUE) {
    CloseHandle(pipe_);
    pipe_ = INVALID_HANDLE_VALUE;
  }
}

}  // namespace t9ime::ipc
