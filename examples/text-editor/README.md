# Budo Text Editor

From the repository root, run:

```sh
./build/budo run examples/text-editor --width 960 --height 640 --file-root "$PWD"
```

`--file-root` exposes a directory as `files/`. The file browser is on the left.
Use its top-left chevron to hide it and the folder icon (or Ctrl/Cmd+O) to show
it again. Double-click a file to open it or a folder to browse it, use **Up** to
go back, or type a mounted or relative path and click **Open**. **New** starts
a blank document. On narrow windows, the browser temporarily takes the full
width and retracts when you open or create a file.

On startup the editor reopens the last file you opened or saved under a new
name and restores its vertical scroll position and editing cursor, using the
current file root. **New** clears the remembered file and positions. If the file is no longer
available, the editor starts with an untitled document.
The session also restores the last **Text** or **Hex** mode and the Hex
**Auto**, **16**, or **32** bytes-per-row setting, along with the sidebar's last
browsed folder and whether it was shown or hidden. If that folder is no longer
available, the sidebar starts in the reopened file's folder or at the mounted
locations. Unsaved edits are not restored.

**Save** (Ctrl/Cmd+S) writes to the current file, or opens **Save As** for an
untitled or read-only file. **Save As** (Ctrl/Cmd+Shift+S) puts the sidebar in
save mode: browse to a folder and enter a filename, or type a path under
`files/` directly. Existing files are replaced atomically on supported builds.
The toolbar shows the current filename and wraps its buttons onto additional
rows when they need more space.

- **Text** edits UTF-8. Invalid UTF-8 or pages containing NUL bytes can be edited in
  **Hex** instead. Hex accepts pairs like `00 41 FF`; whitespace is ignored.
  The Hex cursor skips spaces and line breaks between bytes, but both digits
  of each byte remain editable.
  The left margin shows each visible row's absolute byte offset in decimal and hexadecimal.
  The row-width selector sits next to the byte offset and **Go**.
  **Auto** uses the largest power of two that fits per row at the current window
  size and zoom, up to 256 bytes, while **16** and **32** keep a fixed row width.
  Turn off **Wrap** to keep fixed
  rows on one visual line and scroll horizontally if needed.
  The ASCII column on the right shows printable bytes and uses `.` for other
  bytes and `?` for incomplete or invalid hex. Click either column to edit it:
  the active line and byte are marked in both, more strongly in the active column.
  Scrolling does not move the active byte; its highlight reappears when you
  scroll back to it.
  Typing in ASCII overwrites bytes by default. To append, go to the end of the
  file. Finish or correct incomplete hex before editing in ASCII.
- Both modes use the bundled JetBrains Mono font ([SIL Open Font License](./JetBrainsMono-OFL.txt)).
  When editing, the active line is highlighted across all of its wrapped rows,
  and a rectangular cursor marks the character at the insertion point.
  Text and Hex share a cursor by byte offset. Text shows the start of the UTF-8
  character containing that byte; switching back without moving the cursor
  returns to the exact Hex digit. Switching modes centers the active row
  vertically when there is enough content above and below it.
- Turn off **Wrap** to keep lines on one row. Use the horizontal scrollbar,
  horizontal wheel, or **Shift+wheel** to see the rest of a long line.
- Use **Ctrl+wheel** over the document, pinch on a macOS trackpad, or pinch with
  two fingers on a touchscreen to resize the editor text without resizing the
  toolbar. Two-finger scrolling still scrolls normally.
- Scroll continuously in either mode using the wheel, touch, or the vertical
  scrollbar. **Page Up** and **Page Down** move the cursor by about one screen
  and center the resulting row vertically when there is enough content. If
  more rows need to load, the viewport waits before jumping to the destination.
  On long files the scrollbar tracks byte position, so its thumb is
  approximate when lines have different lengths or wrap. In Hex, **Go** jumps
  to a decimal or `0x`-prefixed byte offset.
- Selection and undo/redo are local to the current editing buffer. Scrolling
  far enough to load a different buffer commits valid edits and resets undo.
  Complete invalid Hex pairs before scrolling beyond the current buffer.
- A file is opened without reading or indexing it in full. Text pages hold up to
  8 KiB; hex buffers grow to fill the viewport, up to 8 KiB of source bytes.
  The document remembers edited
  ranges in memory and reads unchanged ranges from disk on demand. Don't edit
  the backing file in another app while it is open here.
- On the updated macOS/Linux runtime, the editor stages saves in 1 MiB blocks.
  **Cancel** leaves the destination unchanged. The destination directory must
  exist and have room for a second copy. On older builds or other platforms,
  only saving to a new file up to 8 MiB is offered.

The `editor-ui/` directory is a local copy of the standard Budo UI library.
Local changes reset widget state when the editor loads a new page or the file
browser enters a different directory.

## Code organization

- [`main.js`](./main.js) draws the editor, routes input and dialogs, and stores
  the last successfully opened or saved path and display mode. [`file-picker.js`](./file-picker.js)
  owns directory browsing and path entry.
- [`workspace.js`](./workspace.js) owns the active document, page, and save
  operation. Navigation and saves commit the draft first. While a save is in
  progress, the UI makes the page read-only and the workspace rejects changes
  to the active file or page. Publishing a save replaces the backing file
  without resetting page-local undo.
- [`page.js`](./page.js) owns the bounded byte snapshot and its editable Text
  or Hex representation. Invalid drafts stay in the page rather than entering
  the piece table. [`document.js`](./document.js) provides byte storage and
  the UTF-8/hex conversions.

From the repository root, run `python3 tests/text_editor_session_test.py` for
the Budo file and session tests, `node tests/text_editor_browser_test.mjs` for
the sidebar, and `node tests/text_editor_hex_test.mjs` for the hex/ASCII widget.
