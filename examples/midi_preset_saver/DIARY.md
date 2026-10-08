# MIDI Preset Saver: engineering diary

Notes on the MIDI capture, recall, recording, and playback fixes. Newest
entries last. The JSON export format and the database schema are not changed
by any of this work: existing exports and databases keep loading and recalling
exactly as before.

## 2026-10-06: audit and plan

An audit of recording, replay, storage, and loading of MIDI events found
problems in two layers: the Budo MIDI runtime (`src/midi/`) and the app.
Fix order, runtime first because the app cannot be right on top of a lossy
input path:

1. Runtime: input messages are dispatched by device index instead of port
   handle, so input from any device except the first in the list is dropped.
2. Runtime: SysEx is only reassembled within one packet (macOS, Android), not
   at all on Windows, and Linux reads 3-byte chunks as messages. One shared,
   tested byte-stream parser for every platform.
3. Runtime: SysEx output over ~1 KB fails on macOS (fixed 1 KB packet buffer).
4. Runtime: SysEx limits (4 KB per message, 16 per frame, silently dropped).
5. Runtime: channel messages and SysEx are delivered in two batches per frame,
   losing their relative order.
6. App: captured commands are ordered by `Date.now()` at dispatch, so a burst
   delivered in one frame (Bank Select + Program Change) can be stored in the
   wrong order.
7. App: capture keeps everything forever; only the X key clears it.
8. App: pressure and channel-mode messages are stored as preset state.
9. App: recall sends SysEx with no pacing.
10. App: notes held at Stop, or cut by the event limit, hang on playback.
11. App: timestamps from mixed clocks can scramble a recording.
12. App: memos record notes only (sustain pedal is lost).
13. App: stopping playback early leaves notes sounding.
14. App: the 45 ms minimum note length can cut a re-struck note short.
15. App: playback is driven by the wall clock and bursts after a stall.
16. App: MIDI devices are selected by list index, so a hot-plug can redirect
    Live recalls to another device.
17. App: Live recall writes to the database before sending MIDI.
18. App: an export over 8 MiB cannot be imported again.

## 2026-10-06: runtime fixes (src/midi, private/android)

- **Dispatch by handle.** Every backend (CoreMIDI, ALSA, WinMM, Android)
  called the input callback with the device's list index, while the script
  bindings look callbacks up by the handle `openInput` returned. The app keeps
  one input open, in handle 0, so any input that was not first in the device
  list was delivered to an empty slot and lost. Inputs now remember their
  handle and report it.
- **Android handles ran out.** Java numbered input handles with an
  ever-growing counter, but native code keeps a 32-slot table. This app
  reopens its input on every device change, so after 32 openings all input
  was ignored. Java now reuses the lowest free handle, and closes a device that
  finishes opening after it was already closed.
- **One byte-stream parser** (`src/midi/midi_stream_parser.c`, tested by
  `tests/midi_stream_parser_test.c`) for every platform that sees raw bytes:
  SysEx reassembled across packets (macOS CoreMIDI and Android split long
  dumps), running status (Bluetooth MIDI devices use it), real-time bytes
  skipped anywhere, system common messages consumed, and a SysEx that is too
  long or interrupted dropped whole and counted, never delivered truncated.
  Linux read 3-byte chunks as messages; it now parses the stream. Android
  forwards raw bytes to the parser (`nativeOnMidiData`) instead of its own
  Java parser. Windows gained SysEx input (`MIM_LONGDATA` buffers, reassembled
  by the same parser); it had none.
- **Long SysEx output on macOS.** `midi_send_raw` built its packet list in a
  1 KB stack buffer, so any longer SysEx failed. It now sizes the packet list
  to the message.
- **Limits.** SysEx up to 65535 bytes (was 4096, larger ones were silently
  discarded); up to 1024 input events and 32 SysEx messages per frame (were
  256 and 16). Drops are counted (`js_midi_dropped_*`).
- **Arrival order.** Channel messages and SysEx sat in two queues and scripts
  got all channel messages first, then all SysEx. One ordered queue
  (`src/midi/midi_event_queue.c`) now keeps them in arrival order, so a Bank
  Select, a SysEx, and a Program Change reach the app as the device sent them.

