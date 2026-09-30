"""End-to-end touch behaviour with injected touch (InjectTouchInput, Windows 8+).

Real touch screens keep sending pointer updates while a finger is held still
(every ~8 ms), unlike a mouse; the injected holds below do the same.

1. Long press on the QWERTY keyboard: holding q types its small character 1;
   holding BackSpace deletes repeatedly.
2. TestHost's buttons tapped with a finger: "隐藏键盘" hides (the handler's
   SetFocus back to the text box is not a touch on the field).
4. "切换" shows and hides; "数字" / "符号" keep their layout.
   A finger on the text box itself still pops the keyboard up.
3. Tapping another application's title bar (TestHost C#) hides the keyboard
   and it stays hidden; that application's "隐藏键盘" works too.
Needs T9Tip.dll registered and TestHost.exe next to t9ctl.exe (TestHost.CS.exe
for 3); exit code 77 = skipped.
"""
import argparse
import ctypes
import subprocess
import sys
import time
from ctypes import wintypes
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))
import autoshow_e2e as ae  # noqa: E402
import panel_e2e as pe  # noqa: E402  (per-monitor DPI aware)
import tip_e2e as te  # noqa: E402

user32 = pe.user32
EM_SETSEL = 0x00B1
WM_SETTEXT = 0x000C
UPDATE_MS = 8  # a real touch screen's update interval while the finger is held


def touch_hold(x: int, y: int, seconds: float) -> None:
    user32.InitializeTouchInjection(1, 3)  # TOUCH_FEEDBACK_NONE
    info = ae.POINTER_TOUCH_INFO()
    info.pointerInfo.pointerType = ae.PT_TOUCH
    info.pointerInfo.ptPixelLocation = wintypes.POINT(x, y)
    info.rcContact = wintypes.RECT(x - 2, y - 2, x + 2, y + 2)
    contact = ae.POINTER_FLAG_INRANGE | ae.POINTER_FLAG_INCONTACT
    info.pointerInfo.pointerFlags = ae.POINTER_FLAG_DOWN | contact
    assert user32.InjectTouchInput(1, ctypes.byref(info)), 'InjectTouchInput failed'
    end = time.time() + seconds
    while time.time() < end:
        time.sleep(UPDATE_MS / 1000)
        info.pointerInfo.pointerFlags = ae.POINTER_FLAG_UPDATE | contact
        assert user32.InjectTouchInput(1, ctypes.byref(info)), 'InjectTouchInput failed'
    info.pointerInfo.pointerFlags = ae.POINTER_FLAG_UP
    assert user32.InjectTouchInput(1, ctypes.byref(info)), 'InjectTouchInput failed'


def find_child(parent, text):
    """A descendant window with this text (WinForms nests its buttons)."""
    found = []

    def visit(h, _):
        if pe.window_text(h) != text:
            return True
        found.append(h)
        return False  # stop

    user32.EnumChildWindows(parent, ctypes.WINFUNCTYPE(wintypes.BOOL, wintypes.HWND, wintypes.LPARAM)(visit), 0)
    return found[0] if found else None


def title_bar(hwnd):
    """A point on the caption, left of its buttons."""
    r = wintypes.RECT()
    user32.GetWindowRect(hwnd, ctypes.byref(r))
    client = wintypes.POINT(0, 0)
    user32.ClientToScreen(hwnd, ctypes.byref(client))
    return r.left + (r.right - r.left) // 3, (r.top + client.y) // 2


