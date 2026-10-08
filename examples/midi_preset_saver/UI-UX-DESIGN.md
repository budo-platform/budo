# MIDI Preset Saver UI/UX Design

## Product modes

The application has two different operating contexts and must treat them as two
products sharing one data model.

- **Edit mode is a setup workspace.** The operator is attentive and can compare,
  inspect, name, reorder, import, export, capture, and correct data. Density is
  acceptable when hierarchy and actions are explicit.
- **Live mode is a performance surface.** The operator is dividing attention
  between the app, instruments, sound, and an audience. The interface must favor
  recognition, stable positions, large targets, and immediate confidence.

## Principles

1. **Make mode unmistakable.** Setup and performance use different hierarchy,
   color emphasis, labels, and available actions. No destructive editing appears
   in Live mode.
  Switching modes uses one continuous spatial transition: secondary Edit zones
  fade away while the shared Library expands and its grouped rows crossfade into
  the Live preset flow. Returning to Edit reverses the same geometry.
2. **One dominant task per mode.** Edit prioritizes building the song/preset
   library. Live prioritizes selecting a song and recalling a preset.
3. **Optimize Live for a glance.** Output readiness, current song, available
   presets, and last successful recall must be readable without scanning secondary
   metadata.
4. **Preserve spatial memory.** Controls do not move after feedback or state
   changes. Selection changes content only at an intentional boundary.
5. **Make Live recall one deliberate tap.** Repeated confirmation dialogs add
   cognitive and timing cost. Safety comes from large separated targets,
   release-based activation, movement cancellation, and unmistakable feedback.
6. **Separate scrolling from activation.** A gesture that crosses the movement
   threshold never activates a row. Touch tolerance must account for hand tremor
   and stage movement.
7. **Make Edit actions discoverable.** Rename/delete/update actions use visible
   action affordances; long-press may remain a shortcut, never the only route.
8. **Show consequences immediately.** MIDI recall reports the preset name,
   command count, success/failure, and recency in the persistent log/debug panel
   shared by both modes.
9. **Block impossible actions at the source.** Missing MIDI output disables recall
   targets and is explained at the top of Live mode.
10. **Protect data workflows.** Imports are explicit, validated, recoverable, and
    visually separated from everyday setup actions.
11. **Use platform behavior where it reduces effort.** Names use Budo text sessions
    and native IME; imports use the system file picker.
12. **Remain usable under interruption.** Pause/resume, picker return, orientation,
    and layout changes preserve model state and interaction readiness.

## Visual language

The interface uses a warm analogue-instrument palette inspired by professional
audio hardware rather than a generic software dashboard. Graphite and walnut
tones establish the chassis, warm ivory carries labels, oxidized copper marks
the primary interaction, brass identifies setup attention, and restrained sage
confirms successful MIDI operations. Dusty blue-green remains available for
secondary data actions, while destructive red is reserved for genuine danger.

Color is functional rather than decorative: selection uses a warm recessed
surface, Live song groups use muted hardware-like accents, and status colors
retain distinct luminance as well as hue. The shader adds subtle warm faceplate
sheen and grain without reducing text contrast.

Barlow Semi Condensed Medium provides compact, humanist instrument labeling
without sacrificing preset-name readability. It is bundled under the SIL Open
Font License and loaded through `sys.font`, with the runtime default retained as
a defensive fallback.

## Spacing system

Spacing follows a small role-based scale. Equivalent relationships use the same
token even when the surrounding components have different dimensions.

| Token | Size | Role |
| --- | ---: | --- |
| `SPACE_XS_DP` | 4dp | Compact internal insets, especially controls embedded in rows |
| `SPACE_SM_DP` | 6dp | Gaps between paired workspace controls |
| `SPACE_MD_DP` | 8dp | Standard separation between related controls or subregions |
| `SPACE_LG_DP` | 12dp | Edit-mode outer margins and action-to-content spacing |
| `SPACE_XL_DP` | 18dp | Modal content padding |

The following rules govern use of the scale:

- Paired buttons in Edit panels use 6dp between controls.
- Lists begin 12dp after their panel action row.
- Buttons embedded in list rows use a 4dp vertical inset.
- Compact drag handles use a consistent 24dp width and switch at the same
  190dp row-width threshold for songs and presets.
- Modal dialogs retain 18dp content padding and 10dp vertical button gaps. The
  larger gaps separate consequential actions and are not workspace spacing.
- The Live preset flow inherits the Edit Library panel's horizontal bounds, so
  its position and width remain fixed during mode transitions. Live chips retain
  a 10dp wrapping gap for glanceability and touch use during performance.
- Row heights remain content-specific: memo rows are compact, song and preset
  rows carry hierarchy or metadata, and Live targets remain the largest. Equal
  spacing does not require equal component heights.
