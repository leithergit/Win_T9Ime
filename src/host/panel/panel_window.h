#pragma once
// The touch panel window: never activates, takes touch / pen / mouse input,
// drives the engine thread and outputs committed text.

#include <windows.h>

#include <map>
#include <string>
#include <vector>

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

  void Show();
  void Hide();
  void Toggle() { visible() ? Hide() : Show(); }
  // Back to the default place: bottom center of the current monitor.
  void Dock();
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
  void SetMode(Mode mode);
  bool composing() const { return !snapshot_.state.input.empty(); }
  std::vector<std::wstring> SideItems() const;
  void PlaceDefault();
  void LoadPlacement();
  void SavePlacement();

  EngineThread& engine_;
  PanelOptions options_;
  HWND hwnd_ = nullptr;
  UINT dpi_ = 96;
  PanelRenderer renderer_;
  Theme theme_ = Theme::Light();
  Layout layout_;

  EngineSnapshot snapshot_;
  Mode mode_ = Mode::kChinese;
  Mode text_mode_ = Mode::kChinese;  // mode to return to from numbers / symbols
  bool expanded_ = false;
  int symbol_category_ = 0;
  float candidate_scroll_ = 0, side_scroll_ = 0, grid_scroll_ = 0;
  float width_dip_ = 400, height_dip_ = 300;

  std::map<UINT32, Track> tracks_;
  UINT32 long_press_id_ = 0;  // pointer waiting for the long-press timer
};

}  // namespace t9ime::panel
