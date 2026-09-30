#pragma once
// Was the current focus change caused by touch or pen?
//   Windows 8+: GetCurrentInputMessageSource / GetCIMSSM for the message being
//               processed.
//   Windows 7 / fallback: thread-level hooks on the TIP's own thread (not
//   global hooks) remember the last press: WH_MOUSE sees each mouse message's
//   dwExtraInfo (touch / pen promoted to mouse carry the 0xFF515700
//   signature), WH_GETMESSAGE sees WM_POINTERDOWN (Windows 8+).

#include <windows.h>

#include <string>

namespace t9ime::tip {

class TouchTracker {
 public:
  TouchTracker() = default;
  TouchTracker(const TouchTracker&) = delete;
  TouchTracker& operator=(const TouchTracker&) = delete;
  ~TouchTracker() { Uninstall(); }

  // On the thread-manager thread. When a press lands on the window that
  // already has the keyboard focus (no focus change follows), `message` is
  // posted to `notify` so the focus can be reported again.
  void Install(HWND notify, UINT message);
  void Uninstall();
  bool FocusFromTouch() const;
  // What the decision was based on (for t9diag).
  std::wstring Describe() const;

 private:
  HHOOK message_hook_ = nullptr;
  HHOOK mouse_hook_ = nullptr;
};

}  // namespace t9ime::tip
