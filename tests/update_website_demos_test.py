#!/usr/bin/env python3
"""Test generation of the website playground example selector."""

import importlib.util
import json
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch


ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location(
    "update_website_demos", ROOT / "scripts" / "update-website-demos.py"
)
assert SPEC is not None and SPEC.loader is not None
generator = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(generator)


class PlaygroundOptionsTest(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory()
        self.addCleanup(temporary.cleanup)
        root = Path(temporary.name)
        self.demos = root / "demos-playground"
        self.demos.mkdir()
        self.page = root / "playground.html"
        self.prefix = '<select id="example-select">\n            '
        self.suffix = '\n        </select>\n<p>Keep this</p>\n'
        self.page.write_text(
            self.prefix + generator.PLAYGROUND_START_MARKER
            + '\n<option value="obsolete">Old</option>\n'
            + generator.PLAYGROUND_END_MARKER + self.suffix,
            encoding="utf-8",
        )
        patches = patch.multiple(
            generator, ROOT=root, PLAYGROUND_DEMOS_DIR=self.demos,
            PLAYGROUND_HTML=self.page,
        )
        patches.start()
        self.addCleanup(patches.stop)

    def add_demo(self, slug, entry="main.js", metadata=None):
        demo = self.demos / slug
        demo.mkdir()
        (demo / entry).touch()
        if metadata is not None:
            (demo / "app.json").write_text(json.dumps(metadata), encoding="utf-8")
        return demo

    def test_discovers_sorts_and_escapes_all_demo_types(self):
        self.add_demo("shader_demo", metadata={"name": "Shader Demo"})
        self.add_demo('a"&demo', metadata={"title": '<Demo> & "title"'})
        for slug, entry in (
            ("b_lua", "main.lua"), ("c_typescript", "main.ts"),
            ("d_wat", "main.wat"), ("e_wasm", "main.wasm"),
            ("f_metadata", "app.json"),
        ):
            self.add_demo(slug, entry, {"name": slug} if entry == "app.json" else None)
        self.add_demo("_private")
        self.add_demo(".hidden")
        (self.demos / "assets").mkdir()
        (self.demos / "not-a-directory.js").touch()

        generator.update_playground()
        result = self.page.read_text(encoding="utf-8")
        self.assertEqual(result.count("<option "), 4)
        self.assertNotIn("d_wat", result)
        self.assertNotIn("e_wasm", result)
        self.assertNotIn("f_metadata", result)
        self.assertIn('value="a&quot;&amp;demo">&lt;Demo&gt; &amp; &quot;title&quot;', result)
        self.assertIn('value="shader_demo" selected>Shader Demo</option>', result)
        self.assertIn('value="b_lua">B Lua</option>', result)
        self.assertLess(result.index('value="a'), result.index('value="b_lua"'))
        self.assertNotIn("obsolete", result)
        self.assertNotIn("_private", result)
        self.assertNotIn(".hidden", result)
        self.assertTrue(result.startswith(self.prefix))
        self.assertTrue(result.endswith(self.suffix))
        generator.update_playground()
        self.assertEqual(self.page.read_text(encoding="utf-8"), result)

    def test_removes_deleted_demos(self):
        demo = self.add_demo("temporary")
        generator.update_playground()
        (demo / "main.js").unlink()
        demo.rmdir()
        generator.update_playground()
        self.assertNotIn("<option ", self.page.read_text(encoding="utf-8"))

    def test_missing_directory_produces_empty_options(self):
        self.demos.rmdir()
        generator.update_playground()
        self.assertNotIn("<option ", self.page.read_text(encoding="utf-8"))

    def test_playground_manifest_lists_demo_files(self):
        demo = self.add_demo("canvas_texture", metadata={"name": "Canvas <Texture>"})
        (demo / "assets").mkdir()
        (demo / "assets" / "image.png").touch()
        (demo / "dist").mkdir()
        (demo / "dist" / "app.apk").touch()
        self.assertEqual(generator.generate_playground_manifests(), 1)
        page = (demo / "index.html").read_text(encoding="utf-8")
        self.assertIn("Canvas &lt;Texture&gt;", page)
        start = page.index('type="application/json">') + len('type="application/json">')
        files = json.loads(page[start:page.index("</script>", start)])
        self.assertEqual([item["path"] for item in files], ["app.json", "assets/image.png", "main.js"])
        self.assertTrue(files[2]["text"])
        self.assertTrue(files[1]["image"])
        self.assertEqual(generator.generate_playground_manifests(), 1)
        self.assertEqual((demo / "index.html").read_text(encoding="utf-8"), page)

    def test_missing_markers_fail_without_changing_page(self):
        original = '<select id="example-select"></select>'
        self.page.write_text(original, encoding="utf-8")
        with self.assertRaisesRegex(RuntimeError, "playground demo markers not found"):
            generator.update_playground()
        self.assertEqual(self.page.read_text(encoding="utf-8"), original)


if __name__ == "__main__":
    unittest.main()
