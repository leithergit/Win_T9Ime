"""Compatibility probe with real applications (M3: Notepad).

Switches the session input method to T9Ime (tip_activate on), launches the
application, types full pinyin with SendInput and reads the result through the
clipboard (Ctrl+A, Ctrl+C). Needs T9Tip.dll registered and an interactive
desktop. Manual probe, not part of ctest: `tip_activate save` runs in a
console process whose "active profile" is not reliably the user's, so the
restore at the end may not bring back the previous input method (switch back
with Win+Space if needed).
"""
import argparse
import ctypes
import subprocess
import sys
import time
from ctypes import wintypes
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))
import tip_e2e as t  # noqa: E402

user32, kernel32 = t.user32, ctypes.WinDLL('kernel32')
user32.GetClipboardData.restype = ctypes.c_void_p
kernel32.GlobalLock.restype = ctypes.c_void_p
kernel32.GlobalLock.argtypes = [ctypes.c_void_p]
kernel32.GlobalUnlock.argtypes = [ctypes.c_void_p]


def clipboard_text() -> str:
    for _ in range(20):
        if user32.OpenClipboard(None):
            break
        time.sleep(0.05)
    try:
        h = user32.GetClipboardData(13)  # CF_UNICODETEXT
        if not h:
            return ''
        p = kernel32.GlobalLock(h)
        try:
            return ctypes.wstring_at(p)
        finally:
            kernel32.GlobalUnlock(h)
    finally:
        user32.CloseClipboard()


def ctrl(ch: str) -> None:
    t.key(0x11)
    t.tap(ord(ch))
    t.key(0x11, True)
    time.sleep(0.1)


def app_text() -> str:
    ctrl('A')
    ctrl('C')
    time.sleep(0.2)
    return clipboard_text()


def foreground_pid() -> int:
    pid = wintypes.DWORD()
    user32.GetWindowThreadProcessId(user32.GetForegroundWindow(), ctypes.byref(pid))
    return pid.value


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument('--bin', required=True, type=Path)
    ap.add_argument('--work', required=True, type=Path)
    args = ap.parse_args()
    if not t.registered():
        print('SKIP: T9Tip.dll is not registered')
        return 77
    args.work.mkdir(parents=True, exist_ok=True)
    saved = args.work / 'profile.bin'
    activate = str((args.bin / 'tip_activate.exe').resolve())
    subprocess.run([activate, 'save', str(saved)], check=True)
    subprocess.Popen([str((args.bin / 'T9Host.exe').resolve())])
    caps = user32.GetKeyState(0x14) & 1
    if caps:
        t.tap(0x14)
    failures = []

    def check(cond, msg):
        print(('PASS ' if cond else 'FAIL ') + msg)
        if not cond:
            failures.append(msg)

    try:
        r = subprocess.run([activate, 'on'], capture_output=True, text=True)
        check(r.returncode == 0, f'session profile switched to T9Ime ({r.stdout.strip()})')

        # --- Notepad (Windows 11: TSF-native RichEdit based editor) ---
        before = {p for p in subprocess.run(['tasklist', '/fo', 'csv', '/nh', '/fi', 'imagename eq notepad.exe'],
                                            capture_output=True, text=True).stdout.splitlines()}
        subprocess.Popen(['notepad.exe'])
        t.wait_for(lambda: 'notepad' in subprocess.run(
            ['tasklist', '/fi', f'pid eq {foreground_pid()}'], capture_output=True, text=True).stdout.lower(),
            timeout=15, what='notepad in the foreground')
        time.sleep(1.5)
        ctrl('N')  # fresh tab/document in case Notepad restored a session
        time.sleep(1.0)
        subprocess.run([activate, 'on'], capture_output=True)  # the new thread may start with another profile
        time.sleep(0.5)
        t.type_keys('nihao')
        t.tap(t.VK['space'])
        t.type_keys('zhongguo')
        t.tap(ord('1'))
        time.sleep(0.5)
        text = app_text()
        check(text == '你好中国', f'Notepad: nihao zhongguo -> 你好中国 (got {text!r})')
        t.tap(t.VK['back'])  # delete the selection
        time.sleep(0.2)
        # Close the tab without saving: Ctrl+W, then "Don't save" if asked.
        ctrl('W')
        time.sleep(0.8)
        t.tap(ord('N'))
        time.sleep(0.5)
        after = subprocess.run(['tasklist', '/fo', 'csv', '/nh', '/fi', 'imagename eq notepad.exe'],
                               capture_output=True, text=True).stdout.splitlines()
        if not before or before == {'INFO: No tasks are running which match the specified criteria.'}:
            subprocess.run(['taskkill', '/im', 'notepad.exe'], capture_output=True)
        _ = after
    finally:
        subprocess.run([activate, 'restore', str(saved)], capture_output=True)
        if caps and not user32.GetKeyState(0x14) & 1:
            t.tap(0x14)

    print('ALL PASSED' if not failures else f'{len(failures)} FAILED')
    return 1 if failures else 0


if __name__ == '__main__':
    sys.exit(main())
