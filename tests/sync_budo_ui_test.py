#!/usr/bin/env python3
"""Test public and optional private budo-ui copy synchronization."""

from contextlib import redirect_stderr, redirect_stdout
import importlib.util
import io
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch


ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location(
    "sync_budo_ui", ROOT / "scripts" / "sync-budo-ui.py"
)
assert SPEC is not None and SPEC.loader is not None
sync = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(sync)


class SyncBudoUiTest(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory()
        self.addCleanup(temporary.cleanup)
        self.root = Path(temporary.name)
        self.source = self.root / "examples" / "budo-ui" / "ui"
        (self.source / "lib").mkdir(parents=True)
        (self.source / "budo-ui.js").write_text(
            '/// <reference path="../../budo.d.ts" />\n'
        )
        (self.source / "lib" / "core.js").write_text(
            '/// <reference path="../../../budo.d.ts" />\n'
        )
        (self.source / "README.md").write_text("Budo UI\n")
        (self.source / "assets").mkdir()
        (self.source / "assets" / "fixture.bin").write_bytes(b"\x00\xff\r\n")
        patches = patch.multiple(sync, ROOT=self.root, SOURCE=self.source)
        patches.start()
        self.addCleanup(patches.stop)

    def add_copy(self, directory, app):
        ui = self.root / directory / app / "ui"
        ui.mkdir(parents=True)
        (ui / "budo-ui.js").write_text("stale\n")
        return ui

    def run_sync(self, check=False):
        error = io.StringIO()
        with patch("sys.argv", ["sync-budo-ui.py"] + (["--check"] if check else [])):
            with redirect_stdout(io.StringIO()), redirect_stderr(error):
                result = sync.main()
        return result, error.getvalue()

    def test_public_only_checkout_syncs_without_creating_private_directory(self):
        ui = self.add_copy("examples", "public-app")
        self.assertEqual(sync.copies(), [ui])
        self.assertEqual(self.run_sync()[0], 0)
        self.assertEqual(self.run_sync(check=True), (0, ""))
        self.assertFalse((self.root / "examples-private").exists())

    def test_private_drift_is_detected_and_all_copies_are_refreshed(self):
        public = self.add_copy("examples", "public-app")
        private = self.add_copy("examples-private", "gallery")
        obsolete = private / "obsolete" / "nested"
        obsolete.mkdir(parents=True)
        stale = obsolete / "old.js"
        stale.write_text("obsolete\n")
        (private / "empty").mkdir()
        self.assertEqual(set(sync.copies()), {public, private})
        result, error = self.run_sync(check=True)
        self.assertEqual(result, 1)
        self.assertIn("examples-private/gallery/ui/budo-ui.js differs", error)
        self.assertIn("examples-private/gallery/ui/obsolete/nested/old.js is not part", error)
        self.assertIn("examples-private/gallery/ui/empty/ is not part", error)
        self.assertEqual((private / "budo-ui.js").read_text(), "stale\n")
        self.assertTrue(stale.exists(), "--check must not modify files")
        self.assertEqual(self.run_sync()[0], 0)
        self.assertFalse(stale.exists())
        self.assertFalse((private / "obsolete").exists())
        self.assertFalse((private / "empty").exists())
        self.assertEqual(self.run_sync(check=True), (0, ""))
        for ui in (public, private):
            self.assertEqual(
                (ui / "budo-ui.js").read_text(),
                '/// <reference path="../../budo.d.ts" />\n',
            )
            self.assertEqual(
                (ui / "lib" / "core.js").read_text(),
                '/// <reference path="../../../budo.d.ts" />\n',
            )
            self.assertEqual((ui / "README.md").read_text(), "Budo UI\n")
            source_files = {path.relative_to(self.source): path.read_bytes()
                            for path in self.source.rglob("*") if path.is_file()}
            copied_files = {path.relative_to(ui): path.read_bytes()
                            for path in ui.rglob("*") if path.is_file()}
            self.assertEqual(copied_files, source_files)

    def test_missing_canonical_entry_never_clears_copies(self):
        ui = self.add_copy("examples", "public-app")
        (self.source / "budo-ui.js").unlink()
        with self.assertRaises(FileNotFoundError):
            self.run_sync()
        self.assertEqual((ui / "budo-ui.js").read_text(), "stale\n")


if __name__ == "__main__":
    unittest.main()
