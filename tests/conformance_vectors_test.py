#!/usr/bin/env python3
"""Validate generated operation vectors against contracts and test ownership."""

import json
import sys
from pathlib import Path


def export_names(operation, runtime):
    exported = operation.get("exports", {}).get(runtime)
    if exported is None:
        return None
    values = exported if isinstance(exported, list) else [exported]
    return [value if isinstance(value, str) else value["name"] for value in values]


def main():
    root = Path(sys.argv[1])
    matrix = json.loads((root / "api/conformance-matrix.json").read_text())
    generated = json.loads(
        (root / "api/generated/conformance-vectors.json").read_text())
    vectors = generated.get("vectors", [])
    actual = {(vector["subsystem"], vector["operation"]): vector for vector in vectors}
    assert len(actual) == len(vectors), "duplicate generated conformance vector"

    expected_keys = set()
    for subsystem, ownership in matrix.get("subsystems", {}).items():
        if ownership.get("mode") != "normalized_trace":
            continue
        contract = json.loads(
            (root / f"api/contracts/{subsystem}.json").read_text())
        for operation in contract.get("operations", []):
            key = (subsystem, operation["name"])
            expected_keys.add(key)
            assert key in actual, f"missing generated conformance vector: {key}"
            vector = actual[key]
            assert vector["signature"] == operation["signature"], key
            assert vector["runtimes"] == ownership["runtimes"], key
            assert vector["test"] == ownership["test"], key
            expected_exports = {}
            for runtime in ownership["runtimes"]:
                names = export_names(operation, runtime)
                if names is not None:
                    expected_exports[runtime] = names
            assert vector["exports"] == expected_exports, key

    unexpected = set(actual) - expected_keys
    assert not unexpected, f"unexpected generated conformance vectors: {sorted(unexpected)}"
    print(f"validated {len(expected_keys)} generated conformance vectors")


if __name__ == "__main__":
    main()
