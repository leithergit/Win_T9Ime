"""End-to-end push test (M4): touch panel output goes through the TIP.

test_target runs with T9Ime active; T9Host (default pipes) shows the panel.
Panel commits must arrive through the events pipe and the TIP's edit session
(layout dump: "pushed" grows, "sent" = SendInput fallback stays 0), and must
interact correctly with a physical-keyboard composition.
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
import panel_e2e as pe  # noqa: E402  (sets per-monitor DPI awareness)
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
    layout = args.work / 'push_layout.json'
    layout.unlink(missing_ok=True)
    subprocess.run(['taskkill', '/im', 'T9Host.exe', '/f'], capture_output=True)
    time.sleep(0.5)
    caps = user32.GetKeyState(0x14) & 1
    if caps:
        te.tap(0x14)
    host = pe.start_host(args.host, '--show', '--user', str(args.work / 'push_user'),
                             '--settings', str(args.work / 'push_panel.ini'), '--dump-layout', str(layout))
    target = subprocess.Popen([str(Path(args.target).resolve()), '--activate-tip'])
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
        pe.wait_for(lambda: '[tip' in pe.window_text(hwnd), what='profile activation')
        r = wintypes.RECT()
        user32.GetWindowRect(edit, ctypes.byref(r))
        pe.click((r.left + r.right) // 2, (r.top + r.bottom) // 2)
        pe.wait_for(lambda: user32.GetForegroundWindow() == hwnd, what='target foreground')
        time.sleep(1.0)  # TIP connects, reports focus, attaches the events pipe

        def clear():
            user32.SendMessageW(edit, 0x0C, 0, ctypes.c_wchar_p(''))  # WM_SETTEXT

        panel.keys('94664486')
        panel.tap(pe.CANDIDATE, index=0)
        time.sleep(0.3)
        d = panel.read()
        check(pe.window_text(edit) == '中国', f'panel commit reaches the app (got {pe.window_text(edit)!r})')
        check(d['pushed'] >= 1 and d['sent'] == 0, f'delivered through the TIP (pushed={d["pushed"]}, sent={d["sent"]})')
        if d['pushed'] == 0:
            diag = Path(args.host).resolve().with_name('t9diag.exe')
            print(subprocess.run([str(diag), '--watch', '1'], capture_output=True).stdout.decode('utf-8', 'replace'))
        check(user32.GetForegroundWindow() == hwnd, 'target keeps the foreground')

        # Physical composition, then a panel commit: the panel text replaces it.
        clear()
        te.type_keys('ni')
        time.sleep(0.3)
        panel.keys('744')
        panel.tap(pe.SPACE)
        time.sleep(0.4)
        check(pe.window_text(edit) == '是', f'panel commit replaces a physical composition (got {pe.window_text(edit)!r})')
        te.type_keys('hao')
        te.tap(te.VK['space'])
        time.sleep(0.3)
        check(pe.window_text(edit) == '是好', f'physical session was reset (got {pe.window_text(edit)!r})')

        # Panel composing, then a physical key: the panel composition is dropped.
        clear()
        panel.keys('9466')
        check(panel.input() == '9466', 'panel composing')
        te.type_keys('a')
        time.sleep(0.4)
        check(panel.input() == '', f'physical key clears the panel composition (panel input {panel.input()!r})')
        te.tap(te.VK['space'])
        time.sleep(0.3)
        check(pe.window_text(edit) == '啊', f'physical input continues normally (got {pe.window_text(edit)!r})')

        # Quick punctuation and backspace passthrough with the TIP active.
        clear()
        panel.tap(pe.TEXT, label='，')
        panel.tap(pe.BACKSPACE)
        panel.tap(pe.TEXT, label='。')
        time.sleep(0.3)
        check(pe.window_text(edit) == '。', f'punctuation push + backspace (got {pe.window_text(edit)!r})')
        d = panel.read()
        check(d['sent'] == 0, f'no SendInput fallback while the TIP is focused (sent={d["sent"]})')
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
