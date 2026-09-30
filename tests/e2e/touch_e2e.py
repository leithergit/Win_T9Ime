"""End-to-end touch behaviour with injected touch (InjectTouchInput, Windows 8+).

Real touch screens keep sending pointer updates while a finger is held still
(every ~8 ms), unlike a mouse; the injected holds below do the same.

1. Long press on the QWERTY keyboard: holding q types its small character 1;
   holding BackSpace deletes repeatedly.
Needs T9Tip.dll registered and TestHost.exe next to t9ctl.exe; exit code 77 = skipped.
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
