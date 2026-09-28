#!/usr/bin/env bash
# Extract JS bindings and consume contract-generated fragments to emit
# types/budo.d.ts.
#
# This script parses the JS_CFUNC_DEF / JS_SetPropertyStr registrations in the
# JS binding source files and cross-references them against a hand-maintained
# type-annotation file (scripts/type-annotations.txt) to produce
# types/budo.d.ts.
#
# The annotation file maps each JS function name (optionally scoped by object)
# to its TypeScript signature and doc-comment.  Functions found in the C source
# but missing from the annotations file are emitted with a generic signature so
# that nothing is silently dropped.
#
# Usage:
#   ./scripts/generate-types.sh          # writes types/budo.d.ts
#   make generate-types                  # same, via Makefile

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "$SCRIPT_DIR/_common.sh"

SRC_DIR="$PROJECT_ROOT/src"
ANNOTATIONS="$SCRIPT_DIR/type-annotations.txt"
FRAGMENTS_DIR="$SCRIPT_DIR/type-fragments"
OUT="$PROJECT_ROOT/types/budo.d.ts"
SNAPSHOT_OUTS=(
    "$PROJECT_ROOT/budo.d.ts"
    "$PROJECT_ROOT/examples/midi_preset_saver/budo.d.ts"
)
CHECK_ONLY=false
if [[ "${1:-}" == "--check" ]]; then
    CHECK_ONLY=true
