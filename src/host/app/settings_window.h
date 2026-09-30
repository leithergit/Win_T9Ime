#pragma once
// Settings window (SPEC §8, M6): standard Win32 controls, enlarged for touch.
// Three pages: 键盘 (pop-up behaviour, system keyboard, theme, size),
// 输入 (simplified / traditional, candidates per page, fuzzy pinyin),
// 词库 (export / import / clear the user dictionary, redeploy).

#include <windows.h>

#include <string>
#include <vector>

#include "auto_show.h"
#include "input_settings.h"

namespace t9ime {

struct SettingsValues {
  panel::AutoShowSettings auto_show;
  bool take_over_touch_keyboard = false;
  int theme = -1;  // -1 follow the system, 0 light, 1 dark
  int size = -1;   // 0 small, 1 medium, 2 large, -1 keep the current (custom) size
  InputSettings input;
};

// Implemented by the host; called on the UI thread.
class SettingsHost {
 public:
  virtual ~SettingsHost() = default;
  virtual SettingsValues CurrentSettings() = 0;
  // Applies and saves; a changed deployment starts a redeploy (asynchronous,
  // the result comes back through SettingsWindow::SetStatus).
  virtual void ApplySettings(const SettingsValues& values) = 0;
  virtual void Redeploy() = 0;
  virtual void ExportDictionary(const std::wstring& file) = 0;
  virtual void ImportDictionary(const std::wstring& file) = 0;
  virtual void ClearDictionary() = 0;
  virtual void DockKeyboard() = 0;
};

class SettingsWindow {
 public:
  explicit SettingsWindow(SettingsHost& host) : host_(host) {}
  SettingsWindow(const SettingsWindow&) = delete;
  SettingsWindow& operator=(const SettingsWindow&) = delete;
  ~SettingsWindow();

  // Creates the window, or brings it to the front with the current values.
  void Show(HINSTANCE instance);
  HWND hwnd() const { return hwnd_; }
  // Result of asynchronous work (redeploy, dictionary); `busy` disables the
  // buttons that start more of it.
  void SetStatus(const std::wstring& text, bool busy);

 private:
  static LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);
  LRESULT HandleMessage(UINT msg, WPARAM wp, LPARAM lp);
  void CreateControls();
  void Layout();
  void SelectPage(int page);
  void Load(const SettingsValues& v);
  SettingsValues Read() const;
  void Apply();
  void UpdateEnabled();
  HWND Add(int page, const wchar_t* cls, const wchar_t* text, DWORD style, int id);
  float Scale() const { return dpi_ / 96.f; }

  SettingsHost& host_;
  HWND hwnd_ = nullptr;
  HWND tabs_ = nullptr;
  HWND status_ = nullptr;
  HFONT font_ = nullptr;
  UINT dpi_ = 96;
  bool busy_ = false;
  int page_ = 0;
  struct Control {
    HWND hwnd;
    int page;  // -1: always visible
  };
  std::vector<Control> controls_;
  std::vector<HWND> fuzzy_;  // one check box per FuzzyOptions() entry
};

}  // namespace t9ime
