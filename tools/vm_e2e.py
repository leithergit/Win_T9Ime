"""Run the end-to-end tests inside the Windows 7 VM (after tools/vm_deploy.py).

    py -3 tools/vm_e2e.py [test ...]      default: all

Clicks are delivered as window messages there (T9IME_CLICK=post): VMware's
absolute pointer moves the cursor back to the host mouse position, so
injected mouse clicks miss. Keyboard input is injected normally.
"""
import sys
from pathlib import Path

import vm

ROOT = Path(__file__).resolve().parent.parent
TESTS = ['panel', 'tip', 'push', 'autoshow', 'switch', 'ctl', 'imm']


def main() -> int:
    pkg = (ROOT / 'out' / 'vm_pkg.txt').read_text(encoding='utf-8').strip()
    for f in (ROOT / 'tests' / 'e2e').glob('*.py'):  # latest scripts
        vm.vmrun('CopyFileFromHostToGuest', vm.VMX, str(f), f'{vm.GUEST_DIR}\\e2e\\{f.name}')
    host, target = f'{pkg}\\x64\\T9Host.exe', f'{pkg}\\x64\\test_target.exe'
    work = f'{vm.GUEST_DIR}\\work'
    args = {
        'panel': f'--host {host} --target {target} --work {work}',
        'tip': f'--host {host} --target {target}',
        'push': f'--host {host} --target {target} --work {work}',
        'autoshow': f'--host {host} --target {target} --work {work}',
        'switch': f'--host {host} --target {target} --work {work}',
        'ctl': f'--host {host} --target {target} --ctl {pkg}\\x64\\t9ctl.exe --work {work}',
        'imm': f'--host {host} --target {target} --work {work}',
    }
    failed = []
    for name in sys.argv[1:] or TESTS:
        _, out = vm.cmd(f'set T9IME_CLICK=post& {vm.GUEST_DIR}\\py\\python.exe '
                        f'{vm.GUEST_DIR}\\e2e\\{name}_e2e.py {args[name]}', timeout=900)
        verdict = 'ALL PASSED' in out
        print(f'===== {name}: {"PASS" if verdict else "FAIL"}')
        for line in out.splitlines():
            if line.startswith(('FAIL', 'SKIP')) or 'Error' in line or 'Traceback' in line:
                print('   ', line)
        if not verdict:
            failed.append(name)
            (ROOT / 'out' / f'vm_{name}.log').write_text(out, encoding='utf-8')
    print('ALL PASSED' if not failed else f'FAILED: {", ".join(failed)} (logs in out/vm_*.log)')
    return 1 if failed else 0


if __name__ == '__main__':
    raise SystemExit(main())
