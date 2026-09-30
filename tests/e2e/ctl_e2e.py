"""End-to-end test of the control API (T9Ctl.dll through t9ctl.exe).

Show / hide / toggle, layouts, position and docking, visibility notifications
(t9ctl watch), and switching an application to T9Ime and back (T9_Activate /
T9_Deactivate on another process's window).
"""
import argparse
import ctypes
import subprocess
import sys
import threading
import time
from ctypes import wintypes
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))
import panel_e2e as pe  # noqa: E402  (per-monitor DPI aware)
import tip_e2e as te  # noqa: E402

user32 = pe.user32


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument('--host', required=True)
    ap.add_argument('--target', required=True)
    ap.add_argument('--ctl', required=True)
    ap.add_argument('--work', required=True, type=Path)
    args = ap.parse_args()
    if not te.registered():
        print('SKIP: T9Tip.dll is not registered')
        return 77
    args.work.mkdir(parents=True, exist_ok=True)
    layout = args.work / 'ctl_layout.json'
    layout.unlink(missing_ok=True)
    ini = args.work / 'ctl_panel.ini'
    ini.unlink(missing_ok=True)
    failures = []

    def check(cond, msg):
        print(('PASS ' if cond else 'FAIL ') + msg)
        if not cond:
            failures.append(msg)

    def ctl(*a):
        r = subprocess.run([str(Path(args.ctl).resolve()), *a], capture_output=True, text=True, timeout=30)
        return r.returncode, r.stdout.strip()

    def status():
        out = ctl('status')[1]
        fields = dict(kv.split('=', 1) for kv in out.split())
        fields['rect'] = [int(v) for v in fields['rect'].split(',')]
        return fields

    caps = user32.GetKeyState(0x14) & 1
    if caps:
        te.tap(0x14)
    host = pe.start_host(args.host, '--user', str(args.work / 'ctl_user'), '--settings', str(ini),
                         '--dump-layout', str(layout))
    watch = None
    target = None
    try:
        panel = pe.Panel(layout)
        pe.wait_for(panel.read, what='layout dump')
        s = status()
        check(s['visible'] == '0' and s['mode'] == 'chinese', f'status of a hidden panel ({s})')

        # Visibility notifications, collected in the background.
        lines = []
        watch = subprocess.Popen([str(Path(args.ctl).resolve()), 'watch', '30'], stdout=subprocess.PIPE, text=True)
        threading.Thread(target=lambda: [lines.append(l.strip()) for l in watch.stdout], daemon=True).start()
        pe.wait_for(lambda: 'watching' in lines, what='watch registered')

        code, _ = ctl('show', 'number')
        pe.wait_for(lambda: (panel.read() or {}).get('visible'), timeout=5, what='panel shown')
        check(code == 0 and panel.has(pe.TEXT, label='7') and not panel.has(pe.KEY),
              'show number: number layout visible')
        s = status()
        check(s['visible'] == '1' and s['mode'] == 'number',
              f'status after show ({s}, last focus {(panel.read() or {}).get("focus")!r})')

        ctl('mode', 'english')
        time.sleep(0.4)
        s = status()
        check(panel.has(pe.LETTER, label='q') and s['mode'] == 'english', f'mode english: QWERTY layout ({s})')
        ctl('mode', 'chinese')
        time.sleep(0.4)
        check(panel.has(pe.KEY, text='2'), 'mode chinese: nine-key layout')
        check(ctl('mode', 'bogus')[0] == 2, 'unknown mode is a usage error')

        ctl('pos', '120', '140')
        time.sleep(0.4)
        s = status()
        window = panel.read()['window']
        check(s['rect'][:2] == [120, 140] and window[:2] == [120, 140], f'pos moves the panel ({s["rect"]}, {window})')
        ctl('dock')
        time.sleep(0.4)
        s = status()
        check(s['rect'][:2] != [120, 140], f'dock moves it back ({s["rect"]})')

        ctl('hide')
        pe.wait_for(lambda: not (panel.read() or {}).get('visible'), timeout=5, what='panel hidden')
        check(status()['visible'] == '0', 'hide')
        ctl('toggle')
        pe.wait_for(lambda: (panel.read() or {}).get('visible'), timeout=5, what='toggle shows')
        ctl('toggle')
        pe.wait_for(lambda: not (panel.read() or {}).get('visible'), timeout=5, what='toggle hides')
        time.sleep(0.5)
        shown = [l for l in lines if l.startswith('notify visible=1')]
        hidden = [l for l in lines if l.startswith('notify visible=0')]
        check(len(shown) >= 2 and len(hidden) >= 2, f'visibility notifications ({len(shown)} shown, {len(hidden)} hidden)')
        check(any(l.endswith('rect=120,140,' + ','.join(map(str, s['rect'][2:]))) for l in lines) or
              any('rect=120,140,' in l for l in lines), 'notification after a move reports the new rectangle')

        # Switching another application to T9Ime and back.
        target = subprocess.Popen([str(Path(args.target).resolve()), '--activate-english'])
        hwnd = pe.wait_for(lambda: user32.FindWindowW('T9Ime.TestTarget', None), what='test target')
        edit = user32.FindWindowExW(hwnd, None, 'Edit', None)
        pe.wait_for(lambda: '[en' in pe.window_text(hwnd), what='english activation')
        r = wintypes.RECT()
        user32.GetWindowRect(edit, ctypes.byref(r))
        pe.click((r.left + r.right) // 2, (r.top + r.bottom) // 2)
        pe.wait_for(lambda: user32.GetForegroundWindow() == hwnd, what='target foreground')
        time.sleep(0.5)
        te.type_keys('ab')
        time.sleep(0.3)
        check(pe.window_text(edit) == 'ab', f'target starts in English (got {pe.window_text(edit)!r})')
        user32.SendMessageW(edit, 0x0C, 0, ctypes.c_wchar_p(''))

        code, _ = ctl('activate', str(hwnd))
        time.sleep(1.0)
        te.type_keys('nihao')
        te.tap(te.VK['space'])
        time.sleep(0.5)
        check(code == 0 and pe.window_text(edit) == '你好', f'activate switches the target to T9Ime (got {pe.window_text(edit)!r})')
        user32.SendMessageW(edit, 0x0C, 0, ctypes.c_wchar_p(''))

        code, _ = ctl('deactivate', str(hwnd))
        time.sleep(1.0)
        te.type_keys('ab')
        time.sleep(0.3)
        check(code == 0 and pe.window_text(edit) == 'ab', f'deactivate switches it away (got {pe.window_text(edit)!r})')

        # D1: a keyboard shown for an application hides when another one comes to the front.
        ctl('show')
        pe.wait_for(lambda: (panel.read() or {}).get('visible'), timeout=5, what='panel shown for the target')
        number = user32.FindWindowExW(hwnd, edit, 'Edit', None)
        user32.GetWindowRect(number, ctypes.byref(r))
        pe.click((r.left + r.right) // 2, (r.top + r.bottom) // 2)
        time.sleep(0.8)
        check((panel.read() or {}).get('visible'), 'stays while the user works in the same application')
        other = subprocess.Popen([str(Path(args.target).resolve()), '--title', 'T9Ime Other', '--at', '640,60'])
        other_hwnd = pe.wait_for(lambda: user32.FindWindowW(None, 'T9Ime Other'), what='second application')
        other_edit = user32.FindWindowExW(other_hwnd, None, 'Edit', None)
        time.sleep(0.5)
        user32.GetWindowRect(other_edit, ctypes.byref(r))
        pe.click((r.left + r.right) // 2, (r.top + r.bottom) // 2)
        pe.wait_for(lambda: user32.GetForegroundWindow() == other_hwnd, what='second application in front')
        ok = True
        try:
            pe.wait_for(lambda: not (panel.read() or {}).get('visible'), timeout=5, what='panel hidden')
        except AssertionError:
            ok = False
        check(ok, 'switching to another application hides the keyboard')
        user32.PostMessageW(other_hwnd, pe.WM_CLOSE, 0, 0)
        try:
            other.wait(5)
        except subprocess.TimeoutExpired:
            other.kill()
    finally:
        if target:
            user32.PostMessageW(user32.FindWindowW(None, 'T9Ime Other') or 0, pe.WM_CLOSE, 0, 0)
            user32.PostMessageW(user32.FindWindowW('T9Ime.TestTarget', None), pe.WM_CLOSE, 0, 0)
            try:
                target.wait(5)
            except subprocess.TimeoutExpired:
                target.kill()
        if watch:
            watch.kill()
        host.terminate()
        if caps and not user32.GetKeyState(0x14) & 1:
            te.tap(0x14)

    print('ALL PASSED' if not failures else f'{len(failures)} FAILED')
    return 1 if failures else 0


if __name__ == '__main__':
    sys.exit(main())
