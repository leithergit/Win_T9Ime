"""End-to-end automatic IME switch (M4, SPEC U6).

test_target starts with the US English keyboard (--activate-english). Tapping
the panel must switch the application to T9Ime: the panel text arrives, and
afterwards physical typing produces Chinese. test_target restores the user's
previous input method when it closes. Needs T9Tip.dll registered.
"""
import argparse
import ctypes
import subprocess
import sys
import time
from ctypes import wintypes
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))
import panel_e2e as pe  # noqa: E402
import tip_e2e as te  # noqa: E402

user32 = pe.user32


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
    layout = args.work / 'switch_layout.json'
    layout.unlink(missing_ok=True)
    subprocess.run(['taskkill', '/im', 'T9Host.exe', '/f'], capture_output=True)
    time.sleep(0.5)
    caps = user32.GetKeyState(0x14) & 1
    if caps:
        te.tap(0x14)
    host = pe.start_host(args.host, '--show', '--user', str(args.work / 'switch_user'),
                             '--settings', str(args.work / 'switch_panel.ini'), '--dump-layout', str(layout))
    target = subprocess.Popen([str(Path(args.target).resolve()), '--activate-english'])
    failures = []

    def check(cond, msg):
        print(('PASS ' if cond else 'FAIL ') + msg)
        if not cond:
            failures.append(msg)

    try:
        panel = pe.Panel(layout)
        pe.wait_for(lambda: (panel.read() or {}).get('visible'), what='panel visible')
        hwnd = pe.wait_for(lambda: user32.FindWindowW('T9Ime.TestTarget', None), what='test target')
        edit = user32.FindWindowExW(hwnd, None, 'Edit', None)
        pe.wait_for(lambda: '[en' in pe.window_text(hwnd), what='english activation')
        check(pe.window_text(hwnd).endswith('[en]'), 'target starts with the English keyboard')
        r = wintypes.RECT()
        user32.GetWindowRect(edit, ctypes.byref(r))
        pe.click((r.left + r.right) // 2, (r.top + r.bottom) // 2)
        pe.wait_for(lambda: user32.GetForegroundWindow() == hwnd, what='target foreground')
        time.sleep(0.5)
        te.type_keys('abc')
        time.sleep(0.3)
        check(pe.window_text(edit) == 'abc', f'physical keys are plain English (got {pe.window_text(edit)!r})')
        user32.SendMessageW(edit, 0x0C, 0, ctypes.c_wchar_p(''))

        start = time.time()
        panel.keys('94664486')
        panel.tap(pe.CANDIDATE, index=0)
        time.sleep(0.5)
        d = panel.read()
        check(pe.window_text(edit) == '中国', f'panel text arrives (got {pe.window_text(edit)!r}, '
                                             f'pushed={d["pushed"]} sent={d["sent"]}, {time.time() - start:.1f}s)')
        user32.SendMessageW(edit, 0x0C, 0, ctypes.c_wchar_p(''))
        te.type_keys('nihao')
        te.tap(te.VK['space'])
        time.sleep(0.4)
        check(pe.window_text(edit) == '你好', f'the application now uses T9Ime (got {pe.window_text(edit)!r})')
        check(user32.GetForegroundWindow() == hwnd, 'target kept the foreground')
    finally:
        user32.PostMessageW(user32.FindWindowW('T9Ime.TestTarget', None), pe.WM_CLOSE, 0, 0)
        try:
            target.wait(5)
        except subprocess.TimeoutExpired:
            target.kill()
        host.terminate()
        if caps and not user32.GetKeyState(0x14) & 1:
            te.tap(0x14)

    print('ALL PASSED' if not failures else f'{len(failures)} FAILED')
    return 1 if failures else 0


if __name__ == '__main__':
    sys.exit(main())
