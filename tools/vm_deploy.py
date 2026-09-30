"""Deploy a test package and the e2e scripts into the Windows 7 VM, replacing
any registered T9Ime build, and start the host.

    py -3 tools/vm_deploy.py [--milestone M4] [--old-dir D:\\Works\\T9Ime-M4-test]

Guest layout: C:\\t9test\\pkg-<n> (package; a new directory each time because
the previous DLLs stay loaded until the guest restarts), C:\\t9test\\e2e.
"""
import argparse
import time
from pathlib import Path

import vm

ROOT = Path(__file__).resolve().parent.parent


def unzip_in_guest(guest_zip: str, guest_dir: str) -> None:
    vm.cmd(f'powershell -NoProfile -Command "$s=New-Object -ComObject Shell.Application; '
           f'New-Item -ItemType Directory -Force {guest_dir} | Out-Null; '
           f"$s.NameSpace('{guest_dir}').CopyHere($s.NameSpace('{guest_zip}').Items(), 20)\"")


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument('--milestone', default='M4')
    ap.add_argument('--old-dir', action='append', default=[], help='previously registered package directory')
    args = ap.parse_args()

    # Unregister what is registered now (paths from the registry) and stop the host.
    _, reg = vm.cmd(r'reg query "HKCR\CLSID\{BB2F3BA4-3B7A-414A-A98F-08C100E5447E}\InprocServer32" /ve & '
                    r'reg query "HKCR\Wow6432Node\CLSID\{BB2F3BA4-3B7A-414A-A98F-08C100E5447E}\InprocServer32" /ve')
    dlls = [line.split('REG_SZ')[-1].strip() for line in reg.splitlines() if 'REG_SZ' in line]
    for d in args.old_dir:
        dlls += [d + r'\x64\T9Tip.dll', d + r'\x86\T9Tip.dll']
    lines = ['taskkill /im T9Host.exe /f']
    for dll in dict.fromkeys(dlls):
        regsvr = r'%SystemRoot%\SysWOW64\regsvr32.exe' if r'\x86' in dll else r'%SystemRoot%\System32\regsvr32.exe'
        lines.append(f'if exist "{dll}" {regsvr} /s /u "{dll}"')
    print(vm.cmd(' & '.join(lines))[1].strip())

    # Copy and unpack the package into a fresh directory.
    stamp = time.strftime('%m%d%H%M%S')
    pkg = f'{vm.GUEST_DIR}\\pkg-{stamp}'
    vm.vmrun('CopyFileFromHostToGuest', vm.VMX, str(ROOT / 'dist' / args.milestone / f'T9Ime-{args.milestone}-test.zip'),
             f'{vm.GUEST_DIR}\\pkg.zip')
    unzip_in_guest(f'{vm.GUEST_DIR}\\pkg.zip', pkg)

    # e2e scripts
    vm.vmrun('createDirectoryInGuest', vm.VMX, f'{vm.GUEST_DIR}\\e2e', check=False)
    for f in (ROOT / 'tests' / 'e2e').glob('*.py'):
        vm.vmrun('CopyFileFromHostToGuest', vm.VMX, str(f), f'{vm.GUEST_DIR}\\e2e\\{f.name}')

    # Register the new build and start the host.
    code, out = vm.cmd(f'{r"%SystemRoot%\System32\regsvr32.exe"} /s "{pkg}\\x64\\T9Tip.dll" && '
                       f'{r"%SystemRoot%\SysWOW64\regsvr32.exe"} /s "{pkg}\\x86\\T9Tip.dll" && echo registered')
    print(out.strip())
    vm.vmrun('runProgramInGuest', vm.VMX, '-noWait', '-activeWindow', '-interactive', f'{pkg}\\x64\\T9Host.exe',
             check=False)
    (ROOT / 'out').mkdir(exist_ok=True)
    (ROOT / 'out' / 'vm_pkg.txt').write_text(pkg, encoding='utf-8')
    print(f'deployed to {pkg}')
    return 0 if 'registered' in out else 1


if __name__ == '__main__':
    raise SystemExit(main())
