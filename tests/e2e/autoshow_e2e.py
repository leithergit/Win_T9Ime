"""End-to-end automatic show / hide (M4).

1. --always-show: clicking a text field pops the panel up in the text layout,
   a number field (InputScope IS_NUMBER) switches it to the number layout, a
   password field hides it.
2. Default settings: a mouse click does not pop the panel up, a touch tap
   (InjectTouchInput, Windows 8+) does.
Needs T9Tip.dll registered; exit code 77 = skipped.
"""
import argparse
import ctypes
import subprocess
import sys
import time
from ctypes import wintypes
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))
import panel_e2e as pe  # noqa: E402  (per-monitor DPI aware)
import tip_e2e as te  # noqa: E402

user32 = pe.user32
WM_APP = 0x8000

# --- touch injection (Windows 8+) ---
PT_TOUCH = 2
POINTER_FLAG_INRANGE, POINTER_FLAG_INCONTACT = 0x2, 0x4
POINTER_FLAG_DOWN, POINTER_FLAG_UPDATE, POINTER_FLAG_UP = 0x10000, 0x20000, 0x40000


class POINTER_INFO(ctypes.Structure):
    _fields_ = [('pointerType', wintypes.DWORD), ('pointerId', ctypes.c_uint32), ('frameId', ctypes.c_uint32),
                ('pointerFlags', wintypes.DWORD), ('sourceDevice', wintypes.HANDLE), ('hwndTarget', wintypes.HWND),
                ('ptPixelLocation', wintypes.POINT), ('ptHimetricLocation', wintypes.POINT),
                ('ptPixelLocationRaw', wintypes.POINT), ('ptHimetricLocationRaw', wintypes.POINT),
                ('dwTime', wintypes.DWORD), ('historyCount', ctypes.c_uint32), ('InputData', ctypes.c_int32),
                ('dwKeyStates', wintypes.DWORD), ('PerformanceCount', ctypes.c_uint64),
                ('ButtonChangeType', ctypes.c_int)]


class POINTER_TOUCH_INFO(ctypes.Structure):
    _fields_ = [('pointerInfo', POINTER_INFO), ('touchFlags', wintypes.DWORD), ('touchMask', wintypes.DWORD),
                ('rcContact', wintypes.RECT), ('rcContactRaw', wintypes.RECT), ('orientation', ctypes.c_uint32),
                ('pressure', ctypes.c_uint32)]


def touch_tap(x: int, y: int) -> bool:
    if not hasattr(user32, 'InjectTouchInput'):
        return False
    user32.InitializeTouchInjection(1, 3)  # TOUCH_FEEDBACK_NONE
    info = POINTER_TOUCH_INFO()
    info.pointerInfo.pointerType = PT_TOUCH
    info.pointerInfo.pointerId = 0
    info.pointerInfo.ptPixelLocation = wintypes.POINT(x, y)
    info.rcContact = wintypes.RECT(x - 2, y - 2, x + 2, y + 2)
    for flags in (POINTER_FLAG_DOWN | POINTER_FLAG_INRANGE | POINTER_FLAG_INCONTACT, POINTER_FLAG_UP):
        info.pointerInfo.pointerFlags = flags
        if not user32.InjectTouchInput(1, ctypes.byref(info)):
            return False
        time.sleep(0.05)
    return True


