#pragma once
// The touch panel window: never activates, takes touch / pen / mouse input,
// drives the engine thread and outputs committed text.

#include <windows.h>

#include <functional>
#include <deque>
#include <map>
#include <mutex>
#include <string>
#include <vector>

#include "auto_show.h"
#include "engine_thread.h"
#include "panel_layout.h"
#include "panel_renderer.h"

namespace t9ime::panel {

enum class InputMode {
  kPointer,  // Windows 8+: WM_POINTER for touch/pen, mouse messages for the mouse
  kTouch,    // Windows 7: WM_TOUCH, mouse messages only from real mice
  kMouse,    // fallback: only mouse messages (touch arrives promoted to mouse)
};

struct PanelOptions {
  InputMode input = InputMode::kPointer;
  std::wstring settings_file;  // position / size persistence; empty = none
  std::wstring dump_layout;    // test hook: write element screen positions after each paint
  bool always_show = false;    // command line override of AutoShowSettings::always_show
  int theme = -1;              // -1 follow the system, 0 light, 1 dark (debugging)
};

class PanelWindow {
 public:
  explicit PanelWindow(EngineThread& engine) : engine_(engine) {}
  PanelWindow(const PanelWindow&) = delete;
  PanelWindow& operator=(const PanelWindow&) = delete;
  ~PanelWindow();

  bool Create(HINSTANCE instance, const PanelOptions& options);
  HWND hwnd() const noexcept { return hwnd_; }
  // Message posted by the engine thread when snapshots are ready.
  static constexpr UINT kEngineMessage = WM_APP + 1;
  static constexpr UINT kFocusMessage = WM_APP + 2;

  void Show();
  void Hide();
  void Toggle() { visible() ? Hide() : Show(); }
  // The panel belongs to the foreground application: it hides when another
  // application comes to the front. Without a binding (tray, --show) the next
  // foreground application becomes its owner.
  void BindToForeground();
  // UI thread: the foreground window changed (another process). Hides the
  // panel when its application goes to the background and shows it again
  // when that application comes back.
  void OnForegroundChanged(HWND foreground);
  // Text output: returns false when it could not be delivered (then the panel
  // falls back to SendInput).
  void SetDeliver(std::function<bool(const std::wstring&)> deliver) { deliver_ = std::move(deliver); }
  // Called when the user starts touching a key (e.g. to switch the target
  // application to T9Ime before text arrives).
  void SetOnInteraction(std::function<void()> f) { on_interaction_ = std::move(f); }
  // Back to the default place: bottom center of the current monitor.
  void Dock();
  // Control API: layout, position, placement changes (show / hide / move / resize).
  Mode mode() const { return mode_; }
  void SetModeByApplication(Mode mode) { SetMode(mode, true); }
  void MoveTo(int x, int y);
  void SetOnPlacement(std::function<void()> f) { on_placement_ = std::move(f); }

  // Focus changes reported by the TIPs; callable from any thread.
  void PostFocusEvent(FocusEvent e);
  AutoShowSettings& settings() { return settings_; }
  // Human-readable state for t9diag (any thread).
  std::wstring Describe();
  void SaveSettings();
  bool visible() const { return hwnd_ && IsWindowVisible(hwnd_); }

 private:
  struct Track {
    ElementKey key;
    Action action = Action::kNone;
    Region region = Region::kNone;
    std::string text;
    float x0 = 0, y0 = 0, lx = 0, ly = 0;  // DIPs
    POINT screen0{};
    RECT window0{};
    bool scrolling = false;
    bool long_fired = false;
    bool swipe_clear = false;
    bool inside = true;
    bool resizing = false;  // handle strip grip: resize instead of move
  };

  static LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);
  LRESULT HandleMessage(UINT msg, WPARAM wp, LPARAM lp);

  // Unified input, in client DIPs (screen point kept for window dragging).
  void PointerDown(UINT32 id, POINT screen);
  void PointerMove(UINT32 id, POINT screen);
  void PointerUp(UINT32 id, POINT screen);
  void PointerCancel(UINT32 id);
  void OnTimer(UINT_PTR id);
  bool HandleTouch(WPARAM wp, LPARAM lp);

  void Execute(Action action, int index, const std::string& text, const std::wstring& label);
  void LongPress(Track& t);
  void OnEngineSnapshots();
  void Relayout();
  void Paint();
  void DumpLayout();

  float Scale() const { return dpi_ / 96.f; }
  void ScreenToDip(POINT screen, float* x, float* y) const;
  void ApplyScroll(Region region, float delta);
  // `remember`: a user's choice of Chinese / English becomes the text mode for
  // ordinary fields; layouts picked for a field (InputScope) do not.
  void SetMode(Mode mode, bool remember = true);
  void TypeLetter(const std::wstring& text);
  bool composing() const { return !snapshot_.state.input.empty(); }
  std::vector<std::wstring> SideItems() const;
  void PlaceDefault();
  void LoadPlacement();
  void SavePlacement();

  void Output(const std::wstring& text);
  void OnFocusEvents();

  EngineThread& engine_;
  PanelOptions options_;
  std::function<bool(const std::wstring&)> deliver_;
  std::function<void()> on_interaction_;
  std::function<void()> on_placement_;
  unsigned pushed_ = 0, sent_ = 0;  // delivery statistics (test hook)

  AutoShowSettings settings_;
  bool auto_shown_ = false;
  DWORD owner_pid_ = 0;    // application the visible panel belongs to (0: not bound yet)
  DWORD restore_pid_ = 0;  // hidden because this application went to the background: show on its return
  std::string last_focus_;  // test hook: last focus event, for the layout dump  // shown by a focus change (then also hidden by one)
  std::mutex focus_mutex_;
  std::deque<FocusEvent> focus_events_;
  HWND hwnd_ = nullptr;
  UINT dpi_ = 96;
  PanelRenderer renderer_;
  Theme theme_ = Theme::Light();
  Layout layout_;

  EngineSnapshot snapshot_;
  Mode mode_ = Mode::kChinese;
  Mode text_mode_ = Mode::kChinese;  // Chinese / English: the text mode for ordinary fields
  Mode return_mode_ = Mode::kChinese; // mode to return to from numbers / symbols
  bool shift_ = false;               // QWERTY: upper case
  bool caps_lock_ = false;           // QWERTY: shift stays on
  ULONGLONG shift_tick_ = 0;         // last shift tap (double tap = caps lock)
  bool expanded_ = false;
  int symbol_category_ = 0;
  float candidate_scroll_ = 0, side_scroll_ = 0, grid_scroll_ = 0;
  float width_dip_ = 400, height_dip_ = 300;

  std::map<UINT32, Track> tracks_;
  UINT32 long_press_id_ = 0;  // pointer waiting for the long-press timer
};

}  // namespace t9ime::panel
