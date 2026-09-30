#pragma once
// Which TIP instance (client) owns the keyboard focus, and the events pipe to
// reach it. Shared by the request and events servers and the panel.

#include <windows.h>

#include <cstdint>
#include <functional>
#include <map>
#include <mutex>
#include <string>
#include <vector>

namespace t9ime::ipc {

struct FocusInfo {
  uint64_t client = 0;
  DWORD pid = 0;
  DWORD tid = 0;
  HWND hwnd = nullptr;
  std::wstring exe;             // file name, e.g. notepad.exe
  std::vector<uint32_t> scopes;  // InputScope values of the focused field
  bool touch = false;           // the focus change came from touch / pen
  bool read_only = false;
  bool focused = false;
  std::wstring touch_debug;     // what the TIP based `touch` on (diagnostics)
  bool no_context = false;      // no TSF document (password edit): use SendInput
};

class FocusRegistry {
 public:
  void Register(uint64_t client, DWORD pid, DWORD tid, std::wstring exe);
  void Unregister(uint64_t client);
  void SetEventPipe(uint64_t client, HANDLE pipe);
  void ClearEventPipe(uint64_t client, HANDLE pipe);

  void FocusIn(uint64_t client, HWND hwnd, std::vector<uint32_t> scopes, bool touch, bool read_only,
               std::wstring touch_debug = {}, bool no_context = false);
  void FocusOut(uint64_t client);

  // The focused client on the foreground window's thread, if any.
  bool Foreground(FocusInfo* out) const;
  // Pushes text to that client (committed through its edit session).
  // False if the foreground window has no connected TIP: use the fallback.
  bool PushCommit(const std::wstring& text);

  // Called (on the calling pipe thread) after every focus change with the
  // client's state (`focused` false = focus out).
  void SetListener(std::function<void(const FocusInfo&)> listener);
  // Human-readable state for t9diag.
  std::wstring Describe() const;

 private:
  struct Client {
    FocusInfo info;
    HANDLE events = nullptr;  // owned by the events server connection
  };
  mutable std::mutex mutex_;
  std::map<uint64_t, Client> clients_;
  std::function<void(const FocusInfo&)> listener_;
  void Notify(const FocusInfo& info);
};

}  // namespace t9ime::ipc
