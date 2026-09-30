#pragma once
// Was the current focus change caused by touch or pen?
//   Windows 8+: GetCurrentInputMessageSource / GetCIMSSM for the message being
//               processed.
//   Fallback (Windows 7, asynchronous focus changes): a thread-level
//   WH_GETMESSAGE hook on the TIP's own thread (not a global hook) remembers
//   the last pointer press and whether it came from touch or pen (mouse
//   messages promoted from touch carry a signature in GetMessageExtraInfo).

#include <windows.h>

namespace t9ime::tip {

class TouchTracker {
 public:
  TouchTracker() = default;
  TouchTracker(const TouchTracker&) = delete;
  TouchTracker& operator=(const TouchTracker&) = delete;
  ~TouchTracker() { Uninstall(); }

  // On the thread-manager thread. When a touch / pen press lands on the window
  // that already has the keyboard focus (no focus change follows), `message`
  // is posted to `notify` so the focus can be reported again as touch.
  void Install(HWND notify, UINT message);
  void Uninstall();
  bool FocusFromTouch() const;

 private:
  HHOOK hook_ = nullptr;
};

}  // namespace t9ime::tip
