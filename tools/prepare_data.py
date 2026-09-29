"""Assemble the shared Rime data directory and pre-deploy it with rime_deployer.

Output layout (the installer ships this directory as <install>\\data):
    <out>/                 schema/config yaml, lua/, opencc/, en_dicts/, ...
    <out>/build/           pre-built *.bin and compiled *.yaml (prebuilt_data_dir)

All copies use shutil.copy2 so file mtimes are preserved; librime compares
timestamps and would otherwise rebuild the prisms on first run.
Dictionary sources (*.dict.yaml) are deployed but not kept in the output.

Usage: prepare_data.py --rime-ice <dir> --custom <dir> --opencc <dir>
                       --deployer <rime_deployer.exe> --out <dir>
"""
import argparse
import hashlib
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

# Files and directories taken from rime-ice. radical_pinyin is intentionally absent.
RIME_ICE_FILES = [
    'default.yaml', 'rime_ice.schema.yaml', 't9.schema.yaml',
    'melt_eng.schema.yaml', 'melt_eng.dict.yaml', 'rime_ice.dict.yaml',
    'symbols_v.yaml', 'symbols_caps_v.yaml', 'custom_phrase.txt', 'LICENSE',
]
RIME_ICE_DIRS = ['cn_dicts', 'en_dicts', 'lua', 'opencc']
SKIP_DICTS = {'41448.dict.yaml'}  # not imported by rime_ice.dict.yaml
SOURCE_ONLY = ('.dict.yaml',)     # dropped from the runtime output after deploy


def copy_tree(src: Path, dst: Path, skip=frozenset()) -> None:
    for p in src.rglob('*'):
        if p.is_file() and p.name not in skip:
            target = dst / p.relative_to(src)
            target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(p, target)


def input_digest(paths: list[Path], extra: str) -> str:
    h = hashlib.sha256(extra.encode())
    for root in paths:
        files = sorted(root.rglob('*')) if root.is_dir() else [root]
        for f in files:
            if f.is_file() and '.git' not in f.parts:
                st = f.stat()
                h.update(f'{f}|{st.st_size}|{st.st_mtime_ns}\n'.encode())
    return h.hexdigest()


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument('--rime-ice', required=True, type=Path)
    ap.add_argument('--custom', required=True, type=Path)
    ap.add_argument('--opencc', required=True, type=Path)
    ap.add_argument('--deployer', required=True, type=Path)
    ap.add_argument('--out', required=True, type=Path)
    args = ap.parse_args()

    stamp = args.out / '.t9ime-data-stamp'
    digest = input_digest([args.rime_ice, args.custom, args.opencc, args.deployer,
                           Path(__file__)], str(args.out))
    if stamp.exists() and stamp.read_text() == digest:
        print('rime data up to date')
        return 0

    with tempfile.TemporaryDirectory(prefix='t9ime-data-') as tmp:
        stage = Path(tmp) / 'shared'
        user = Path(tmp) / 'user'
        stage.mkdir()
        user.mkdir()
        for name in RIME_ICE_FILES:
            shutil.copy2(args.rime_ice / name, stage / name)
        for d in RIME_ICE_DIRS:
            copy_tree(args.rime_ice / d, stage / d, SKIP_DICTS)
        copy_tree(args.opencc, stage / 'opencc')
        copy_tree(args.custom, stage)

        print(f'deploying with {args.deployer} ...', flush=True)
        r = subprocess.run([str(args.deployer), '--build', str(user), str(stage),
                            str(stage / 'build')], cwd=args.deployer.parent,
                           capture_output=True, text=True, encoding='utf-8', errors='replace')
        if r.returncode != 0:
            sys.stderr.write(r.stdout + r.stderr)
            return r.returncode

        if args.out.exists():
            shutil.rmtree(args.out)
        args.out.mkdir(parents=True)
        for p in stage.rglob('*'):
            if p.is_file() and not p.name.endswith(SOURCE_ONLY):
                target = args.out / p.relative_to(stage)
                target.parent.mkdir(parents=True, exist_ok=True)
                shutil.copy2(p, target)

    stamp.write_text(digest)
    print(f'rime data -> {args.out}')
    return 0


if __name__ == '__main__':
    sys.exit(main())
