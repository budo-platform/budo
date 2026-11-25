#!/usr/bin/env python3
"""Verify pending canvas work can wake a runtime without animation."""

from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
SOURCE = (ROOT / "src/core/managed_runtime.c").read_text()

assert "if (!contexts)\n        return -1.0;" in SOURCE
assert "bool has_animation = runtime_has_animation(kind, contexts);" in SOURCE
assert "bool graphics_pending =" in SOURCE
assert "if (graphics_pending && (!has_animation || idle > 16.0))" in SOURCE

# The old early return made video/image workers impossible to poll when the
# guest had not installed an animation callback.
assert "if (!contexts || !runtime_has_animation(kind, contexts))" not in SOURCE
