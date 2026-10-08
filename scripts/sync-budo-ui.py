#!/usr/bin/env python3
"""Keep vendored copies of budo-ui identical to examples/budo-ui.

Budo projects cannot import files outside their own directory, so an example
that uses budo-ui carries a copy in its `ui/` folder, laid out like
`budo init --template ui`: ui/budo-ui.js, ui/lib/**, ui/README.md, with
`/// <reference path>` lines one level shallower.

Usage:
  scripts/sync-budo-ui.py           # refresh every examples/*/ui copy
  scripts/sync-budo-ui.py --check   # exit 1 when a copy differs (CTest)
"""

import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
SOURCE = ROOT / "examples" / "budo-ui"
REFERENCE = re.compile(rb'^(/// <reference path=")\.\./', re.MULTILINE)


def library():
    """Published path in ui/ -> source bytes, with references rewritten."""
    files = {"budo-ui.js": SOURCE / "budo-ui.js", "README.md": SOURCE / "README.md"}
    for path in sorted((SOURCE / "lib").rglob("*.js")):
        files[path.relative_to(SOURCE).as_posix()] = path
    return {name: REFERENCE.sub(rb"\1", path.read_bytes()) for name, path in files.items()}


def copies():
    """Every examples/<app>/ui folder that holds budo-ui."""
    return sorted(path.parent for path in (ROOT / "examples").glob("*/ui/budo-ui.js")
                  if path.parent.parent != SOURCE)


def main():
    check = "--check" in sys.argv[1:]
    expected = library()
    problems = []
    for ui in copies():
        present = {path.relative_to(ui).as_posix() for path in ui.rglob("*") if path.is_file()}
        for name, data in expected.items():
            target = ui / name
            if target.exists() and target.read_bytes() == data:
                continue
            if check:
                problems.append(f"{target.relative_to(ROOT)} differs from examples/budo-ui")
            else:
                target.parent.mkdir(parents=True, exist_ok=True)
                target.write_bytes(data)
                print(f"updated {target.relative_to(ROOT)}")
        for name in sorted(present - set(expected)):
            if check:
                problems.append(f"{(ui / name).relative_to(ROOT)} is not part of budo-ui")
            else:
                (ui / name).unlink()
                print(f"removed {(ui / name).relative_to(ROOT)}")
    if problems:
        print("\n".join(problems) + "\nRun scripts/sync-budo-ui.py to refresh the copies.", file=sys.stderr)
        return 1
    if check:
        print(f"budo-ui copies: ok ({len(copies())})")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
