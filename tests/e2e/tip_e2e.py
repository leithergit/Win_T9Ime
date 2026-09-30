"""End-to-end TIP test (M3): physical keyboard full pinyin through TSF.

Needs T9Tip.dll registered (tools/dev_register.ps1) and an interactive desktop.
Starts T9Host (unless one is running) and test_target --activate-tip (a Win32
Edit, i.e. the IMM32/CUAS path), types with SendInput and checks the text.
Exit code 77 = skipped (TIP not registered).
"""
import argparse
import ctypes
import subprocess
import sys
import time
import winreg
from ctypes import wintypes
from pathlib import Path

sys.stdout.reconfigure(encoding='utf-8')
user32 = ctypes.WinDLL('user32', use_last_error=True)
try:
    user32.SetProcessDpiAwarenessContext(ctypes.c_void_p(-4))  # physical pixels, like panel_e2e.click
except AttributeError:
    user32.SetProcessDPIAware()
CLSID = '{BB2F3BA4-3B7A-414A-A98F-08C100E5447E}'
WM_GETTEXT, WM_GETTEXTLENGTH, WM_CLOSE, WM_SETTEXT = 0x0D, 0x0E, 0x10, 0x0C
INPUT_KEYBOARD, KEYEVENTF_KEYUP = 1, 0x2
VK = {'shift': 0x10, 'enter': 0x0D, 'esc': 0x1B, 'space': 0x20, 'back': 0x08, 'minus': 0xBD, 'equal': 0xBB}


class KEYBDINPUT(ctypes.Structure):
    _fields_ = [('wVk', wintypes.WORD), ('wScan', wintypes.WORD), ('dwFlags', wintypes.DWORD),
                ('time', wintypes.DWORD), ('dwExtraInfo', ctypes.c_size_t)]


class INPUT(ctypes.Structure):
    class _U(ctypes.Union):
        _fields_ = [('ki', KEYBDINPUT), ('pad', ctypes.c_byte * 32)]
    _anonymous_ = ('u',)
    _fields_ = [('type', wintypes.DWORD), ('u', _U)]


def key(vk: int, up: bool = False) -> None:
    inp = INPUT(type=INPUT_KEYBOARD)
    inp.ki = KEYBDINPUT(vk, user32.MapVirtualKeyW(vk, 0), KEYEVENTF_KEYUP if up else 0, 0, 0)
    user32.SendInput(1, ctypes.byref(inp), ctypes.sizeof(INPUT))


def tap(vk: int) -> None:
    key(vk)
    time.sleep(0.02)
    key(vk, True)
    time.sleep(0.06)


def type_keys(text: str) -> None:
    for ch in text:
        tap(ord(ch.upper()) if ch.isalpha() else ord(ch))


def window_text(hwnd) -> str:
    n = user32.SendMessageW(hwnd, WM_GETTEXTLENGTH, 0, 0)
    buf = ctypes.create_unicode_buffer(n + 1)
    user32.SendMessageW(hwnd, WM_GETTEXT, n + 1, buf)
    return buf.value


def wait_for(pred, timeout=10.0, what='condition'):
    end = time.time() + timeout
    while time.time() < end:
        v = pred()
        if v:
            return v
        time.sleep(0.05)
    raise AssertionError(f'timeout waiting for {what}')


def registered() -> bool:
    try:
        winreg.OpenKey(winreg.HKEY_CLASSES_ROOT, rf'CLSID\{CLSID}\InprocServer32').Close()
        return True
    except OSError:
        return False


def click(x: int, y: int) -> None:
    sys.path.insert(0, str(Path(__file__).parent))
    import panel_e2e
    panel_e2e.click(x, y)


def candidate_window(pid: int):
    found = []

    @ctypes.WINFUNCTYPE(wintypes.BOOL, wintypes.HWND, wintypes.LPARAM)
    def cb(hwnd, _):
        cls = ctypes.create_unicode_buffer(64)
        user32.GetClassNameW(hwnd, cls, 64)
        owner = wintypes.DWORD()
        user32.GetWindowThreadProcessId(hwnd, ctypes.byref(owner))
        if cls.value == 'T9Ime.TipCandidate' and owner.value == pid and user32.IsWindowVisible(hwnd):
            found.append(hwnd)
        return True

    user32.EnumWindows(cb, 0)
    return found[0] if found else None


