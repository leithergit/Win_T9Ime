"""End-to-end test of the settings window (M6), without moving the mouse.

The window's controls are driven with window messages (check boxes, combo
boxes, buttons) and panel keys with posted clicks. Checks that applying
settings takes effect: fuzzy pinyin (redeploy), traditional characters,
theme and size saved, and that the status line reports the redeploy.
"""
import argparse
import ctypes
import os
import subprocess
import sys
import time
from ctypes import wintypes
from pathlib import Path

os.environ['T9IME_CLICK'] = 'post'  # never move the user's mouse
sys.path.insert(0, str(Path(__file__).parent))
import panel_e2e as pe  # noqa: E402

user32 = pe.user32
# A separately typed SendMessageW (the shared one is used with buffers elsewhere).
send = ctypes.WINFUNCTYPE(ctypes.c_ssize_t, wintypes.HWND, ctypes.c_uint, ctypes.c_size_t, ctypes.c_ssize_t)(
    ('SendMessageW', ctypes.windll.user32))
BM_CLICK, BM_SETCHECK, CB_SETCURSEL = 0x00F5, 0x00F1, 0x014E
# Control ids (src/host/app/settings_window.cpp).
IDS = dict(tabs=100, auto_show=101, always_show=102, show_on_switch=103, take_over=104, theme=106, size=108,
           script=111, page=113, ok=121, cancel=122, apply=123, status=124, fuzzy_first=200)
FUZZY_NL = 3  # FuzzyOptions(): z, c, s, nl, ...


def text(hwnd) -> str:
    return pe.window_text(hwnd)


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument('--host', required=True)
    ap.add_argument('--work', required=True, type=Path)
    args = ap.parse_args()
    work = args.work
    work.mkdir(parents=True, exist_ok=True)
    layout = work / 'settings_layout.json'
    ini = work / 'settings_panel.ini'
    user = work / 'settings_user'
    for p in (layout, ini):
        p.unlink(missing_ok=True)
    subprocess.run(['cmd', '/c', 'rmdir', '/s', '/q', str(user)], capture_output=True)
    failures = []

    def check(cond, msg):
        print(('PASS ' if cond else 'FAIL ') + msg)
        if not cond:
            failures.append(msg)

    host = subprocess.Popen([str(Path(args.host).resolve()), '--no-single-instance', '--pipe',
                             r'\\.\pipe\T9Ime.settings-e2e', '--show', '--user', str(user), '--settings', str(ini),
                             '--dump-layout', str(layout), '--open-settings'])
    try:
        panel = pe.Panel(layout)
        pe.wait_for(lambda: (panel.read() or {}).get('visible'), what='panel visible')
        w = pe.wait_for(lambda: user32.FindWindowW('T9Ime.Settings', None), what='settings window')
        item = lambda name: user32.GetDlgItem(w, IDS[name])
        time.sleep(1.5)  # engine warm-up

        def first_candidate(keys):
            for k in keys:
                panel.tap(pe.KEY, text=k)
            time.sleep(0.5)
            cands = [e['label'] for e in panel.read()['elements'] if e['action'] == pe.CANDIDATE]
            panel.tap(pe.CLEAR)
            return cands[0] if cands else ''

        check(first_candidate('54426') != '你好', 'without fuzzy pinyin 54426 (lihao) is not 你好')

        # Fuzzy n = l: apply -> redeploy, reported in the status line.
        send(user32.GetDlgItem(w, IDS['fuzzy_first'] + FUZZY_NL), BM_SETCHECK, 1, 0)
        send(item('apply'), BM_CLICK, 0, 0)
        status = item('status')
        pe.wait_for(lambda: '完成' in text(status) or '失败' in text(status), timeout=60, what='redeploy finished')
        check(text(status) == '重新部署完成', f'status line reports the redeploy ({text(status)!r})')
        time.sleep(0.5)
        check(first_candidate('54426') == '你好', 'fuzzy n = l after the redeploy: 54426 gives 你好')

        # Traditional characters: no redeploy.
        send(item('script'), CB_SETCURSEL, 1, 0)
        send(item('apply'), BM_CLICK, 0, 0)
        time.sleep(0.5)
        check(first_candidate('94664486') == '中國', 'traditional characters (中國)')

        # Theme and size are saved.
        send(item('theme'), CB_SETCURSEL, 2, 0)  # dark
        send(item('size'), CB_SETCURSEL, 2, 0)   # large
        before = panel.read()['window']
        send(item('ok'), BM_CLICK, 0, 0)
        pe.wait_for(lambda: not user32.FindWindowW('T9Ime.Settings', None), what='settings closed')
        time.sleep(0.5)
        after = panel.read()['window']
        saved = ini.read_text(encoding='utf-16', errors='replace') if ini.read_bytes()[:2] == b'\xff\xfe' else \
            ini.read_text(errors='replace')
        check('theme=1' in saved, 'dark theme saved')
        check(after[2] - after[0] > before[2] - before[0], f'large size applied ({before} -> {after})')
        check('fuzzy=nl' in saved and 'traditional=1' in saved, 'input settings saved')
    finally:
        host.terminate()

    print('ALL PASSED' if not failures else f'{len(failures)} FAILED')
    return 1 if failures else 0


if __name__ == '__main__':
    sys.exit(main())
