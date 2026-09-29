"""Run a t9repl script and compare its output with <script>.expected.

--update rewrites the snapshot (review the diff before committing it).
"""
import argparse
import difflib
import subprocess
import sys
from pathlib import Path


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument('--repl', required=True)
    ap.add_argument('--data', required=True)
    ap.add_argument('--user', required=True)
    ap.add_argument('--script', required=True, type=Path)
    ap.add_argument('--update', action='store_true')
    args = ap.parse_args()

    r = subprocess.run([str(Path(args.repl).resolve()), '--data', args.data, '--user', args.user, '--fresh',
                        '--script', str(args.script)], capture_output=True)
    actual = r.stdout.decode('utf-8').replace('\r\n', '\n')
    if r.returncode != 0:
        sys.stderr.write(actual + r.stderr.decode('utf-8', 'replace'))
        return r.returncode

    expected_path = args.script.with_suffix('.expected')
    if args.update:
        expected_path.write_text(actual, encoding='utf-8', newline='\n')
        print(f'updated {expected_path}')
        return 0
    expected = expected_path.read_text(encoding='utf-8') if expected_path.exists() else ''
    if actual == expected:
        return 0
    diff = difflib.unified_diff(expected.splitlines(True), actual.splitlines(True),
                                str(expected_path), 'actual')
    sys.stdout.buffer.write(''.join(diff).encode('utf-8'))
    return 1


if __name__ == '__main__':
    sys.exit(main())
