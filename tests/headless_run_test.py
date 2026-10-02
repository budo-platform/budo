#!/usr/bin/env python3
"""Applications that never use graphics run without a window and exit on their own.

Usage: headless_run_test.py <budo executable> <scratch directory>
"""

import shutil
import subprocess
import sys
from pathlib import Path

CASES = [
    # name, entrypoint, source, expected exit status, expected output, forbidden output
    ("plain", "main.js", 'console.log("hello headless");', 0, ["hello headless"], []),
    ("timer", "main.js",
     'console.log("start"); sys.timer.once(200, () => console.log("timer fired"));',
     0, ["start", "timer fired"], []),
    ("promise", "main.js",
     'Promise.resolve().then(() => console.log("job ran"));', 0, ["job ran"], []),
    ("exit_code", "main.js",
     'console.log("before"); sys.exit(3); console.log("NOT REACHED");',
     3, ["before"], ["NOT REACHED", "Exception"]),
    ("exit_in_timer", "main.js",
     'sys.timer.every(50, () => { console.log("tick"); sys.exit(6); });', 6, ["tick"], ["Exception"]),
    ("load_error", "main.js", 'throw new Error("boom");', 1, ["boom"], []),
    ("lua_plain", "main.lua", 'print("lua headless")', 0, ["lua headless"], []),
    ("lua_exit", "main.lua", 'print("lua before"); sys.exit(5)', 5, ["lua before"], []),
    ("wasm_init_only", "main.wat",
     '(module (import "env" "log_int" (func $log (param i32)))'
     ' (func (export "init") i32.const 42 call $log))', 0, ["42"], []),
    ("wasm_exit", "main.wat",
     '(module (import "env" "app_exit" (func $exit (param i32)))'
     ' (func (export "init") i32.const 9 call $exit))', 9, [], ["init function failed"]),
]

# QuickJS-NG makes sys.exit uncatchable; Bellard's QuickJS cannot.
UNCATCHABLE = ("exit_caught", "main.js",
               'try { sys.exit(4); } catch (e) { console.log("CAUGHT"); } console.log("NOT REACHED");',
               4, [], ["CAUGHT", "NOT REACHED"])


def run_case(budo, scratch, name, entrypoint, source, status, expected, forbidden):
    app = scratch / name
    shutil.rmtree(app, ignore_errors=True)
    app.mkdir(parents=True)
    (app / entrypoint).write_text(source + "\n", encoding="utf-8")
    try:
        result = subprocess.run([budo, "run", str(app)], capture_output=True, text=True, timeout=30)
    except subprocess.TimeoutExpired:
        return [f"{name}: did not exit within 30 s (a window-less app must end on its own)"]
    output = result.stdout + result.stderr
    errors = []
    if result.returncode != status:
        errors.append(f"{name}: exit status {result.returncode}, expected {status}")
    errors += [f"{name}: missing output {text!r}" for text in expected if text not in output]
    errors += [f"{name}: unexpected output {text!r}" for text in forbidden if text in output]
    if errors:
        errors.append(f"{name}: output was:\n{output}")
    return errors


def main():
    budo, scratch = sys.argv[1], Path(sys.argv[2])
    cases = list(CASES)
    if "--quickjs-ng" in sys.argv[3:]:
        cases.append(UNCATCHABLE)
    errors = []
    for case in cases:
        errors += run_case(budo, scratch, *case)
    if errors:
        print("\n".join(errors), file=sys.stderr)
        return 1
    print(f"headless_run_test: ok ({len(cases)} cases)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
