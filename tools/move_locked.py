"""Pre-link helper: a registered T9Tip.dll is loaded by running applications
and cannot be overwritten, but it can be renamed. Moves <file> aside to
<file>.old-<n> and deletes earlier leftovers that are no longer in use."""
import sys
import time
from pathlib import Path

target = Path(sys.argv[1])
for old in target.parent.glob(target.name + '.old-*'):
    try:
        old.unlink()
    except OSError:
        pass  # still loaded somewhere
if target.exists():
    try:
        target.unlink()
    except OSError:
        target.rename(target.with_name(f'{target.name}.old-{int(time.time() * 1000)}'))
