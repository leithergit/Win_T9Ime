"""End-to-end panel test (M2, SendInput path).

Starts test_target (an Edit control) and T9Host, then clicks panel keys with the
real mouse (SendInput) and checks the Edit text. Also checks that clicking the
panel never takes the foreground away from the target.

Needs an interactive desktop; moves the mouse cursor while running.
"""
import argparse
import ctypes
import os
import json
import subprocess
import sys
import time
from ctypes import wintypes
from pathlib import Path

sys.stdout.reconfigure(encoding='utf-8')
user32 = ctypes.WinDLL('user32', use_last_error=True)
try:
    user32.SetProcessDpiAwarenessContext(ctypes.c_void_p(-4))  # per-monitor v2: physical pixels
except AttributeError:
    user32.SetProcessDPIAware()

# panel_layout.h Action values
KEY, TEXT, BACKSPACE, CLEAR, SPACE, ENTER, SYMBOLS, NUMBERS, TOGGLE, BACK, HIDE, EXPAND, CANDIDATE, PINYIN = range(1, 15)
SYMBOL_CATEGORY, HANDLE, LETTER, SHIFT = range(15, 19)

INPUT_MOUSE = 0
MOUSEEVENTF_MOVE, MOUSEEVENTF_LEFTDOWN, MOUSEEVENTF_LEFTUP = 0x1, 0x2, 0x4
MOUSEEVENTF_VIRTUALDESK, MOUSEEVENTF_ABSOLUTE = 0x4000, 0x8000
WM_GETTEXT, WM_GETTEXTLENGTH, WM_CLOSE = 0x0D, 0x0E, 0x10


class MOUSEINPUT(ctypes.Structure):
    _fields_ = [('dx', wintypes.LONG), ('dy', wintypes.LONG), ('mouseData', wintypes.DWORD),
                ('dwFlags', wintypes.DWORD), ('time', wintypes.DWORD), ('dwExtraInfo', ctypes.c_size_t)]


class INPUT(ctypes.Structure):
    class _U(ctypes.Union):
        _fields_ = [('mi', MOUSEINPUT), ('pad', ctypes.c_byte * 32)]
    _anonymous_ = ('u',)
    _fields_ = [('type', wintypes.DWORD), ('u', _U)]


def _post_click(x: int, y: int) -> None:
    """Delivers the click as window messages to the window under the point.
    Used in VMware guests, where the tools' absolute pointer keeps moving the
    cursor back to the host mouse position so injected clicks miss."""
    user32.WindowFromPoint.restype = wintypes.HWND
    user32.WindowFromPoint.argtypes = [wintypes.POINT]
    hwnd = user32.WindowFromPoint(wintypes.POINT(x, y))
    pt = wintypes.POINT(x, y)
    user32.ScreenToClient(hwnd, ctypes.byref(pt))
    lp = ((pt.y & 0xFFFF) << 16) | (pt.x & 0xFFFF)
    # A real click also focuses / activates normal windows.
    top = user32.GetAncestor(hwnd, 2)  # GA_ROOT
    ex = user32.GetWindowLongW(top, -20)
    if not ex & 0x08000000:  # WS_EX_NOACTIVATE
        user32.SetForegroundWindow(top)
        tid = user32.GetWindowThreadProcessId(hwnd, None)
        me = ctypes.windll.kernel32.GetCurrentThreadId()
        user32.AttachThreadInput(me, tid, True)
        user32.SetFocus(hwnd)
        user32.AttachThreadInput(me, tid, False)
    user32.PostMessageW(hwnd, 0x0201, 1, lp)  # WM_LBUTTONDOWN
    time.sleep(0.03)
    user32.PostMessageW(hwnd, 0x0202, 0, lp)  # WM_LBUTTONUP
    time.sleep(0.05)


def click(x: int, y: int) -> None:
    if os.environ.get('T9IME_CLICK') == 'post':
        _post_click(x, y)
        return
    vx, vy = user32.GetSystemMetrics(76), user32.GetSystemMetrics(77)
    vw, vh = user32.GetSystemMetrics(78), user32.GetSystemMetrics(79)
    ax = int((x - vx) * 65535 / (vw - 1))
    ay = int((y - vy) * 65535 / (vh - 1))
    base = MOUSEEVENTF_ABSOLUTE | MOUSEEVENTF_VIRTUALDESK
    events = [base | MOUSEEVENTF_MOVE, base | MOUSEEVENTF_LEFTDOWN, base | MOUSEEVENTF_LEFTUP]
    for flags in events:
        inp = INPUT(type=INPUT_MOUSE)
        inp.mi = MOUSEINPUT(ax, ay, 0, flags, 0, 0)
        user32.SendInput(1, ctypes.byref(inp), ctypes.sizeof(INPUT))
        time.sleep(0.03)


