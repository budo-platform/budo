#!/usr/bin/env python3
"""Keep vendored copies byte-identical to examples/budo-ui/ui.

Budo projects cannot import files outside their own directory, so an example
that uses budo-ui carries a copy in its `ui/` folder, laid out like
`budo init --template ui`: ui/budo-ui.js, ui/lib/**, ui/README.md.
Every file is copied unchanged; declarations belong to the app's configuration.

Usage:
  scripts/sync-budo-ui.py           # refresh public and present private example copies
  scripts/sync-budo-ui.py --check   # exit 1 when a copy differs (CTest)
"""

import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
SOURCE = ROOT / "examples" / "budo-ui" / "ui"


def library():
    """Every published path in ui/ -> unchanged source bytes."""
    if not (SOURCE / "budo-ui.js").is_file():
        raise FileNotFoundError(f"Missing canonical budo-ui entry: {SOURCE / 'budo-ui.js'}")
    return {path.relative_to(SOURCE).as_posix(): path.read_bytes()
            for path in sorted(SOURCE.rglob("*")) if path.is_file()}


def copies():
    """Every public or private example ui/ folder that holds budo-ui."""
    return sorted(path.parent
                  for directory in ("examples", "examples-private")
                  for path in (ROOT / directory).glob("*/ui/budo-ui.js")
                  if path.parent != SOURCE)


def main():
    check = "--check" in sys.argv[1:]
    expected = library()
    directories = {parent.as_posix() for name in expected
                   for parent in Path(name).parents if parent != Path(".")}
    problems = []
    for ui in copies():
        present = {path.relative_to(ui).as_posix() for path in ui.rglob("*") if path.is_file()}
        for name, data in expected.items():
            target = ui / name
            if target.exists() and target.read_bytes() == data:
                continue
            if check:
                problems.append(f"{target.relative_to(ROOT)} differs from examples/budo-ui/ui")
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
        for path in sorted(ui.rglob("*"), reverse=True):
            if path.is_dir() and path.relative_to(ui).as_posix() not in directories:
                if check:
                    problems.append(f"{path.relative_to(ROOT)}/ is not part of budo-ui")
                else:
                    path.rmdir()
                    print(f"removed {path.relative_to(ROOT)}/")
    if problems:
        print("\n".join(problems) + "\nRun scripts/sync-budo-ui.py to refresh the copies.", file=sys.stderr)
        return 1
    if check:
        print(f"budo-ui copies: ok ({len(copies())})")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
