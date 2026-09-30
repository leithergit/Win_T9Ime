// t9diag: one-shot diagnosis for test devices. Prints the OS, token, UAC,
// TIP registration, running T9Host processes, and talks to the host over the
// request pipe exactly like the TIP does.
//   t9diag.exe            (run from the package folder; output to the console)
//   t9diag.exe > diag.txt
#include <windows.h>
#include <tlhelp32.h>

#include <cstdio>
#include <string>

#include "pipe.h"
#include "protocol.h"
#include "win_compat.h"

using namespace t9ime;

namespace {

constexpr wchar_t kClsid[] = L"{BB2F3BA4-3B7A-414A-A98F-08C100E5447E}";

void Line(const std::wstring& s) {
  // UTF-8 so the output survives redirection to a file.
  const int n = WideCharToMultiByte(CP_UTF8, 0, s.c_str(), static_cast<int>(s.size()), nullptr, 0, nullptr, nullptr);
  std::string u(n, '\0');
  WideCharToMultiByte(CP_UTF8, 0, s.c_str(), static_cast<int>(s.size()), u.data(), n, nullptr, nullptr);
  std::fputs((u + "\n").c_str(), stdout);
}

std::wstring Hex(DWORD v) {
  wchar_t buf[16];
  swprintf_s(buf, L"0x%X", v);
  return buf;
}

void Token() {
  HANDLE token = nullptr;
  if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token)) return;
  DWORD len = 0;
  TOKEN_ELEVATION_TYPE et = TokenElevationTypeDefault;
  GetTokenInformation(token, TokenElevationType, &et, sizeof(et), &len);
  BYTE buf[64];
  DWORD rid = 0;
  if (GetTokenInformation(token, TokenIntegrityLevel, buf, sizeof(buf), &len)) {
    auto* label = reinterpret_cast<TOKEN_MANDATORY_LABEL*>(buf);
    rid = *GetSidSubAuthority(label->Label.Sid, *GetSidSubAuthorityCount(label->Label.Sid) - 1);
  }
  CloseHandle(token);
  Line(L"token: integrity=" + Hex(rid) + L" elevation_type=" + std::to_wstring(et) +
       L"  (0x2000 medium, 0x3000 high; 1 default, 2 full, 3 limited)");
  DWORD lua = 1, size = sizeof(lua);
  RegGetValueW(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Policies\\System", L"EnableLUA",
               RRF_RT_REG_DWORD, nullptr, &lua, &size);
  Line(L"UAC EnableLUA=" + std::to_wstring(lua));
}

void Registration() {
  for (REGSAM view : {KEY_WOW64_64KEY, KEY_WOW64_32KEY}) {
    HKEY key = nullptr;
    const std::wstring path = std::wstring(L"CLSID\\") + kClsid + L"\\InprocServer32";
    std::wstring label = view == KEY_WOW64_64KEY ? L"64-bit" : L"32-bit";
    if (RegOpenKeyExW(HKEY_CLASSES_ROOT, path.c_str(), 0, KEY_READ | view, &key) != ERROR_SUCCESS) {
      Line(L"TIP " + label + L": not registered");
      continue;
    }
    wchar_t dll[MAX_PATH] = {};
    DWORD size = sizeof(dll);
    RegGetValueW(key, nullptr, nullptr, RRF_RT_REG_SZ, nullptr, dll, &size);
    RegCloseKey(key);
    const bool exists = GetFileAttributesW(dll) != INVALID_FILE_ATTRIBUTES;
    Line(L"TIP " + label + L": " + dll + (exists ? L"" : L"  (FILE MISSING)"));
  }
}

void Hosts() {
  HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
  if (snap == INVALID_HANDLE_VALUE) return;
  PROCESSENTRY32W pe = {sizeof(pe)};
  int count = 0;
  for (BOOL ok = Process32FirstW(snap, &pe); ok; ok = Process32NextW(snap, &pe)) {
    if (_wcsicmp(pe.szExeFile, L"T9Host.exe") != 0) continue;
    ++count;
    std::wstring path = L"?";
    if (HANDLE p = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pe.th32ProcessID)) {
      wchar_t buf[MAX_PATH];
      DWORD n = MAX_PATH;
      if (QueryFullProcessImageNameW(p, 0, buf, &n)) path.assign(buf, n);
      CloseHandle(p);
    }
    Line(L"T9Host running: pid " + std::to_wstring(pe.th32ProcessID) + L" " + path);
  }
  CloseHandle(snap);
  if (!count) Line(L"T9Host running: NO");
}

void Pipe() {
  const std::wstring name = ipc::PipeName(ipc::Endpoint::kRequest);
  Line(L"pipe: " + name);
  ipc::PipeClient c;
  if (!c.Connect(name, 1000)) {
    Line(L"pipe connect: FAILED, error " + std::to_wstring(GetLastError()));
    return;
  }
  Line(L"pipe connect: ok");
  std::vector<uint8_t> resp;
  auto call = [&](ipc::Writer w) -> std::optional<ipc::Result> {
    if (!c.Call(w.Finish(), &resp, 3000)) return std::nullopt;
    return ipc::Result::Decode(ipc::Reader(resp.data(), resp.size()));
  };
  auto state = call(ipc::Writer(ipc::MsgType::kQueryState));
  if (!state) {
    Line(L"query: FAILED (no answer in 3 s)");
    return;
  }
  Line(L"query: schema=" + state->schema + L" ascii_mode=" + std::to_wstring(state->ascii_mode));
  std::optional<ipc::Result> r;
  for (char ch : std::string("nihao")) {
    ipc::Writer w(ipc::MsgType::kKey);
    w.U32(ipc::kTagKeycode, static_cast<uint8_t>(ch)).U32(ipc::kTagMask, 0);
    r = call(std::move(w));
  }
  if (!r) {
    Line(L"keys: FAILED");
    return;
  }
  std::wstring cands;
  for (const auto& s : r->candidates) cands += s + L" ";
  Line(L"keys nihao: eaten=" + std::to_wstring(r->eaten) + L" preedit=[" + r->preedit + L"] candidates=" + cands);
  call(ipc::Writer(ipc::MsgType::kClearComposition));
}

}  // namespace

int wmain() {
  const auto& os = compat::Os();
#ifdef _WIN64
  const wchar_t* arch = L"x64";
#else
  const wchar_t* arch = L"x86";
#endif
  Line(L"T9Ime diagnostics (" + std::wstring(arch) + L" build)");
  Line(L"windows: " + std::to_wstring(os.major) + L"." + std::to_wstring(os.minor) + L"." + std::to_wstring(os.build));
  DWORD session = 0;
  ProcessIdToSessionId(GetCurrentProcessId(), &session);
  Line(L"session: " + std::to_wstring(session) + L"  user sid: " + ipc::CurrentUserSid());
  Token();
  Registration();
  Hosts();
  Pipe();
  return 0;
}