def drag(x0: int, y0: int, x1: int, y1: int, steps: int = 8) -> None:
    vx, vy = user32.GetSystemMetrics(76), user32.GetSystemMetrics(77)
    vw, vh = user32.GetSystemMetrics(78), user32.GetSystemMetrics(79)
    base = MOUSEEVENTF_ABSOLUTE | MOUSEEVENTF_VIRTUALDESK

    def send(x, y, flags):
        inp = INPUT(type=INPUT_MOUSE)
        inp.mi = MOUSEINPUT(int((x - vx) * 65535 / (vw - 1)), int((y - vy) * 65535 / (vh - 1)), 0,
                            base | flags, 0, 0)
        user32.SendInput(1, ctypes.byref(inp), ctypes.sizeof(INPUT))
        time.sleep(0.03)

    send(x0, y0, MOUSEEVENTF_MOVE)
    send(x0, y0, MOUSEEVENTF_LEFTDOWN)
    for i in range(1, steps + 1):
        send(x0 + (x1 - x0) * i // steps, y0 + (y1 - y0) * i // steps, MOUSEEVENTF_MOVE)
    send(x1, y1, MOUSEEVENTF_LEFTUP)


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


def start_host(host: str, *args, attempts: int = 5):
    """Starts T9Host and makes sure it is the one serving the IME pipes.

    With T9Ime active in other applications, their TIPs relaunch a background
    host as soon as the previous one is killed; that host may grab the pipes
    first. Kill everything and retry until ours owns them."""
    host = str(Path(host).resolve())
    diag = str(Path(host).with_name('t9diag.exe'))
    for _ in range(attempts):
        subprocess.run(['taskkill', '/im', 'T9Host.exe', '/f'], capture_output=True)
        time.sleep(0.3)
        proc = subprocess.Popen([host, *args])
        for _ in range(30):
            time.sleep(0.1)
            out = subprocess.run([diag, '--owner'], capture_output=True, text=True).stdout.strip()
            if out and out != '0':
                break
        if out == str(proc.pid):
            return proc
        proc.kill()
    raise AssertionError('could not start a T9Host that owns the pipes')


class Panel:
    def __init__(self, layout_file: Path):
        self.file = layout_file

    def read(self):
        try:
            return json.loads(self.file.read_text(encoding='utf-8'))
        except (OSError, ValueError):
            return None

    def find(self, action, *, text=None, label=None, index=None):
        layout = wait_for(self.read, what='layout')
        for e in layout['elements']:
            if e['action'] == action and (text is None or e['text'] == text) and \
                    (label is None or e['label'] == label) and (index is None or e['index'] == index):
                return e
        raise AssertionError(f'element not found: action={action} text={text} label={label} index={index}')

    def has(self, action, **kw) -> bool:
        try:
            self.find(action, **kw)
            return True
        except AssertionError:
            return False

    def tap(self, action, **kw):
        e = self.find(action, **kw)
        before = self.read()['seq']
        click(e['x'], e['y'])
        # Wait for the panel to repaint after the engine answered.
        wait_for(lambda: (self.read() or {}).get('seq', before) > before, what='repaint')
        time.sleep(0.15)

    def keys(self, digits: str):
        for d in digits:
            self.tap(KEY, text=d)

    def letters(self, text: str):
        for c in text:
            self.tap(LETTER, label=c)

    def input(self) -> str:
        return self.read()['input']


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument('--host', required=True)
    ap.add_argument('--target', required=True)
    ap.add_argument('--work', required=True, type=Path)
    args = ap.parse_args()
    args.work.mkdir(parents=True, exist_ok=True)
    layout_file = args.work / 'layout.json'
    layout_file.unlink(missing_ok=True)

    saved = wintypes.POINT()
    user32.GetCursorPos(ctypes.byref(saved))
    args.host = str(Path(args.host).resolve())
    target = subprocess.Popen([str(Path(args.target).resolve())])
    host = None
    failures = []

    def check(cond, msg):
        print(('PASS ' if cond else 'FAIL ') + msg)
        if not cond:
            failures.append(msg)

    try:
        hwnd = wait_for(lambda: user32.FindWindowW('T9Ime.TestTarget', None), what='test target')
        edit = user32.FindWindowExW(hwnd, None, 'Edit', None)
        rect = wintypes.RECT()
        user32.GetWindowRect(hwnd, ctypes.byref(rect))
        click((rect.left + rect.right) // 2, (rect.top + rect.bottom) // 2)  # focus the target
        wait_for(lambda: user32.GetForegroundWindow() == hwnd, what='target foreground')

        (args.work / 'panel.ini').unlink(missing_ok=True)  # default size: earlier runs resized it
        host = subprocess.Popen([args.host, '--no-single-instance', '--show',
                                 '--user', str(args.work / 'user'), '--settings', str(args.work / 'panel.ini'),
                                 '--dump-layout', str(layout_file)])
        panel = Panel(layout_file)
        wait_for(lambda: (panel.read() or {}).get('visible'), what='panel visible')
        time.sleep(1.0)  # engine start

        panel.keys('94664486')
        check(panel.input() == '94664486', 'typing digits fills the composition')
        check(user32.GetForegroundWindow() == hwnd, 'panel clicks keep the target in the foreground')
        panel.tap(CANDIDATE, index=0)
        check(window_text(edit) == '中国', f'first candidate commits 中国 (got {window_text(edit)!r})')

        panel.keys('744')
        panel.tap(PINYIN, index=0)
        check(panel.input() == "shi'", f"pinyin bar pick gives shi' (got {panel.input()!r})")
        panel.tap(SPACE)
        check(window_text(edit) == '中国是', f'space commits the first candidate (got {window_text(edit)!r})')

        panel.keys('94664486')
        panel.tap(ENTER)
        check(window_text(edit) == '中国是zhongguo', f'enter commits pinyin (got {window_text(edit)!r})')

        panel.tap(BACKSPACE)
        check(window_text(edit) == '中国是zhonggu', 'backspace without composition deletes in the app')

        panel.keys('9466')
        panel.tap(CLEAR)
        check(panel.input() == '', 'clear drops the composition')

        panel.tap(NUMBERS)
        panel.tap(TEXT, label='5')
        panel.tap(BACK)
        panel.tap(TEXT, label='，')  # quick punctuation in the side list
        check(window_text(edit) == '中国是zhonggu5，', f'numbers and punctuation (got {window_text(edit)!r})')

        # English: QWERTY, letters typed directly; shift is one-shot.
        panel.tap(TOGGLE)
        check(panel.has(LETTER, label='q') and not panel.has(KEY), 'english is a QWERTY keyboard')
        panel.letters('hello')
        panel.tap(SPACE)
        panel.tap(SHIFT)
        panel.letters('Work')
        panel.tap(TEXT, label='.')
        check(window_text(edit).endswith('hello Work.'), f'english QWERTY typing (got {window_text(edit)!r})')
        panel.tap(TOGGLE)
        check(panel.has(KEY, text='2'), 'back to the Chinese nine-key layout')

        # Resize with the grip at the right end of the handle strip.
        before = panel.read()['window']
        handle = panel.find(16)  # kHandle
        gx, gy = before[2] - 8, handle['y']
        drag(gx, gy, gx + 60, gy - 40)
        after = wait_for(lambda: (lambda w: w if w != before else None)(panel.read()['window']), what='resize')
        check(after[2] - after[0] > before[2] - before[0] and after[3] - after[1] > before[3] - before[1]
              and after[3] == before[3], f'grip resizes the panel ({before} -> {after})')

        check(user32.GetForegroundWindow() == hwnd, 'target is still in the foreground at the end')
    finally:
        if host:
            host.terminate()
        user32.PostMessageW(user32.FindWindowW('T9Ime.TestTarget', None), WM_CLOSE, 0, 0)
        try:
            target.wait(5)
        except subprocess.TimeoutExpired:
            target.kill()
        user32.SetCursorPos(saved.x, saved.y)

    print('ALL PASSED' if not failures else f'{len(failures)} FAILED')
    return 1 if failures else 0


if __name__ == '__main__':
    sys.exit(main())
