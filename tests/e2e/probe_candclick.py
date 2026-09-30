"""Probe: what happens when the TIP candidate window is clicked (debug aid)."""
import ctypes
import subprocess
import sys
import time
from ctypes import wintypes
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))
import panel_e2e as pe  # noqa: E402
import tip_e2e as te  # noqa: E402

u = pe.user32
u.WindowFromPoint.restype = wintypes.HWND
u.WindowFromPoint.argtypes = [wintypes.POINT]
target = subprocess.Popen([sys.argv[1], '--activate-tip'])
try:
    h = pe.wait_for(lambda: u.FindWindowW('T9Ime.TestTarget', None))
    e = u.FindWindowExW(h, None, 'Edit', None)
    pe.wait_for(lambda: '[tip' in pe.window_text(h))
    r = wintypes.RECT()
    u.GetWindowRect(e, ctypes.byref(r))
    pe.click((r.left + r.right) // 2, (r.top + r.bottom) // 2)
    time.sleep(0.5)
    te.type_keys('shi')
    time.sleep(0.5)
    c = te.candidate_window(target.pid)
    cr = wintypes.RECT()
    u.GetWindowRect(c, ctypes.byref(cr))
    x, y = cr.left + (cr.right - cr.left) * 3 // 4, (cr.top + cr.bottom) // 2
    w = u.WindowFromPoint(wintypes.POINT(x, y))
    cls = ctypes.create_unicode_buffer(64)
    u.GetClassNameW(w, cls, 64)
    print('dpi', u.GetDeviceCaps(u.GetDC(None), 88) if hasattr(u, 'GetDeviceCaps') else '?')
    print('cand rect', cr.left, cr.top, cr.right, cr.bottom, 'click', x, y, '->', cls.value)
    pe.click(x, y)
    time.sleep(0.2)
    print('after 0.2s:', repr(pe.window_text(e)), 'cand visible', bool(te.candidate_window(target.pid)))
    time.sleep(1.0)
    print('after 1.2s:', repr(pe.window_text(e)), 'cand visible', bool(te.candidate_window(target.pid)))
    # Same click delivered as window messages (bypasses mouse injection).
    lp = ((cr.bottom - cr.top) // 2 << 16) | ((cr.right - cr.left) * 3 // 4)
    u.PostMessageW(c, 0x0201, 1, lp)  # WM_LBUTTONDOWN
    u.PostMessageW(c, 0x0202, 0, lp)  # WM_LBUTTONUP
    time.sleep(0.8)
    print('after posted click:', repr(pe.window_text(e)), 'cand visible', bool(te.candidate_window(target.pid)))
    te.tap(0x1B)
finally:
    u.PostMessageW(u.FindWindowW('T9Ime.TestTarget', None), 0x10, 0, 0)
