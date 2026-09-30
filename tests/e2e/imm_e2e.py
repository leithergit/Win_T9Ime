"""IMM32 compatibility (SPEC §7.1): an application that uses the classic IME
API on a TSF input method (CUAS maps it to the TSF compartments).

  ImmSetConversionStatus(IME_CMODE_NATIVE on / off) -> T9Ime Chinese / English
  ImmSetOpenStatus(FALSE / TRUE)                     -> keys pass through / T9Ime again
  T9Ime's own Shift toggle                           -> visible in ImmGetConversionStatus
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
user32.SendMessageW.restype = ctypes.c_ssize_t
WM_APP = 0x8000
IME_CMODE_NATIVE = 1


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

    caps = user32.GetKeyState(0x14) & 1
    if caps:
        te.tap(0x14)
    host = pe.start_host(args.host, '--user', str(args.work / 'imm_user'), '--settings', str(args.work / 'imm.ini'))
    target = subprocess.Popen([str(Path(args.target).resolve()), '--activate-tip'])
    try:
        hwnd = pe.wait_for(lambda: user32.FindWindowW('T9Ime.TestTarget', None), what='test target')
        edit = user32.FindWindowExW(hwnd, None, 'Edit', None)
        pe.wait_for(lambda: '[tip' in pe.window_text(hwnd), what='profile activation')
        r = wintypes.RECT()
        user32.GetWindowRect(edit, ctypes.byref(r))
        pe.click((r.left + r.right) // 2, (r.top + r.bottom) // 2)
        pe.wait_for(lambda: user32.GetForegroundWindow() == hwnd, what='target foreground')
        time.sleep(0.8)

        def state():
            v = user32.SendMessageW(hwnd, WM_APP + 4, 0, 0)
            return bool(v & 1), (v >> 16) & 0xFFFF

        def typed(keys, space=True):
            user32.SendMessageW(edit, 0x0C, 0, ctypes.c_wchar_p(''))
            te.type_keys(keys)
            if space:
                te.tap(te.VK['space'])
            time.sleep(0.5)
            return pe.window_text(edit)

        is_open, conv = state()
        check(is_open and conv & IME_CMODE_NATIVE, f'IMM sees T9Ime open in Chinese mode (open={is_open}, conversion={conv:#x})')
        got = typed('nihao')
        check(got == '你好', f'Chinese input (got {got!r})')

        user32.SendMessageW(hwnd, WM_APP + 3, 0, 0)  # ImmSetConversionStatus: alphanumeric
        time.sleep(0.4)
        got = typed('ab', space=False)
        check(got == 'ab', f'ImmSetConversionStatus(alphanumeric) -> English (got {got!r})')
        user32.SendMessageW(hwnd, WM_APP + 3, 1, 0)  # native
        time.sleep(0.4)
        got = typed('nihao')
        check(got == '你好', f'ImmSetConversionStatus(IME_CMODE_NATIVE) -> Chinese again (got {got!r})')

        user32.SendMessageW(edit, 0x0C, 0, ctypes.c_wchar_p(''))
        te.tap(te.VK['shift'])  # T9Ime's own toggle
        time.sleep(0.4)
        check(not state()[1] & IME_CMODE_NATIVE, f'Shift toggle -> ImmGetConversionStatus alphanumeric ({state()[1]:#x})')
        te.tap(te.VK['shift'])
        time.sleep(0.4)
        check(state()[1] & IME_CMODE_NATIVE, f'Shift again -> native ({state()[1]:#x})')

        user32.SendMessageW(hwnd, WM_APP + 2, 0, 0)  # ImmSetOpenStatus(FALSE)
        time.sleep(0.4)
        got = typed('xy', space=False)
        check(got == 'xy' and not state()[0], f'ImmSetOpenStatus(FALSE) -> keys pass through (got {got!r})')
        user32.SendMessageW(hwnd, WM_APP + 2, 1, 0)  # ImmSetOpenStatus(TRUE)
        time.sleep(0.4)
        got = typed('nihao')
        check(got == '你好' and state()[0], f'ImmSetOpenStatus(TRUE) -> T9Ime again (got {got!r})')
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
