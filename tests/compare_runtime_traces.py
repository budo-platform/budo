#!/usr/bin/env python3
"""Run fixture executables and compare their normalized JSON trace records."""

import argparse
import json
import subprocess
import sys


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--runtime", action="append", required=True,
                        help="runtime=executable")
    parser.add_argument("--arg", action="append", default=[],
                        help="runtime=argument")
    parser.add_argument("--required", default="javascript,lua,wasmtime")
    args = parser.parse_args()
    records = {}
    outputs = []
    runtime_args = {}
    for specification in args.arg:
        runtime, separator, value = specification.partition("=")
        if not separator or not runtime:
            parser.error(f"invalid --arg value: {specification}")
        runtime_args.setdefault(runtime, []).append(value)
    for specification in args.runtime:
        runtime, separator, executable = specification.partition("=")
        if not separator or not runtime or not executable:
            parser.error(f"invalid --runtime value: {specification}")
        completed = subprocess.run([executable] + runtime_args.get(runtime, []), text=True,
                                   stdout=subprocess.PIPE,
                                   stderr=subprocess.PIPE)
        if completed.returncode != 0:
            sys.stderr.write(completed.stdout)
            sys.stderr.write(completed.stderr)
            return completed.returncode
        outputs.append(completed.stdout)
        for line in completed.stdout.splitlines():
            if not line.startswith("{"):
                continue
            record = json.loads(line)
            declared_runtime = record.get("runtime")
            if declared_runtime != runtime and not (
                    runtime == "managed" and declared_runtime in ("javascript", "lua")):
                continue
            key = (declared_runtime, record["operation"])
            if key in records:
                raise ValueError(f"duplicate trace record: {key}")
            records[key] = (
                record.get("result"),
                record.get("errorKind"),
                record.get("errorCode"),
            )

    required = args.required.split(",")
    operations = sorted({operation for _, operation in records})
    errors = []
    for operation in operations:
        values = []
        for runtime in required:
            key = (runtime, operation)
            if key not in records:
                errors.append(f"missing {runtime} trace for {operation}")
            else:
                values.append((runtime, records[key]))
        if values and any(value != values[0][1] for _, value in values[1:]):
            errors.append(
                f"trace mismatch for {operation}: " +
                ", ".join(f"{runtime}={value!r}" for runtime, value in values)
            )
    if not operations:
        errors.append("no normalized trace records found")
    if errors:
        for error in errors:
            print(f"compare_runtime_traces: {error}", file=sys.stderr)
        return 1
    for output in outputs:
        for line in output.splitlines():
            if line.startswith("{"):
                print(line)
    print(f"compared {len(operations)} operations across {len(required)} runtimes")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
