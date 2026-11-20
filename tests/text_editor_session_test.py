#!/usr/bin/env python3
"""Verify session restoration and document transitions in Budo."""

import shutil
import subprocess
import sys
import tempfile
from pathlib import Path


def main():
    repo = Path(__file__).resolve().parents[1]
    budo = Path(sys.argv[1]).resolve() if len(sys.argv) > 1 else repo / "build/budo"
    source = repo / "examples/text-editor"
    entry = (source / "main.js").read_text().removesuffix("sys.animation.requestFrame(frame);\n")
    entry += """
function expect(ok, message) {
    if (!ok) {
        console.log(message);
        sys.exit(1);
    }
}
function expectError(action, pattern) {
    try { action(); } catch (error) {
        expect(pattern.test(error.message), error.message);
        return;
    }
    expect(false, 'Expected ' + pattern);
}
"""
    with tempfile.TemporaryDirectory(prefix="budo-editor-session-") as directory:
        app = Path(directory) / "app"
        root = Path(directory) / "documents"
        shutil.copytree(source, app, ignore=shutil.ignore_patterns("*.db", "*.db-*"))
        root.mkdir()
        (root / "a's notes.txt").write_text("original bytes")
        folder = root / "browsed folder"
        folder.mkdir()

        def launch(case, assertions):
            (app / "main.js").write_text(entry + assertions + '\nconsole.log("session check passed");\nsys.exit(0);\n')
            result = subprocess.run(
                [str(budo), "run", str(app), "--file-root", str(root)],
                capture_output=True, text=True, timeout=15,
            )
            if result.returncode or "session check passed" not in result.stdout:
                raise AssertionError(f"{case}: {result.stdout}{result.stderr}")

        launch("open and leave unsaved changes", """
expect(workspace.path === '', 'expected Untitled on the first launch');
expect(workspace.page.mode === 'Text', 'Text is the default with no saved mode');
expect(browserVisible, 'the sidebar is shown with no saved visibility');
requestOpen("files/a's notes.txt");
expect(workspace.page.model.value === 'original bytes', notice);
browseDirectory(browser, 'files/browsed folder');
toggleBrowser();
workspace.page.model.value = 'unsaved edit';
sys.db.execute(sessionDb, 'DROP TABLE last_document');
sys.db.execute(sessionDb, 'CREATE TABLE last_document (id INTEGER PRIMARY KEY, path TEXT NOT NULL)');
sys.db.run(sessionDb, 'INSERT INTO last_document VALUES (1, ?)', "files/a's notes.txt");
""")
        launch("restore only saved bytes, then Save As", """
expect(workspace.path === "files/a's notes.txt" && workspace.page.model.value === 'original bytes', notice);
expect(visibleByte === 0, 'legacy sessions reopen at the start without losing the remembered file');
workspace.page.model.value = 'saved copy';
expect(!browserVisible && browser.directory === 'files/browsed folder',
    'restore the hidden sidebar and its folder independently of the open file');
toggleBrowser();
const draft = workspace.page;
requestSave('files/copy.txt', true);
while (workspace.saving) advanceSave();
expect(workspace.path === 'files/copy.txt' && !workspace.dirty, notice);
expect(workspace.page === draft, 'saving keeps the editable page and its undo history');
changeMode('Hex');
changeHexRowWidth('32');
browseDirectory(browser, 'files/browsed folder');
""")
        launch("restore Save As target", """
expect(workspace.path === 'files/copy.txt' && workspace.page.mode === 'Hex'
    && workspace.page.model.value === '73 61 76 65 64 20 63 6F 70 79', 'reopen saved copy in Hex');
expect(hexRowWidth === '32', 'restore the fixed Hex row width');
expect(browserVisible && browser.directory === 'files/browsed folder', 'restore the shown sidebar and its folder');
const draft = workspace.page;
draft.model.value = '4';
expectError(() => changeMode('Text'), /complete byte pairs/);
expectError(() => workspace.next(), /complete byte pairs/);
expectError(() => requestSave(workspace.path), /complete byte pairs/);
expect(workspace.page === draft && draft.model.value === '4' && !workspace.saving,
    'invalid hex survives navigation and cannot start a save');
if (typeof sys.files.openAtomicWrite === 'function') {
    draft.model.value = '41 42';
    requestSave(workspace.path);
    advanceSave();
    expectError(() => workspace.open(''), /current save/);
    expectError(() => workspace.next(), /current save/);
    cancelSave();
    expect(!workspace.saving && workspace.dirty && workspace.page.model.value === '41 42',
        'cancelling preserves the draft');
}
""")
        assert (root / "copy.txt").read_text() == "saved copy"
        (root / "copy.txt").unlink()
        folder.rmdir()
        launch("recover from missing file, then choose New", """
expect(workspace.path === '' && notice.startsWith('Could not reopen files/copy.txt'), notice);
expect(workspace.page.mode === 'Hex', 'failed mode switch did not change the saved mode');
expect(browserVisible && browser.directory === '' && notice.includes('could not restore the browser folder'),
    'a missing folder falls back to mounted locations without changing visibility');
requestOpen("files/a's notes.txt");
requestOpen('');
""")
        launch("start untitled after New", """
expect(workspace.path === '' && notice.startsWith('Use the file browser'), notice);
expect(workspace.page.mode === 'Hex', 'New does not clear the display mode');
changeMode('Text');
""")
        with (root / "large.bin").open("wb") as file:
            file.seek(0x100000005)
            file.write(b"\x00\xFF")
        launch("edit a sparse file past 4 GiB", """
expect(workspace.page.mode === 'Text' && hexRowWidth === '32', 'restore Text without forgetting Hex row width');
workspace.open('files/large.bin');
expect(workspace.page.problem.includes('NUL'), 'binary page should suggest Hex');
workspace.navigate(0, 'Hex');
const index = workspace.go('0x100000005');
expect(workspace.page.byteAt(index) === 0x100000005 && workspace.page.model.value.slice(index) === '00 FF',
    'exact byte offset above 4 GiB');
workspace.page.model.value = workspace.page.model.value.slice(0, index) + '41 42 43';
workspace.next();
expect(workspace.page.start === 0x100000008, 'Next uses the end of the committed edit');
const back = workspace.go('0x100000005');
expect(workspace.page.model.value.slice(back) === '41 42 43', 'edit survives page navigation');
openDocument('files/large.bin');
changeMode('Hex');
const caretIndex = workspace.seekViewport(0x100000005);
visibleByte = 0x100000000;
reportCaret(workspace.page, caretIndex + 1);
rememberPosition(1000);
""")
        launch("restore a position above 4 GiB", """
expect(workspace.path === 'files/large.bin' && workspace.page.mode === 'Hex', notice);
expect(workspace.page.byteAt(viewAnchor.index) === 0x100000000, 'saved position is not truncated to 32 bits');
const caret = workspace.page.caretAt(viewAnchor.caret);
expect(caret.byte === 0x100000005 && caret.nibble === 1, 'restore the exact editing caret above 4 GiB');
""")
        (root / "large.bin").write_bytes(b"short")
        launch("restore after the file shrank", """
expect(workspace.path === 'files/large.bin' && workspace.page.byteAt(viewAnchor.index) === workspace.size,
    'position past the new end is clamped');
expect(workspace.page.caretAt(viewAnchor.caret).byte === workspace.size, 'the editing caret is clamped to the new EOF');
""")
    print("text_editor_session_test: ok")


if __name__ == "__main__":
    main()
