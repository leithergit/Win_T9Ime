/*
 * T9Ctl.dll - control API of the T9Ime touch keyboard (x86 / x64).
 *
 * Plain C, __stdcall, no dependencies besides Windows. Every call talks to
 * T9Host (the per-user host process) over a named pipe and returns quickly;
 * T9Host is started when a call needs it (show, mode, position ...).
 * Thread safe; may be called from any thread.
 *
 * Keyboard visibility notifications: T9_RegisterVisibilityNotify(hwnd), then
 * handle the message returned by T9_GetVisibilityMessage() in that window:
 *     wParam = 1 visible / 0 hidden (also sent when the keyboard moves or is
 *     resized); call T9_GetKeyboardRect for the area it covers.
 */
#ifndef T9CTL_H_
#define T9CTL_H_

#include <windows.h>

#ifdef __cplusplus
extern "C" {
#endif

#define T9_API __stdcall

/* Keyboard layouts (T9_ShowKeyboard, T9_SetMode, T9_GetMode). */
#define T9_MODE_KEEP 0    /* T9_ShowKeyboard: keep the current layout */
#define T9_MODE_CHINESE 1 /* nine-key pinyin */
#define T9_MODE_ENGLISH 2 /* full QWERTY keyboard */
#define T9_MODE_NUMBER 3
#define T9_MODE_SYMBOL 4

/* TRUE if the T9Ime input method is registered for this process's architecture. */
BOOL T9_API T9_IsInstalled(void);

/* Switch the application owning `hwnd` (NULL: the foreground window) to T9Ime,
 * or away from it (to the first other input method of the user). For a window
 * of the calling thread the switch is immediate (COM is initialized on the
 * thread if needed and left initialized); other windows are switched
 * asynchronously. */
BOOL T9_API T9_Activate(HWND hwnd);
BOOL T9_API T9_Deactivate(HWND hwnd);

/* Show / hide the touch keyboard. `mode`: T9_MODE_*. Called on the thread
 * that owns the foreground window (e.g. from a button handler), showing also
 * switches that application to T9Ime. */
BOOL T9_API T9_ShowKeyboard(int mode);
BOOL T9_API T9_HideKeyboard(void);
BOOL T9_API T9_ToggleKeyboard(void);
BOOL T9_API T9_IsKeyboardVisible(void);

/* Layout of the keyboard (also while hidden). T9_GetMode returns 0 if T9Host is not running. */
BOOL T9_API T9_SetMode(int mode);
int T9_API T9_GetMode(void);

/* Position: T9_SetDock puts the keyboard at the bottom center of the screen;
 * T9_SetPosition moves its top-left corner (screen pixels). Both are remembered. */
BOOL T9_API T9_SetDock(void);
BOOL T9_API T9_SetPosition(int x, int y);
/* Screen rectangle of the keyboard (also when hidden). */
BOOL T9_API T9_GetKeyboardRect(RECT* rect);

/* Visibility notifications to `hwnd` (see above). Works for elevated
 * processes too: the message is allowed through UIPI for `hwnd`. */
BOOL T9_API T9_RegisterVisibilityNotify(HWND hwnd);
BOOL T9_API T9_UnregisterVisibilityNotify(HWND hwnd);
UINT T9_API T9_GetVisibilityMessage(void); /* RegisterWindowMessage("T9Ime.Visibility") */

#ifdef __cplusplus
}
#endif

#endif /* T9CTL_H_ */
