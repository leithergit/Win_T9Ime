"""Fail if a PE file cannot load on Windows 7 SP1.

Checks the PE header (subsystem / OS version <= 6.1) and the static import
table against DLLs and functions that do not exist on Windows 7. Newer APIs
must be loaded with GetProcAddress instead.

Usage: check_imports.py <file.exe|file.dll> ...
"""
import struct
import sys
from pathlib import Path

WIN8_DLLS = {
    'shcore.dll', 'dcomp.dll', 'combase.dll', 'windows.ui.dll', 'd3d11_1.dll',
    'vcomp140.dll', 'vcruntime140.dll', 'msvcp140.dll', 'ucrtbase.dll',
}
WIN8_DLL_PREFIXES = ('api-ms-win-', 'ext-ms-win-')
WIN8_FUNCTIONS = {
    # kernel32
    'GetSystemTimePreciseAsFileTime', 'CreateFile2', 'CopyFile2', 'WaitOnAddress',
    'WakeByAddressSingle', 'WakeByAddressAll', 'SetThreadDescription', 'GetThreadDescription',
    'InitializeSynchronizationBarrier', 'EnterSynchronizationBarrier', 'DeleteSynchronizationBarrier',
    'GetOverlappedResultEx', 'PrefetchVirtualMemory', 'GetCurrentPackageId', 'GetCurrentPackageFullName',
    'GetPackageFamilyName', 'SetProcessMitigationPolicy', 'GetProcessMitigationPolicy',
    'GetFirmwareType',
    # user32
    'GetDpiForWindow', 'GetDpiForSystem', 'SetProcessDpiAwarenessContext', 'SetThreadDpiAwarenessContext',
    'GetThreadDpiAwarenessContext', 'GetWindowDpiAwarenessContext', 'AdjustWindowRectExForDpi',
    'GetSystemMetricsForDpi', 'SystemParametersInfoForDpi', 'EnableNonClientDpiScaling',
    'GetCurrentInputMessageSource', 'GetCIMSSM', 'GetPointerInfo', 'GetPointerType',
    'GetPointerFrameInfo', 'GetPointerTouchInfo', 'GetPointerPenInfo', 'EnableMouseInPointer',
    'IsMouseInPointerEnabled', 'SetWindowFeedbackSetting', 'GetWindowFeedbackSetting',
    'RegisterPointerInputTarget', 'LogicalToPhysicalPointForPerMonitorDPI',
    'PhysicalToLogicalPointForPerMonitorDPI', 'SetDisplayAutoRotationPreferences',
    'GetDpiForMonitor', 'SetProcessDpiAwareness', 'GetProcessDpiAwareness',
}


def imports(path: Path):
    d = path.read_bytes()
    pe = struct.unpack_from('<I', d, 0x3c)[0]
    if d[pe:pe + 4] != b'PE\0\0':
        raise ValueError('not a PE file')
    nsec = struct.unpack_from('<H', d, pe + 6)[0]
    optsz = struct.unpack_from('<H', d, pe + 20)[0]
    opt = pe + 24
    magic = struct.unpack_from('<H', d, opt)[0]
    pe32 = magic == 0x10b
    os_ver = struct.unpack_from('<HH', d, opt + 40)
    sub_ver = struct.unpack_from('<HH', d, opt + 48)
    dd = opt + (96 if pe32 else 112)
    sections = [struct.unpack_from('<8sIIII', d, opt + optsz + i * 40) for i in range(nsec)]

    def off(rva):
        for _, vsize, va, rsize, raw in sections:
            if va <= rva < va + max(vsize, rsize):
                return rva - va + raw
        raise ValueError(f'rva {rva:#x} outside sections')

    result = {}
    for dir_index, desc_size in ((1, 20), (13, 32)):  # import, delay import
        rva = struct.unpack_from('<I', d, dd + dir_index * 8)[0]
        if not rva:
            continue
        o = off(rva)
        while True:
            if dir_index == 1:
                olt, _, _, name, ft = struct.unpack_from('<IIIII', d, o)
                thunk = olt or ft
            else:
                _, name, _, _, thunk, _, _, _ = struct.unpack_from('<IIIIIIII', d, o)
            if name == 0:
                break
            dll = d[off(name):].split(b'\0')[0].decode()
            funcs = []
            t = off(thunk)
            while True:
                v = struct.unpack_from('<I' if pe32 else '<Q', d, t)[0]
                t += 4 if pe32 else 8
                if v == 0:
                    break
                by_ordinal = v & (0x80000000 if pe32 else 1 << 63)
                if not by_ordinal:
                    funcs.append(d[off(v & 0x7fffffff) + 2:].split(b'\0')[0].decode())
            result.setdefault((dll, dir_index == 13), []).extend(funcs)
            o += desc_size
    return os_ver, sub_ver, result


def check(path: Path) -> list[str]:
    problems = []
    os_ver, sub_ver, table = imports(path)
    for label, ver in (('OS version', os_ver), ('subsystem version', sub_ver)):
        if ver > (6, 1):
            problems.append(f'{label} {ver[0]}.{ver[1]} > 6.1')
    for (dll, delayed), funcs in table.items():
        if delayed:
            continue  # delay-loaded imports are resolved on first call
        low = dll.lower()
        if low in WIN8_DLLS or low.startswith(WIN8_DLL_PREFIXES):
            problems.append(f'imports {dll}')
        for f in funcs:
            if f in WIN8_FUNCTIONS:
                problems.append(f'imports {dll}!{f}')
    return problems


def main() -> int:
    failed = False
    for arg in sys.argv[1:]:
        p = Path(arg)
        problems = check(p)
        if problems:
            failed = True
            print(f'{p.name}: NOT Windows 7 compatible')
            for msg in problems:
                print(f'  {msg}')
        else:
            print(f'{p.name}: ok')
    return 1 if failed else 0


if __name__ == '__main__':
    sys.exit(main())