Verified: the parser and queue unit tests, the binding isolation test (now
also checking arrival order in JavaScript and Lua), the Android C file with
the NDK compiler, and `MidiHelper.java` with javac. The Windows and Linux
paths are compiled by CI only (no toolchain here).

## 2026-10-06: app fixes (main.ts)

None of these change the database schema, the stored payload formats, or the
JSON export format; old exports import and recall exactly as before. Checked
by comparing, byte for byte with the previous version, the schema and
migrations and every export, import, encode, and decode function (the only
difference is the import size limit).

- **Capture order.** `receivedAt` is still the arrival time in milliseconds,
  but made strictly increasing (`nextCaptureTime`), so messages delivered in
  the same millisecond keep their order. Before, a tie fell back to the order
  in which each kind of message was *first* seen in the session, which could
  store a Bank Select burst as Program Change, CC0, CC32 and recall the wrong
  patch. It stays a non-negative number, as the schema requires.
- **Not preset state.** Pedals (CC 64, 66, 67), channel-mode messages (CC
  120-127: all sound off, reset controllers, local control, all notes off,
  omni, mono/poly), and channel and poly pressure are no longer captured. A
  recall could replay "Local Control Off", and an aftertouch keyboard filled
  presets with one pressure message per key ever touched. Existing presets are
  not rewritten; they recall what they stored.
- **Clear capture.** The captured state goes into every new preset; a visible
  "Clear capture (N)" button in the Edit log panel now clears it (the X key was
  the only way, unusable on touch screens).
- **Held notes.** Recording tracks notes and pedals held down. Stop releases
  them in the memo; past the 256-event limit, releases are still recorded (up
  to 160 more). Memos recorded before this fix are closed at playback: notes
  or pedals still down at the end are released when the memo ends.
- **Pedals in memos.** Sustain, sostenuto, and soft pedal are recorded with
  the notes (same event format), and released like notes.
- **Recording clock.** Device timestamps are aligned with the wall clock on
  their first appearance, and again when they restart (a port reopened during
  recording), so mixed or restarted timestamps stay on one timeline.
- **Stopping playback** (switching preset, song, or mode, deleting the memo,
  recording, or a device change) sends note-offs and pedal releases for what
  is still sounding. Device changes stop playback before the port closes, so
  the releases reach the old port.
- **Re-struck notes.** The 45 ms minimum length no longer pushes a note-off
  past the next strike of the same key (it ends 1 ms before it).
- **Playback clock.** Playback and recall pacing use the frame clock
  (monotonic) instead of `Date.now()`, and a playback more than 250 ms behind
  (a stalled frame) skips ahead instead of sending every overdue event at once.
- **Recall pacing.** Channel messages go out back to back; after each SysEx,
  the recall waits 20 ms before the next message, across frames. A new recall
  replaces one still in progress.
- **Live recall latency.** The MIDI is sent first; the selection and its
  database writes follow.
- **Devices by name.** The chosen input and output are remembered by name
  (`app_state` keys `midi_input_name`, `midi_output_name`) and found again
  after every hot-plug. If the chosen output disappears, recall is disabled
  (the log and the output picker say it is disconnected) instead of falling
  back to whatever device took its place; it reconnects when it returns.
  Two devices with the same name cannot be told apart: the first is used.
- **Export size.** Import accepts up to 64 MiB (was 8 MiB); Save warns when an
  export is larger than that.

Tests: `midi.test.mjs` (CTest `midi_preset_saver`) runs main.ts through Budo's
own TypeScript stripper (`ts_strip_tool`) in node, with node's SQLite as the
database and a scripted MIDI device. Its 12 tests reproduce each bug above;
all 12 fail against the previous main.ts and pass now. The Android APK was
built, and its JNI symbol for the new raw-byte path checked.

Writing note: Budo's TypeScript stripper reads `index < list.length) {` as a
generic type and blanks real code. New comparisons are written
`list.length > index`; check with
`build/ts_strip_tool main.ts | node --check --input-type=module`.