def candidate_window_visible(pid: int) -> bool:
    found = []

    @ctypes.WINFUNCTYPE(wintypes.BOOL, wintypes.HWND, wintypes.LPARAM)
    def cb(hwnd, _):
        cls = ctypes.create_unicode_buffer(64)
        user32.GetClassNameW(hwnd, cls, 64)
        owner = wintypes.DWORD()
        user32.GetWindowThreadProcessId(hwnd, ctypes.byref(owner))
        if cls.value == 'T9Ime.TipCandidate' and owner.value == pid and user32.IsWindowVisible(hwnd):
            found.append(hwnd)
        return True

    user32.EnumWindows(cb, 0)
    return bool(found)


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument('--host', required=True)
    ap.add_argument('--target', required=True)
    args = ap.parse_args()
    if not registered():
        print('SKIP: T9Tip.dll is not registered (run tools/dev_register.ps1)')
        return 77

    caps_was_on = user32.GetKeyState(0x14) & 1
    if caps_was_on:
        tap(0x14)  # Caps Lock would make librime pass letters through
    host = subprocess.Popen([str(Path(args.host).resolve())])  # exits at once if one is running
    time.sleep(1.0)
    target = subprocess.Popen([str(Path(args.target).resolve()), '--activate-tip'])
    failures = []

    def check(cond, msg):
        print(('PASS ' if cond else 'FAIL ') + msg)
        if not cond:
            failures.append(msg)

    try:
        hwnd = wait_for(lambda: user32.FindWindowW('T9Ime.TestTarget', None), what='test target')
        edit = user32.FindWindowExW(hwnd, None, 'Edit', None)
        wait_for(lambda: '[tip' in window_text(hwnd), what='profile activation')
        check(window_text(hwnd).endswith('[tip]'), f'profile activated ({window_text(hwnd)})')
        # Clicking is reliable where SetForegroundWindow hits the foreground lock.
        er = wintypes.RECT()
        user32.GetWindowRect(edit, ctypes.byref(er))
        click((er.left + er.right) // 2, (er.top + er.bottom) // 2)
        wait_for(lambda: user32.GetForegroundWindow() == hwnd, what='foreground')
        time.sleep(0.5)

        def clear():
            user32.SendMessageW(edit, WM_SETTEXT, 0, ctypes.c_wchar_p(''))

        type_keys('nihao')
        time.sleep(0.3)
        check(candidate_window_visible(target.pid), 'candidate window shows while composing')
        tap(VK['space'])
        time.sleep(0.3)
        check(window_text(edit) == '你好', f'nihao + space -> 你好 (got {window_text(edit)!r})')
        check(not candidate_window_visible(target.pid), 'candidate window hides after commit')

        # Mouse click on the candidate window selects without taking focus.
        clear()
        type_keys('shi')
        time.sleep(0.3)
        cand = candidate_window(target.pid)
        check(bool(cand), 'candidate window for mouse selection')
        if cand:
            r = wintypes.RECT()
            user32.GetWindowRect(cand, ctypes.byref(r))
            click(r.left + (r.right - r.left) * 3 // 4, (r.top + r.bottom) // 2)
            time.sleep(0.4)
            text = window_text(edit)
            check(len(text) == 1 and text != 's', f'clicking a candidate commits it (got {text!r})')
            check(user32.GetForegroundWindow() == hwnd, 'candidate click keeps the target in the foreground')

        clear()
        type_keys('zhongguo')
        tap(ord('1'))
        time.sleep(0.3)
        check(window_text(edit) == '中国', f'zhongguo + 1 -> 中国 (got {window_text(edit)!r})')

        clear()
        type_keys('ceshi')
        tap(VK['enter'])
        time.sleep(0.3)
        check(window_text(edit) == 'ceshi', f'enter commits the raw input (got {window_text(edit)!r})')

        clear()
        type_keys('shurufa')
        tap(VK['esc'])
        time.sleep(0.3)
        check(window_text(edit) == '', f'escape clears the composition (got {window_text(edit)!r})')

        # Shift alone toggles English; letters then go straight to the app.
        clear()
        tap(VK['shift'])
        time.sleep(0.2)
        type_keys('abc')
        time.sleep(0.2)
        check(window_text(edit) == 'abc', f'shift toggles to English (got {window_text(edit)!r})')
        tap(VK['shift'])
        time.sleep(0.2)
        type_keys('wo')
        tap(VK['space'])
        time.sleep(0.3)
        check(window_text(edit) == 'abc我', f'shift toggles back to Chinese (got {window_text(edit)!r})')

        # Ctrl shortcuts pass through (Ctrl+A selects all, then Backspace deletes).
        key(0x11)
        tap(ord('A'))
        key(0x11, True)
        tap(VK['back'])
        time.sleep(0.2)
        check(window_text(edit) == '', f'ctrl shortcuts reach the app (got {window_text(edit)!r})')

        # Host gone: keys pass through quickly, the app keeps working.
        subprocess.run(['taskkill', '/im', 'T9Host.exe', '/f'], capture_output=True)
        time.sleep(0.5)
        start = time.time()
        type_keys('xyz')
        spent = time.time() - start
        time.sleep(0.3)
        check(window_text(edit) == 'xyz', f'without host, keys pass through (got {window_text(edit)!r})')
        check(spent < 2.0, f'no hang without host ({spent:.2f}s for 3 keys)')

        # The TIP relaunches the host after noticing it is gone; typing works again.
        clear()
        time.sleep(2.5)  # the TIP backs off 2 s after a failed call
        type_keys('a')
        wait_for(lambda: subprocess.run(['tasklist', '/fi', 'imagename eq T9Host.exe'], capture_output=True,
                                        text=True).stdout.count('T9Host.exe') > 0, timeout=20, what='host relaunch')
        time.sleep(3.0)
        tap(VK['esc'])  # the trigger key may already have started a composition
        time.sleep(0.2)
        clear()
        type_keys('nihao')
        tap(VK['space'])
        time.sleep(0.3)
        check(window_text(edit) == '你好', f'host relaunched by the TIP (got {window_text(edit)!r})')
    finally:
        user32.PostMessageW(user32.FindWindowW('T9Ime.TestTarget', None), WM_CLOSE, 0, 0)
        try:
            target.wait(5)
        except subprocess.TimeoutExpired:
            target.kill()
        host.poll()
        if caps_was_on and not user32.GetKeyState(0x14) & 1:
            tap(0x14)

    print('ALL PASSED' if not failures else f'{len(failures)} FAILED')
    return 1 if failures else 0


if __name__ == '__main__':
    sys.exit(main())