def center(hwnd):
    r = wintypes.RECT()
    user32.GetWindowRect(hwnd, ctypes.byref(r))
    return (r.left + r.right) // 2, (r.top + r.bottom) // 2


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument('--host', required=True)
    ap.add_argument('--ctl', required=True)
    ap.add_argument('--work', required=True, type=Path)
    args = ap.parse_args()
    if not te.registered():
        print('SKIP: T9Tip.dll is not registered')
        return 77
    if not hasattr(user32, 'InjectTouchInput'):
        print('SKIP: InjectTouchInput unavailable (Windows 7)')
        return 77
    bin_dir = Path(args.ctl).resolve().parent
    args.work.mkdir(parents=True, exist_ok=True)
    layout = args.work / 'touch_layout.json'
    layout.unlink(missing_ok=True)
    ini = args.work / 'touch_panel.ini'
    ini.unlink(missing_ok=True)
    failures = []

    def check(cond, msg):
        print(('PASS ' if cond else 'FAIL ') + msg)
        if not cond:
            failures.append(msg)

    def visible():
        return bool((panel.read() or {}).get('visible'))

    def mode():
        r = subprocess.run([str(Path(args.ctl).resolve()), 'status'], capture_output=True, text=True, timeout=30)
        return dict(kv.split('=', 1) for kv in r.stdout.split()).get('mode')

    def settles(pred, what, seconds=1.5):
        """pred() becomes true and stays true (nothing reverts it afterwards)."""
        try:
            pe.wait_for(pred, timeout=5, what=what)
        except AssertionError:
            return False
        end = time.time() + seconds
        while time.time() < end:
            if not pred():
                return False
            time.sleep(0.05)
        return True

    host = pe.start_host(args.host, '--user', str(args.work / 'touch_user'), '--settings', str(ini),
                         '--dump-layout', str(layout))
    testhost = None
    try:
        panel = pe.Panel(layout)
        pe.wait_for(panel.read, what='layout dump')

        testhost = subprocess.Popen([str(bin_dir / 'TestHost.exe')])
        th = pe.wait_for(lambda: user32.FindWindowW(None, 'T9Ime TestHost (C++)'), what='TestHost')
        edit = pe.wait_for(lambda: user32.FindWindowExW(th, None, 'Edit', None), what='TestHost edit')
        time.sleep(0.5)
        pe.click(*center(edit))
        pe.wait_for(lambda: user32.GetForegroundWindow() == th, what='TestHost in front')

        def button(name):
            return user32.FindWindowExW(th, None, 'Button', name)

        def set_text(text):
            user32.SendMessageW(edit, WM_SETTEXT, 0, ctypes.c_wchar_p(text))
            user32.SendMessageW(edit, EM_SETSEL, len(text), len(text))

        # 1. Long press (set up with the mouse: only the holds are under test).
        pe.click(*center(button('英文')))
        pe.wait_for(visible, timeout=5, what='panel shown by TestHost')
        pe.wait_for(lambda: panel.has(pe.LETTER, label='q'), what='QWERTY keyboard')
        time.sleep(1.0)  # TestHost switched to T9Ime, engine warmed up
        set_text('')
        q = panel.find(pe.LETTER, label='q')
        touch_hold(q['x'], q['y'], 1.0)
        try:
            pe.wait_for(lambda: pe.window_text(edit) == '1', timeout=3, what='long press')
        except AssertionError:
            pass
        check(pe.window_text(edit) == '1', f'long press q types 1 (got {pe.window_text(edit)!r})')

        start = 'abcdefghijklmnopqrstuvwxyz'
        set_text(start)
        bs = panel.find(pe.BACKSPACE)
        touch_hold(bs['x'], bs['y'], 1.2)
        time.sleep(0.8)
        left = pe.window_text(edit)
        check(start.startswith(left) and len(left) <= len(start) - 4,
              f'holding BackSpace repeats (left {left!r})')

        # 2 and 4: TestHost's buttons with a finger.
        def tap(name):
            ae.touch_tap(*center(button(name)))

        tap('隐藏键盘')
        check(settles(lambda: not visible(), 'hidden'), '2: touch "隐藏键盘" hides the keyboard')
        tap('显示键盘')
        check(settles(visible, 'shown'), '2: touch "显示键盘" shows the keyboard')
        tap('隐藏键盘')
        check(settles(lambda: not visible(), 'hidden'), '2: touch "隐藏键盘" again hides it')
        tap('切换')
        check(settles(visible, 'shown'), '4: touch "切换" shows the keyboard')
        tap('切换')
        check(settles(lambda: not visible(), 'hidden'), '4: touch "切换" again hides it')
        tap('数字')
        check(settles(lambda: visible() and mode() == 'number', 'number layout'),
              f'4: touch "数字" shows the number layout (mode {mode()})')
        tap('符号')
        check(settles(lambda: visible() and mode() == 'symbol', 'symbol layout'),
              f'4: touch "符号" shows the symbol layout (mode {mode()})')
        tap('英文')
        check(settles(lambda: visible() and mode() == 'english', 'english layout'),
              f'4: touch "英文" shows the English keyboard (mode {mode()})')
        pe.click(*center(button('隐藏键盘')))
        pe.wait_for(lambda: not visible(), timeout=5, what='hidden')
        ae.touch_tap(*center(edit))
        check(settles(visible, 'shown by touching the field'), 'a finger on the text box still pops it up')

        # 3: another application's title bar.
        cs_exe = bin_dir / 'TestHost.CS.exe'
        if not cs_exe.exists():
            print('SKIP 3: TestHost.CS.exe not built')
        else:
            cs_proc = subprocess.Popen([str(cs_exe)])
            try:
                cs = pe.wait_for(lambda: user32.FindWindowW(None, 'T9Ime TestHost (C#)'), what='TestHost C#')
                time.sleep(1.0)
                # Side by side at the top of the screen, clear of the docked panel.
                width = user32.GetSystemMetrics(0)
                user32.SetWindowPos(th, None, 0, 0, width // 2 - 10, 360, 0x0014)  # NOZORDER|NOACTIVATE
                user32.SetWindowPos(cs, None, width // 2 + 10, 0, width // 2 - 10, 360, 0x0014)
                time.sleep(0.5)
                pe.click(*center(edit))
                pe.wait_for(lambda: user32.GetForegroundWindow() == th, what='TestHost C++ in front')
                time.sleep(0.5)
                tap('显示键盘')
                pe.wait_for(visible, timeout=5, what='shown by TestHost C++')
                time.sleep(0.5)
                ae.touch_tap(*title_bar(cs))
                pe.wait_for(lambda: user32.GetForegroundWindow() == cs, what='TestHost C# in front')
                check(settles(lambda: not visible(), 'hidden', 2.0),
                      '3: touching the title bar of the C# window hides the keyboard for good')
                cs_hide = find_child(cs, '隐藏键盘')
                cs_show = find_child(cs, '显示键盘')
                if cs_hide and cs_show:
                    ae.touch_tap(*center(cs_show))
                    check(settles(visible, 'shown'), '3: touch "显示键盘" in the C# sample shows it')
                    ae.touch_tap(*center(cs_hide))
                    check(settles(lambda: not visible(), 'hidden'), '3: touch "隐藏键盘" in the C# sample hides it')
                else:
                    check(False, 'C# sample buttons found')
            finally:
                user32.PostMessageW(user32.FindWindowW(None, 'T9Ime TestHost (C#)'), pe.WM_CLOSE, 0, 0)
                try:
                    cs_proc.wait(5)
                except subprocess.TimeoutExpired:
                    cs_proc.kill()
    finally:
        if testhost:
            user32.PostMessageW(user32.FindWindowW(None, 'T9Ime TestHost (C++)'), pe.WM_CLOSE, 0, 0)
            try:
                testhost.wait(5)
            except subprocess.TimeoutExpired:
                testhost.kill()
        host.terminate()

    print('ALL PASSED' if not failures else f'{len(failures)} FAILED')
    return 1 if failures else 0


if __name__ == '__main__':
    sys.exit(main())