def center(hwnd):
    r = wintypes.RECT()
    user32.GetWindowRect(hwnd, ctypes.byref(r))
    return (r.left + r.right) // 2, (r.top + r.bottom) // 2


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument('--host', required=True)
    ap.add_argument('--target', required=True)
    ap.add_argument('--work', required=True, type=Path)
    args = ap.parse_args()
    if not te.registered():
        print('SKIP: T9Tip.dll is not registered')
        return 77
    args.work.mkdir(parents=True, exist_ok=True)
    failures = []

    def check(cond, msg):
        print(('PASS ' if cond else 'FAIL ') + msg)
        if not cond:
            failures.append(msg)

    def run(always: bool, scenario, target_arg='--activate-tip'):
        layout = args.work / 'auto_layout.json'
        layout.unlink(missing_ok=True)
        ini = args.work / 'auto_panel.ini'
        ini.unlink(missing_ok=True)
        subprocess.run(['taskkill', '/im', 'T9Host.exe', '/f'], capture_output=True)
        time.sleep(0.5)
        cmd = ['--user', str(args.work / 'auto_user'), '--settings', str(ini),
               '--dump-layout', str(layout)]
        if always:
            cmd.append('--always-show')
        host = pe.start_host(args.host, *cmd)
        target = subprocess.Popen([str(Path(args.target).resolve()), target_arg])
        try:
            panel = pe.Panel(layout)
            pe.wait_for(panel.read, what='layout dump')
            hwnd = pe.wait_for(lambda: user32.FindWindowW('T9Ime.TestTarget', None), what='test target')
            tag = '[tip' if target_arg == '--activate-tip' else '[en'
            pe.wait_for(lambda: tag in pe.window_text(hwnd), what='profile activation')
            edits = []
            child = None
            while True:
                child = user32.FindWindowExW(hwnd, child, 'Edit', None)
                if not child:
                    break
                edits.append(child)
            scenario(panel, hwnd, edits)
        finally:
            user32.PostMessageW(user32.FindWindowW('T9Ime.TestTarget', None), pe.WM_CLOSE, 0, 0)
            try:
                target.wait(5)
            except subprocess.TimeoutExpired:
                target.kill()
            host.terminate()

    def visible(panel):
        return (panel.read() or {}).get('visible', False)

    def has(panel, action, label=None):
        return any(e['action'] == action and (label is None or e['label'] == label)
                   for e in (panel.read() or {}).get('elements', []))

    def always_scenario(panel, hwnd, edits):
        text, number, password = edits
        # (With --always-show the activation-time focus report may already
        # have shown the panel; the click must show it in any case.)
        pe.click(*center(text))
        pe.wait_for(lambda: visible(panel), timeout=5, what='panel shown')
        check(has(panel, pe.KEY, '分词'), 'text field: Chinese nine-key layout')
        pe.click(*center(number))
        time.sleep(0.8)
        check(visible(panel) and has(panel, pe.TEXT, '7') and not has(panel, pe.KEY),
              f'IS_NUMBER field: number layout (focus {(panel.read() or {}).get("focus")!r})')
        pe.click(*center(password))
        time.sleep(1.0)
        check(visible(panel) and has(panel, pe.LETTER, 'abc'), 'password field: letters layout')
        # Multi-tap: "abc" twice quickly = b, "def" once = d, shift + "abc" = A.
        panel.tap(pe.LETTER, label='abc')
        panel.tap(pe.LETTER, label='abc')
        time.sleep(1.0)  # multi-tap window over
        panel.tap(pe.LETTER, label='def')
        time.sleep(1.0)
        panel.tap(pe.SHIFT)
        panel.tap(pe.LETTER, label='ABC')
        time.sleep(0.3)
        mirror = user32.GetDlgItem(hwnd, 4)
        check(pe.window_text(mirror) == 'bdA', f'multi-tap letters typed into the password (got {pe.window_text(mirror)!r})')
        panel.tap(pe.SHIFT)
        pe.click(*center(text))
        time.sleep(1.0)
        check(visible(panel) and has(panel, pe.KEY, '分词'), 'back to the text layout')

    def touch_scenario(panel, hwnd, edits):
        text = edits[0]
        pe.click(*center(text))
        time.sleep(1.0)
        check(not visible(panel), 'mouse focus does not pop the panel up (default settings)')

        def shown_by(tap_field, what):
            if not touch_tap(*center(tap_field)):
                print('SKIP touch: InjectTouchInput unavailable')
                return
            ok = False
            try:
                pe.wait_for(lambda: visible(panel), timeout=5, what=what)
                ok = True
            except AssertionError:
                pass
            check(ok, what + f' (last focus: {(panel.read() or {}).get("focus")!r})')

        # The text field already has the focus: no focus change, still pops up.
        shown_by(text, 'touch on the already focused field pops the panel up')
        # Hide with the panel's own key, then touch another field.
        panel.tap(pe.HIDE)
        pe.wait_for(lambda: not visible(panel), timeout=5, what='panel hidden')
        shown_by(edits[1], 'touch focus on another field pops the panel up')

    def switch_scenario(panel, hwnd, edits):
        pe.click(*center(edits[1]))  # IS_NUMBER field
        time.sleep(2.0)  # the target has been in front for a while
        check(not visible(panel), 'hidden while the target uses another input method')
        user32.PostMessageW(hwnd, WM_APP + 1, 0, 0)  # the user switches to T9Ime
        pe.wait_for(lambda: '[tip' in pe.window_text(hwnd), what='switch to T9Ime')
        ok = False
        try:
            pe.wait_for(lambda: visible(panel), timeout=5, what='panel shown')
            ok = True
        except AssertionError:
            pass
        check(ok and has(panel, pe.TEXT, '7'),
              f'switching to T9Ime pops the panel up with the field layout (focus {(panel.read() or {}).get("focus")!r})')

    caps = user32.GetKeyState(0x14) & 1
    try:
        run(True, always_scenario)
        run(False, touch_scenario)
        run(False, switch_scenario, '--activate-english')
    finally:
        subprocess.run(['taskkill', '/im', 'T9Host.exe', '/f'], capture_output=True)
        if caps and not user32.GetKeyState(0x14) & 1:
            te.tap(0x14)

    print('ALL PASSED' if not failures else f'{len(failures)} FAILED')
    return 1 if failures else 0


if __name__ == '__main__':
    sys.exit(main())
