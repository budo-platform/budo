#!/usr/bin/env python3
"""Validate that every API contract has an explicit conformance owner."""

import json
import re
import sys
from pathlib import Path


MODES = {"normalized_trace", "backend_sequence"}
RUNTIMES = {"javascript", "lua", "wasmtime", "browser_wasm"}


def main():
    root = Path(sys.argv[1]).resolve() if len(sys.argv) > 1 else Path(__file__).resolve().parents[1]
    matrix = json.loads((root / "api" / "conformance-matrix.json").read_text(encoding="utf-8"))
    contract_documents = {
        document["subsystem"]: document
        for path in (root / "api" / "contracts").glob("*.json")
        for document in [json.loads(path.read_text(encoding="utf-8"))]
    }
    contracts = set(contract_documents)
    declared = set(matrix.get("subsystems", {}))
    errors = []
    if matrix.get("version") != 1:
        errors.append("conformance matrix version must be 1")
    if contracts != declared:
        missing = sorted(contracts - declared)
        extra = sorted(declared - contracts)
        if missing:
            errors.append("missing subsystems: " + ", ".join(missing))
        if extra:
            errors.append("unknown subsystems: " + ", ".join(extra))

    cmake = (root / "CMakeLists.txt").read_text(encoding="utf-8")
    ctest_names = set(re.findall(r"add_test\s*\(\s*NAME\s+([A-Za-z0-9_]+)", cmake))
    for subsystem, details in matrix.get("subsystems", {}).items():
        mode = details.get("mode")
        if mode not in MODES:
            errors.append(f"{subsystem}: unsupported mode {mode!r}")
            continue
        tests = ([details.get("test")] if mode == "normalized_trace"
                 else details.get("tests"))
        if not isinstance(tests, list) or not tests or any(
                not isinstance(test, str) or not test for test in tests):
            errors.append(f"{subsystem}: tests must be non-empty")
        else:
            for test in tests:
                if test not in ctest_names:
                    errors.append(f"{subsystem}: unknown CTest {test}")
        if mode == "normalized_trace":
            runtimes = details.get("runtimes")
            operations = details.get("operations")
            if not isinstance(runtimes, list) or len(runtimes) < 2 or any(
                    runtime not in RUNTIMES for runtime in runtimes):
                errors.append(f"{subsystem}: normalized runtimes are invalid")
            if not isinstance(operations, list) or not operations or any(
                    not isinstance(operation, str) or not operation
                    for operation in operations):
                errors.append(f"{subsystem}: normalized operations are required")
        elif not isinstance(details.get("reason"), str) or not details["reason"]:
            errors.append(f"{subsystem}: backend_sequence requires a reason")

    if errors:
        for error in errors:
            print(f"conformance_matrix_test: {error}", file=sys.stderr)
        return 1
    print(f"conformance matrix covers {len(contracts)} subsystems")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