elif [[ $# -gt 0 ]]; then
    echo "Usage: $0 [--check]" >&2
    exit 2
fi
GENERATED_OUT=$(mktemp)

# ─── 1.  Extract registered function names from C sources ────────────────────

extract_cfunc_defs() {
    # $1 = source file
    # Outputs lines like:   canvas clear 1
    #                       gl     createProgram 2
    #                       window getWidth 0
    local file="$1"
    local current_scope=""

    while IFS= read -r line; do
        # Detect which function-list array we're in (sys.* namespace hierarchy)
        if [[ "$line" =~ js_canvas_texture_canvas_funcs || "$line" =~ js_canvas_texture_funcs || "$line" =~ js_gl_program_funcs || "$line" =~ js_gl_screen_funcs ]]; then
            current_scope=""
        elif [[ "$line" =~ js_sys_canvas_funcs ]]; then
            current_scope="canvas"
        elif [[ "$line" =~ js_sys_path_funcs ]]; then
            current_scope="path"
        elif [[ "$line" =~ js_sys_svg_funcs ]]; then
            current_scope="svg"
        elif [[ "$line" =~ js_sys_font_funcs ]]; then
            current_scope="font"
        elif [[ "$line" =~ js_sys_graphics_funcs ]]; then
            current_scope="graphics"
        elif [[ "$line" =~ js_sys_gl_funcs ]]; then
            current_scope="gl"
        elif [[ "$line" =~ js_sys_input_funcs ]]; then
            current_scope="input"
        elif [[ "$line" =~ js_sys_window_funcs ]]; then
            current_scope="window"
        elif [[ "$line" =~ js_sys_animation_funcs ]]; then
            current_scope="animation"
        elif [[ "$line" =~ js_sys_timer_funcs ]]; then
            current_scope="timer"
        elif [[ "$line" =~ js_console_funcs ]]; then
            current_scope="console"
        elif [[ "$line" =~ js_audio_funcs ]]; then
            current_scope="audio"
        elif [[ "$line" =~ js_midi_funcs ]]; then
            current_scope="midi"
        elif [[ "$line" =~ js_db_funcs ]]; then
            current_scope="db"
        elif [[ "$line" =~ js_file_funcs ]]; then
            current_scope="file"
        elif [[ "$line" =~ js_magneto_funcs ]]; then
            current_scope="magneto"
        elif [[ "$line" =~ js_sys_device_funcs ]]; then
            current_scope="device"
        elif [[ "$line" =~ js_neural_funcs ]]; then
            current_scope="neural"
        fi

        # Match JS_CFUNC_DEF("name", argc, handler)
        if [[ "$line" =~ JS_CFUNC_DEF\(\"([^\"]+)\",\ *([0-9]+) ]]; then
            if [[ -n "$current_scope" ]]; then
                echo "${current_scope} ${BASH_REMATCH[1]} ${BASH_REMATCH[2]}"
            fi
        # Match function-data tables: {"name", argc, handler}
        elif [[ "$line" =~ \{\"([^\"]+)\",\ *([0-9]+)\, ]]; then
            if [[ -n "$current_scope" ]]; then
                echo "${current_scope} ${BASH_REMATCH[1]} ${BASH_REMATCH[2]}"
            fi
        fi
    done < "$file"
}

extract_constants() {
    # $1 = source file
    # Looks for JS_SetPropertyStr(..., "NAME", JS_NewInt32(..., VALUE))
    # Outputs:  scope NAME
    local file="$1"

    grep -oE 'JS_SetPropertyStr\(ctx, [a-z_]+, "([A-Z_]+)", JS_NewInt32' "$file" 2>/dev/null | \
        sed -E 's/JS_SetPropertyStr\(ctx, ([a-z_]+), "([A-Z_]+)".*/\1 \2/' | \
        sed 's/audio_obj/audio/' | sed 's/midi_obj/midi/' | sort -u
}

# Collect everything
ALL_FUNCS=$(mktemp)
ALL_CONSTS=$(mktemp)
trap 'rm -f "$ALL_FUNCS" "$ALL_CONSTS" "$GENERATED_OUT"' EXIT

extract_cfunc_defs "$SRC_DIR/graphics/js_canvas_bindings.c"  >> "$ALL_FUNCS"
extract_cfunc_defs "$SRC_DIR/audio/js_audio_bindings.c"     >> "$ALL_FUNCS"
extract_cfunc_defs "$SRC_DIR/midi/js_midi_bindings.c"       >> "$ALL_FUNCS"
extract_cfunc_defs "$SRC_DIR/sqlite/js_sqlite_bindings.c"   >> "$ALL_FUNCS"
extract_cfunc_defs "$SRC_DIR/file/js_file_bindings.c"       >> "$ALL_FUNCS"
extract_cfunc_defs "$SRC_DIR/magneto/js_magneto_bindings.c" >> "$ALL_FUNCS"
extract_cfunc_defs "$SRC_DIR/neural/js_neural_bindings.c"   >> "$ALL_FUNCS"

extract_constants "$SRC_DIR/audio/js_audio_bindings.c" >> "$ALL_CONSTS"
extract_constants "$SRC_DIR/midi/js_midi_bindings.c"   >> "$ALL_CONSTS"

# ─── 2.  Read type-annotations file ─────────────────────────────────────────

# The annotations file has the format:
#
#   [scope.name]
#   sig = (x: number, y: number) => void
#   ret = number              (optional, for standalone return type)
#   doc = /** Brief doc. */   (optional)
#
# Blank lines or # comments are ignored.

declare -A SIG_MAP
declare -A DOC_MAP
CURRENT_KEY=""

if [[ -f "$ANNOTATIONS" ]]; then
    while IFS= read -r line; do
        # Skip blank / comment
        [[ -z "$line" || "$line" =~ ^[[:space:]]*# ]] && continue

        if [[ "$line" =~ ^\[([a-zA-Z0-9_.]+)\] ]]; then
            CURRENT_KEY="${BASH_REMATCH[1]}"
        elif [[ "$line" =~ ^sig[[:space:]]*=[[:space:]]*(.*) ]]; then
            SIG_MAP[$CURRENT_KEY]="${BASH_REMATCH[1]}"
        elif [[ "$line" =~ ^doc[[:space:]]*=[[:space:]]*(.*) ]]; then
            DOC_MAP[$CURRENT_KEY]="${BASH_REMATCH[1]}"
        fi
    done < "$ANNOTATIONS"
fi

# ─── 3.  Emit the .d.ts ─────────────────────────────────────────────────────

emit_header() {
    cat <<'EOF'
// =============================================================================
// Budo Runtime — TypeScript Declarations
// =============================================================================
//
// Auto-generated by scripts/generate-types.sh from JS bindings and API contracts.
// Do not edit manually — run `make generate-types` to regenerate.
//
// Usage: place this file (or a symlink) in your project and include it from a
// tsconfig.json / jsconfig.json. Budo is not a DOM or Node runtime, so keep the
// ambient environment explicit:
//
//   {
//     "compilerOptions": { "lib": ["ES2020"], "types": [] },
//     "include": ["**/*.js", "budo.d.ts"]
//   }
//

EOF
}

emit_types_and_interfaces() {
    cat <<'EOF'
// -- Color types --------------------------------------------------------------

/**
 * A color value accepted by canvas drawing functions.
 * - Hex string: `"#RRGGBB"` or `"#RRGGBBAA"`
 * - Numeric ARGB: `0xAARRGGBB`
 */
type Color = string | number;

// -- Input types --------------------------------------------------------------

interface Pointer {
  id: number;
  x: number;
  y: number;
  dx: number;
  dy: number;
  down: boolean;
  pressed: boolean;
  type: "mouse" | "touch";
}

interface Mouse {
  x: number;
  y: number;
  dx: number;
  dy: number;
  wheelX: number;
  wheelY: number;
  left: boolean;
  middle: boolean;
  right: boolean;
  leftPressed: boolean;
  rightPressed: boolean;
}

interface Keyboard {
  shift: boolean;
  ctrl: boolean;
  alt: boolean;
  meta: boolean;
}

interface TextInputEdit {
    text: string;
    selectionStart: number;
    selectionEnd: number;
}

interface TextInputComposition {
    active: boolean;
    changed: boolean;
    text: string;
    selectionStart: number;
    selectionEnd: number;
}

interface TextInputOptions {
    text: string;
    selectionStart: number;
    selectionEnd: number;
    multiline?: boolean;
    caret?: { x: number; y: number; width: number; height: number };
}

interface InputState {
  pointer: Pointer;
  pointers: Pointer[];
  mouse: Mouse;
  keyboard: Keyboard;
    /** Layout-aware UTF-8 text committed since the previous frame. */
    text: string;
    textEdit: TextInputEdit | null;
    composition: TextInputComposition;
    textInputActive: boolean;
  deltaTime: number;
  totalTime: number;
  frameCount: number;
  focused: boolean;
}

// -- MIDI message type --------------------------------------------------------

interface MidiMessage {
    status: number;
    data1: number;
    data2: number;
    /** Native receive/event timestamp in microseconds from a monotonic clock, or 0 when unavailable. */
    timestamp: number;
    type: number;
    channel: number;
    /** SysEx bytes for system-exclusive messages. */
    data?: number[];
    /** True when the message was received from an RTP-MIDI network session. */
    network?: boolean;
}

// -- MIDI device info ---------------------------------------------------------

interface MidiDevice {
  id: number;
  name: string;
}

interface MidiDevicesChangedEvent {
    /** Fresh input device snapshot after the topology change. */
    inputs: MidiDevice[];
    /** Fresh output device snapshot after the topology change. */
    outputs: MidiDevice[];
    /** Monotonically increasing generation for this callback registration. */
    generation: number;
}

// -- RTP-MIDI session info ---------------------------------------------------

interface RtpMidiSession {
    handle: number;
    name: string;
    port: number;
    state: "idle" | "listening" | "connecting" | "connected" | "closed" | "unknown";
    peerCount: number;
}

// -- File entry type ----------------------------------------------------------

interface FileEntry {
  name: string;
  type: "file" | "directory";
  size?: number;
}

interface PickedTextFile {
    name: string;
    text: string;
}

// -- Accelerometer data -------------------------------------------------------

interface AccelData {
  /** Acceleration along X axis (m/s²). */
  x: number;
  /** Acceleration along Y axis (m/s²). */
  y: number;
  /** Acceleration along Z axis (m/s²). */
  z: number;
}

// -- Compass data -------------------------------------------------------------

interface CompassData {
  /** Magnetic field along X axis (µT). */
  x: number;
  /** Magnetic field along Y axis (µT). */
  y: number;
  /** Magnetic field along Z axis (µT). */
  z: number;
  /** Compass heading in degrees (0 = north, 90 = east). */
  heading: number;
}

// -- Fetch / Network types ----------------------------------------------------

interface RequestInit {
  method?: "GET" | "POST" | "PUT" | "DELETE" | "PATCH" | "HEAD" | "OPTIONS";
  headers?: Record<string, string>;
  body?: string | ArrayBuffer;
}

interface Headers {
  get(name: string): string | null;
  has(name: string): boolean;
  entries(): [string, string][];
  keys(): string[];
  values(): string[];
  forEach(callback: (value: string, name: string) => void): void;
}

interface Response {
  readonly ok: boolean;
  readonly status: number;
  readonly statusText: string;
  readonly url: string;
  readonly redirected: boolean;
  readonly type: string;
  readonly headers: Headers;
  readonly bodyUsed: boolean;
  text(): string;
  json(): any;
  arrayBuffer(): ArrayBuffer;
}

interface CanvasTextureCanvas {
    /** Clear this offscreen canvas texture. */
    clear(color?: Color): void;
    /** Flush this offscreen canvas texture so its GL texture can be sampled. */
    flush(): void;
    /** Draw a filled/stroked rectangle into this offscreen canvas texture. */
    drawRect(x: number, y: number, width: number, height: number): void;
    /** Draw a filled/stroked circle into this offscreen canvas texture. */
    drawCircle(cx: number, cy: number, radius: number): void;
    /** Draw text into this offscreen canvas texture. */
    drawText(text: string, x: number, y: number, fontSize?: number): void;
}

interface CanvasTexture {
    /** Opaque CanvasTexture handle. */
    readonly id: number;
    /** Current width in pixels. */
    readonly width: number;
    /** Current height in pixels. */
    readonly height: number;
    /** Backing GL texture id. Pass the CanvasTexture object to sys.gl.bindCanvasTexture. */
    readonly texture: number;
    /** Backing GL framebuffer id. */
    readonly target: number;
    /** Skia drawing API for this offscreen texture. */
    readonly canvas: CanvasTextureCanvas;
    /** Flush pending Skia work so the backing GL texture can be sampled. */
    flush(): void;
    /** Resize this texture-backed Skia surface. */
    resize(width: number, height: number): void;
    /** Destroy the owned Skia surface, GL texture, and framebuffer. */
    destroy(): void;
}

EOF
}

# Helper: emit one function declaration
#   $1 = indentation
#   $2 = scope.name key
#   $3 = JS function name
#   $4 = argc from C source
emit_func() {
    local indent="$1" key="$2" name="$3" argc="$4"
    local sig doc

    sig="${SIG_MAP[$key]:-}"
    doc="${DOC_MAP[$key]:-}"

    # If no annotation, build a generic signature from argc
    if [[ -z "$sig" ]]; then
        local params=""
        for ((i=0; i<argc; i++)); do
            [[ -n "$params" ]] && params="$params, "
            params="${params}arg$((i+1)): any"
        done
        sig="($params): any"
    fi

    if [[ -n "$doc" ]]; then
        echo "${indent}${doc}"
    fi
    echo "${indent}${name}${sig};"
    echo ""
}

# Helper: emit a constant
#   $1 = indent
#   $2 = constant name
#   $3 = value (from annotation, or 'number')
emit_const() {
    local indent="$1" cname="$2"
    local key val doc
    key="$3"
    val="${SIG_MAP[$key]:-number}"
    doc="${DOC_MAP[$key]:-}"

    if [[ -n "$doc" ]]; then
        echo "${indent}${doc}"
    fi
    echo "${indent}readonly ${cname}: ${val};"
}

# ─── Build the output ────────────────────────────────────────────────────────

{
    emit_header
    emit_types_and_interfaces

    # ── sys.gl ──
    echo "// -- sys.gl namespace ---------------------------------------------------------"
    echo ""
    echo "interface SysGL {"
    # Names handled by the 3D-pipeline fragment (skipped here so the auto path
    # doesn't emit a generic `(arg1: any, …): any` shadow signature for them).
    GL3D_NAMES="|createBuffer|updateBuffer|destroyBuffer|createTexture2D|loadTexture2D|createTextureCube|loadTextureCube|updateTexture2D|destroyTexture|createVertexLayout|setAttribute|setIndexBuffer|destroyVertexLayout|getAttribLocation|setUniformMatrix3|setUniformMatrix4|setUniform1fv|setUniform2fv|setUniform3fv|setUniform4fv|setUniform1iv|bindTexture2D|bindTextureCube|drawMesh|drawMeshInstanced|"
    while read -r scope name argc; do
        [[ "$scope" == "gl" ]] || continue
        [[ "$GL3D_NAMES" == *"|$name|"* ]] && continue
        emit_func "  " "gl.$name" "$name" "$argc"
    done < "$ALL_FUNCS"
    cat "$FRAGMENTS_DIR/sysgl-3d.d.ts.frag"
    echo "}"
    echo ""

    # ── GLDrawOptions + contract-generated basic interfaces.
    cat "$FRAGMENTS_DIR/gl-options.d.ts.frag"
    echo ""
    cat "$FRAGMENTS_DIR/generated-basics.d.ts.frag"
    echo ""

    # ── sys.canvas ──
    echo "// -- sys.canvas object --------------------------------------------------------"
    echo ""
    echo "interface SysCanvas {"
    while read -r scope name argc; do
        [[ "$scope" == "canvas" ]] || continue
        emit_func "  " "canvas.$name" "$name" "$argc"
    done < "$ALL_FUNCS"
    echo "}"
    echo ""

    # ── sys.graphics ──
    echo "// -- sys.graphics object ------------------------------------------------------"
    echo ""
    echo "interface SysGraphics {"
    while read -r scope name argc; do
        [[ "$scope" == "graphics" ]] || continue
        emit_func "  " "graphics.$name" "$name" "$argc"
    done < "$ALL_FUNCS"
    echo "}"
    echo ""

    # ── sys.path ──
    echo "// -- sys.path object ----------------------------------------------------------"
    echo ""
    echo "interface SysPath {"
    while read -r scope name argc; do
        [[ "$scope" == "path" ]] || continue
        emit_func "  " "path.$name" "$name" "$argc"
    done < "$ALL_FUNCS"
    echo "}"
    echo ""

    # ── sys.svg ──
    echo "// -- sys.svg object -----------------------------------------------------------"
    echo ""
    echo "interface SysSVG {"
    while read -r scope name argc; do
        [[ "$scope" == "svg" ]] || continue
        emit_func "  " "svg.$name" "$name" "$argc"
    done < "$ALL_FUNCS"
    echo "}"
    echo ""

    # ── sys.font ──
    echo "// -- sys.font object ----------------------------------------------------------"
    echo ""
    echo "interface SysFont {"
    while read -r scope name argc; do
        [[ "$scope" == "font" ]] || continue
        emit_func "  " "font.$name" "$name" "$argc"
    done < "$ALL_FUNCS"
    echo "}"
    echo ""

    # ── sys.input ──
    echo "// -- sys.input object ---------------------------------------------------------"
    echo ""
    echo "interface SysInput {"
    while read -r scope name argc; do
        [[ "$scope" == "input" ]] || continue
        emit_func "  " "input.$name" "$name" "$argc"
    done < "$ALL_FUNCS"
    echo "}"
    echo ""

    # ── sys.window ──
    echo "// -- sys.window object --------------------------------------------------------"
    echo ""
    echo "interface SysWindow {"
    while read -r scope name argc; do
        [[ "$scope" == "window" ]] || continue
        emit_func "  " "window.$name" "$name" "$argc"
    done < "$ALL_FUNCS"
    echo "}"
    echo ""

    # ── sys.animation ──
    echo "// -- sys.animation object -----------------------------------------------------"
    echo ""
    echo "interface SysAnimation {"
    while read -r scope name argc; do
        [[ "$scope" == "animation" ]] || continue
        emit_func "  " "animation.$name" "$name" "$argc"
    done < "$ALL_FUNCS"
    echo "}"
    echo ""

    # ── sys.timer ──
    echo "// -- sys.timer object ---------------------------------------------------------"
    echo ""
    echo "interface SysTimer {"
    while read -r scope name argc; do
        [[ "$scope" == "timer" ]] || continue
        emit_func "  " "timer.$name" "$name" "$argc"
    done < "$ALL_FUNCS"
    echo "}"
    echo ""

    # ── sys.audio ──
    echo "// -- sys.audio object ---------------------------------------------------------"
    echo ""
    echo "interface SysAudio {"
    # Constants first
    while read -r scope cname; do
        [[ "$scope" == "audio" ]] || continue
        emit_const "  " "$cname" "audio.$cname"
    done < "$ALL_CONSTS"
    echo ""
    # Functions
    while read -r scope name argc; do
        [[ "$scope" == "audio" ]] || continue
        emit_func "  " "audio.$name" "$name" "$argc"
    done < "$ALL_FUNCS"
    echo "}"
    echo ""

    # ── sys.midi ──
    echo "// -- sys.midi object ----------------------------------------------------------"
    echo ""
    echo "interface SysMidi {"
    # Constants first
    while read -r scope cname; do
        [[ "$scope" == "midi" ]] || continue
        emit_const "  " "$cname" "midi.$cname"
    done < "$ALL_CONSTS"
    echo ""
    # Functions
    while read -r scope name argc; do
        [[ "$scope" == "midi" ]] || continue
        emit_func "  " "midi.$name" "$name" "$argc"
    done < "$ALL_FUNCS"
    echo "}"
    echo ""

    # ── sys.db ──
    echo "// -- sys.db object ------------------------------------------------------------"
    echo ""
    echo "interface SysDB {"
    while read -r scope name argc; do
        [[ "$scope" == "db" ]] || continue
        emit_func "  " "db.$name" "$name" "$argc"
    done < "$ALL_FUNCS"
    echo "}"
    echo ""

    # ── sys.files ──
    echo "// -- sys.files object ---------------------------------------------------------"
    echo ""
    echo "interface SysFiles {"
    while read -r scope name argc; do
        [[ "$scope" == "file" ]] || continue
        emit_func "  " "file.$name" "$name" "$argc"
    done < "$ALL_FUNCS"
    echo "}"
    echo ""

    echo "// -- sys.assets object --------------------------------------------------------"
    echo ""
    echo "interface SysAssets {"
    emit_func "  " "file.list" "list" "0"
    emit_func "  " "file.readText" "readText" "1"
    emit_func "  " "file.readBinary" "readBinary" "1"
    emit_func "  " "file.exists" "exists" "1"
    emit_func "  " "file.isDirectory" "isDirectory" "1"
    emit_func "  " "file.size" "size" "1"
    emit_func "  " "file.getError" "getError" "0"
    echo "}"
    echo ""

    # ── sys.sensors ──
    echo "// -- sys.sensors object -------------------------------------------------------"
    echo ""
    echo "interface SysMagneto {"
    while read -r scope name argc; do
        [[ "$scope" == "magneto" ]] || continue
        emit_func "  " "magneto.$name" "$name" "$argc"
    done < "$ALL_FUNCS"
    echo "}"
    echo ""

    # ── sys.neural ──
    echo "// -- sys.neural object --------------------------------------------------------"
    echo ""
    echo "// -- Neural inference types --------------------------------------------------"
    echo ""
    cat <<'NEURAL_TYPES'
/** Shape and type information for a single model tensor. */
interface TensorInfo {
  /** Tensor name as declared in the ONNX graph. */
  name: string;
  /** Dimension sizes. -1 indicates a dynamic dimension. */
  shape: number[];
  /** Element type: "float32" | "int32" | "int64" | "uint8" */
  dtype: string;
}

/** ONNX model metadata returned by sys.neural.getModelInfo(). */
interface ModelInfo {
  description:  string;
  producerName: string;
  graphName:    string;
  domain:       string;
  version:      number;
  inputs:       TensorInfo[];
  outputs:      TensorInfo[];
}

NEURAL_TYPES
    echo "interface SysNeural {"
    # Auto-extracted neural verbs, except `run` which is overridden together
    # with the post-init JS-shim verbs (setInput / getOutput) by the staged
    # fragment below; see src/neural/js_neural_bindings.c.
    while read -r scope name argc; do
        [[ "$scope" == "neural" ]] || continue
        [[ "$name" == "run" ]] && continue
        emit_func "  " "neural.$name" "$name" "$argc"
    done < "$ALL_FUNCS"
    cat "$FRAGMENTS_DIR/sysneural-staged.d.ts.frag"
    echo "}"
    echo ""

    cat "$FRAGMENTS_DIR/sysllamacpp.d.ts.frag"
    echo ""

    # ── sys.net.udp (interface body lives in a fragment — the QuickJS function-data
    # table is registered into a JS_NewObject + JS_SetPropertyStr(ctx, sys_obj,
    # "udp", …) pair; the function names line up but the parameter shapes use
    # literal union types beyond what argc tells us). ──
    cat "$FRAGMENTS_DIR/sysudp.d.ts.frag"
    echo ""

    # ── sys.net ──
    echo "// -- sys.net object -------------------------------------------------------"
    echo ""
    echo "interface SysNetwork {"
    emit_func "  " "network.fetch" "fetch" "2"
    echo "}"
    echo ""

    # ── sys namespace ──
    echo "// -- sys namespace ------------------------------------------------------------"
    echo ""
    echo "interface Sys {"
    echo "  /** 2D drawing primitives and paint style settings. */"
    echo "  readonly canvas: SysCanvas;"
    echo "  /** Path creation and manipulation. */"
    echo "  readonly path: SysPath;"
    echo "  /** SVG loading and drawing. */"
    echo "  readonly svg: SysSVG;"
    echo "  /** Font loading and selection. */"
    echo "  readonly font: SysFont;"
    echo "  /** Texture-backed Skia surfaces. */"
    echo "  readonly graphics: SysGraphics;"
    echo "  /** OpenGL shader pass API. */"
    echo "  readonly gl: SysGL;"
    echo "  /** In-place 3D math (vec3, quat, mat4) — gl-matrix style. */"
    echo "  readonly math: SysMath;"
    echo "  /** Input state (pointer, keyboard, timing). */"
    echo "  readonly input: SysInput;"
    echo "  /** Window dimensions and display density. */"
    echo "  readonly window: SysWindow;"
    echo "  /** Animation frame scheduling. */"
    echo "  readonly animation: SysAnimation;"
    echo "  /** Delayed and repeating callback timers. */"
    echo "  readonly timer: SysTimer;"
    echo "  /** Audio synthesis engine. */"
    echo "  readonly audio: SysAudio;"
    echo "  /** MIDI input/output. */"
    echo "  readonly midi: SysMidi;"
    echo "  /** SQLite database persistence. */"
    echo "  readonly db: SysDB;"
    echo "  /** Virtual filesystem containing read-only assets/ and writable files/. */"
    echo "  readonly files: SysFiles;"
    echo "  /** Read-only convenience wrapper over the sys.files assets/ mount. */"
    echo "  readonly assets: SysAssets;"
    echo "  /** HTTP networking. */"
    echo "  readonly network: SysNetwork;"
    echo "  /** Accelerometer and compass sensors. */"
    echo "  readonly magneto: SysMagneto;"
    echo "  /** Device-level controls. */"
    echo "  readonly device: SysDevice;"
    echo "  /** UDP datagram sockets. */"
    echo "  readonly udp: SysUDP;"
    echo "  /** Per-feature availability probes. */"
    echo "  readonly capabilities: SysCapabilities;"
    echo "  /** ONNX Runtime neural network inference (requires \"neural\": true in app.json). */"
    echo "  readonly neural: SysNeural;"
    echo "  /** Optional local GGUF inference through llama.cpp (desktop managed runtimes only). */"
    echo "  readonly llamacpp: SysLlamaCpp;"
    echo "}"
    echo ""

    # ── Global declarations ──
    echo "// -- Global declarations ------------------------------------------------------"
    echo ""
    echo "/** The Budo runtime system namespace. */"
    echo "declare const sys: Sys;"
    echo ""
    echo "/** Console output (stdout). */"
    echo "declare const console: {"
    while read -r scope name argc; do
        [[ "$scope" == "console" ]] || continue
        emit_func "  " "console.$name" "$name" "$argc"
    done < "$ALL_FUNCS"
    echo "};"
    echo ""

} > "$GENERATED_OUT"

if [[ "$CHECK_ONLY" == true ]]; then
    if ! cmp -s "$GENERATED_OUT" "$OUT"; then
        echo "stale generated declaration: ${OUT#$PROJECT_ROOT/}" >&2
        echo "run \`make generate-types\`" >&2
        exit 1
    fi
    for snapshot in "${SNAPSHOT_OUTS[@]}"; do
        if ! cmp -s "$GENERATED_OUT" "$snapshot"; then
            echo "stale generated declaration: ${snapshot#$PROJECT_ROOT/}" >&2
            echo "run \`make generate-types\`" >&2
            exit 1
        fi
    done
else
    cp "$GENERATED_OUT" "$OUT"
    for snapshot in "${SNAPSHOT_OUTS[@]}"; do
        cp "$GENERATED_OUT" "$snapshot"
    done
fi

# Count what we emitted
FUNC_COUNT=$(wc -l < "$ALL_FUNCS" | tr -d ' ')
CONST_COUNT=$(wc -l < "$ALL_CONSTS" | tr -d ' ')

if [[ "$CHECK_ONLY" == true ]]; then
    echo "Verified ${OUT#$PROJECT_ROOT/} and ${#SNAPSHOT_OUTS[@]} snapshots"
else
    echo "Generated ${OUT#$PROJECT_ROOT/} and ${#SNAPSHOT_OUTS[@]} snapshots"
fi
echo "  $FUNC_COUNT functions, $CONST_COUNT constants extracted from C sources"
echo "  $(wc -l < "$ANNOTATIONS" 2>/dev/null || echo 0 | tr -d ' ') annotation lines applied"
