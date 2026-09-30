"""Drive the Windows 7 test VM from the host with VMware's vmrun.

    py -3 tools/vm.py run  <program> [args...]      run in the guest desktop, wait
    py -3 tools/vm.py cmd  "<command line>" [out]   run as a batch file in the guest
                                                    desktop; output captured
    py -3 tools/vm.py put  <host file> <guest file>
    py -3 tools/vm.py get  <guest file> <host file>
    py -3 tools/vm.py shot <host png>
    py -3 tools/vm.py ps

Configuration (environment): T9IME_VMX (default: the Windows 7 x64 VM),
T9IME_VM_USER (Administrator), T9IME_VM_PASS_FILE (tests/vm_pass.txt, git-ignored).
The password is only passed to vmrun, never printed.
"""
import os
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
VMRUN = r'C:\Program Files (x86)\VMware\VMware Workstation\vmrun.exe'
VMX = os.environ.get('T9IME_VMX', r'D:\VirutalMachine\Windows7\Windows 7 x64.vmx')
USER = os.environ.get('T9IME_VM_USER', 'Administrator')
PASS_FILE = Path(os.environ.get('T9IME_VM_PASS_FILE', ROOT / 'tests' / 'vm_pass.txt'))
GUEST_DIR = 'C:\\t9test'


def vmrun(*args, check=True, timeout=None):
    password = PASS_FILE.read_text(encoding='utf-8').splitlines()[0].strip()
    cmd_line = [VMRUN, '-T', 'ws', '-gu', USER, '-gp', password, *args]
    r = subprocess.run(cmd_line, capture_output=True, text=True, timeout=timeout)
    out = r.stdout + r.stderr
    if password:
        out = out.replace(password, '***')
    if check and r.returncode != 0:
        raise SystemExit(f'vmrun {args[0]} failed ({r.returncode}): {out.strip()}')
    return r.returncode, out


def run(program, *args, timeout=600):
    return vmrun('runProgramInGuest', VMX, '-activeWindow', '-interactive', program, *args, check=False,
                 timeout=timeout)


def cmd(command_line, host_out=None, timeout=600):
    """Runs `command_line` as a batch file in the guest desktop; returns
    (exit code, captured output)."""
    vmrun('createDirectoryInGuest', VMX, GUEST_DIR, check=False)
    with tempfile.TemporaryDirectory() as tmp:
        bat = Path(tmp) / 'run.bat'
        bat.write_bytes(('@echo off\r\nchcp 65001 > nul\r\n' + command_line + '\r\n').encode('utf-8'))
        vmrun('CopyFileFromHostToGuest', VMX, str(bat), GUEST_DIR + '\\_run.bat')
        vmrun('deleteFileInGuest', VMX, GUEST_DIR + '\\_out.txt', check=False)
        code, _ = run('C:\\Windows\\System32\\cmd.exe',
                      f'/c {GUEST_DIR}\\_run.bat > {GUEST_DIR}\\_out.txt 2>&1', timeout=timeout)
        local = Path(tmp) / 'out.txt'
        vmrun('CopyFileFromGuestToHost', VMX, GUEST_DIR + '\\_out.txt', str(local), check=False)
        raw = local.read_bytes() if local.exists() else b''
    try:
        text = raw.decode('utf-8')
    except UnicodeDecodeError:
        text = raw.decode('gbk', 'replace')  # tools that ignore the code page
    if host_out:
        Path(host_out).write_text(text, encoding='utf-8')
    return code, text


def main() -> int:
    if len(sys.argv) < 2:
        print(__doc__)
        return 2
    op, rest = sys.argv[1], sys.argv[2:]
    if op == 'run':
        code, out = run(*rest)
        print(out.strip())
        return code
    if op == 'cmd':
        code, out = cmd(rest[0], rest[1] if len(rest) > 1 else None)
        sys.stdout.buffer.write(out.encode('utf-8'))
        return code
    if op == 'put':
        vmrun('CopyFileFromHostToGuest', VMX, rest[0], rest[1])
        return 0
    if op == 'get':
        vmrun('CopyFileFromGuestToHost', VMX, rest[0], rest[1])
        return 0
    if op == 'shot':
        vmrun('captureScreen', VMX, rest[0])
        return 0
    if op == 'ps':
        print(vmrun('listProcessesInGuest', VMX)[1])
        return 0
    print(__doc__)
    return 2


if __name__ == '__main__':
    sys.exit(main())