- One-off optical offsets for text baselines, icon centering, and scrollbar
  geometry are not spacing tokens and may differ when needed for alignment.

## Current-state audit

### Edit mode

- Songs and presets share one vertically ordered Library. Each song is a full
  parent row followed by indented preset children, so hierarchy and set order
  can be scanned without moving between columns.
- Moving a song reorders its complete visual group; presets retain their parent
  identity. Preset reordering is constrained to siblings under one song.
- Sequence memos and MIDI Capture are separate work zones outside the Library,
  keeping preset hierarchy independent from note-sequence editing and incoming
  device monitoring.
- Sequence memos can collapse to its header and expand again through an
  always-visible toggle. A reversible eased transition gives reclaimed height
  to the Library on phones and reclaimed width on larger layouts.
- Edit/Live transitions lock input until they settle and preserve the shared
  song/preset surface as the visual anchor: the Library panel morphs into the
  Live flow on a critically damped spring while mode-specific chrome fades
  across the whole transition. The shared surface hands its content over in
  sequence (Edit rows fade out during the first half, Live chips fade in during
  the second), so the two never overlap, and nested panels fade as one layer.
  With the system's reduced-motion setting, the switch is immediate.
- `MIDI Preset Saver` is a persistent, fixed-position anchor across both modes.
  Live and Setup share one fixed button rectangle and crossfade labels inside it.
  Live connectivity is reduced to a small red/green dot immediately left of that
  button; the previous large status header is intentionally absent.
- Edit and Live share one fixed bottom log/debug panel. It keeps the selected
  song/preset and MIDI output context visible, then displays whichever is newer:
  the current status or the latest event-log entry. The panel does not move,
  resize, duplicate, or crossfade during mode transitions.
- Row actions use always-visible `•••` buttons; long-press is no longer used,
  so it cannot conflict with scrolling.
- A touch moves 18dp before it scrolls instead of activating.
- Capture, sequence, and persistence tools have similar visual weight despite
  different frequency and risk.
- Import/export are technically safe but visually compressed in narrow layouts.

### Live mode

- All presets use one wrapping performance flow. Compact song separators and
  stable group colors preserve hierarchy without a separate navigator.
- Edit and Live retain too much shared visual language, weakening mode certainty.
- Output status does not include the selected output name in the primary header.
- Recall feedback uses the shared bottom log/debug panel and the recalled-row
  pulse, preserving the same feedback location in both modes.
- The Edit exit control is as prominent as performance actions.
- A touch moves 18dp before it scrolls, and a recall is cancelled when the
  finger moved that far, so stage tremor neither scrolls nor recalls.

## Remediation plan

### Implemented foundation

- The interface is built with budo-ui (vendored in `ui/`, refreshed by
  `scripts/sync-budo-ui.py`): the grouped Library is a reorderable list,
  dialogs trap the keyboard focus, every control is reachable with Tab and
  readable by screen readers, and colors come from the palette above as a
  budo-ui theme. The data model, MIDI handling, and the JSON export/import are
  independent of the interface and unchanged by it.

- Native Budo text sessions and IME composition.
- Versioned JSON export/import with a published JSON Schema, strict validation,
  and transactional recovery.
- Cross-platform system file picker.
- Release-based list activation and dedicated drag handles.

### Phase 1: mode architecture and performance ergonomics

- Keep Live presets in one grouped wrapping flow with large recall targets.
- Add a strong Live header containing mode, MIDI readiness, and output name.
- Keep recall feedback in the persistent log/debug panel and retain the short
  recalled-row visual pulse.
- Increase movement threshold.
- Add visible row action buttons in Edit mode.

### Phase 2: setup efficiency

- Keep songs and child presets in one grouped, reorderable Library.
- Keep Sequence memos in a dedicated zone outside the Library.
- Group persistence tools under a secondary Data action on narrow layouts.
- Add preset content summary/preview before destructive update.
- Add explicit delete confirmation with item name and consequences.
- Preserve independent Edit and Live scroll positions.

### Phase 3: performance hardening

- Optional performance lock requiring a deliberate hold to leave Live mode.
- Configurable preset grid density for tablets.
- Optional high-contrast daylight/stage theme.
- Optional external MIDI command for song/preset navigation.

## Acceptance criteria

- In Live mode, current output status, song, preset choices, and the latest
  log/debug event are visible in one glance.
- A preset can be recalled with one clean tap and cannot be recalled by a scroll.
- The most common Live targets are at least 56dp high.
- Song grouping remains visible while scrolling the unified preset flow.
- Every Edit row action has a visible affordance.
- Returning from native keyboard or file picker leaves rendering and touch input
  active.
