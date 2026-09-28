#!/usr/bin/env bash
# Shared utilities for Budo build scripts.
# Source this file near the top of every script:
#   source "$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)/_common.sh"

set -euo pipefail

# ── Path helpers ──────────────────────────────────────────────────────────────

# Derive PROJECT_ROOT from this shared helper's location. Some feature packs are
# nested Git repositories, so asking Git from the caller's directory can resolve
# to the feature-pack root instead of the main project root.
COMMON_SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd -L)"
PROJECT_ROOT="$(cd "$COMMON_SCRIPT_DIR/.." && pwd -L)"

BUDO_DEPENDENCY_LOCK="$PROJECT_ROOT/cmake/BudoDependencyLock.cmake"

budo_dependency_lock_value() {
    local name="$1"
    local value
    if [ ! -f "$BUDO_DEPENDENCY_LOCK" ]; then
        echo "Missing dependency lock: $BUDO_DEPENDENCY_LOCK" >&2
        return 1
    fi
    value="$(sed -nE "s/^set\\(${name} \"([^\"]+)\"\\)$/\\1/p" "$BUDO_DEPENDENCY_LOCK")"
    if [ -z "$value" ]; then
        echo "Dependency lock value not found: $name" >&2
        return 1
    fi
    printf '%s\n' "$value"
}

# ── Project-local Emscripten SDK ─────────────────────────────────────────────

BUDO_DEFAULT_EMSDK_VERSION="$(budo_dependency_lock_value BUDO_EMSDK_VERSION)"

activate_vendored_emscripten() {
    local emsdk_version="${BUDO_EMSDK_VERSION:-${EMSDK_VERSION:-$BUDO_DEFAULT_EMSDK_VERSION}}"
    local emsdk_dir="${BUDO_EMSDK_DIR:-${EMSDK_DIR:-$PROJECT_ROOT/third_party/emsdk}}"
    local emsdk_stamp
    local emsdk_physical
    local emsdk_node_bin

    if [[ ! "$emsdk_dir" = /* ]]; then
        emsdk_dir="$PROJECT_ROOT/$emsdk_dir"
    fi
    emsdk_stamp="$emsdk_dir/.budo-emsdk-version"

    if [ ! -f "$emsdk_dir/emsdk_env.sh" ] || \
       [ ! -x "$emsdk_dir/upstream/emscripten/emcc" ] || \
       [ ! -f "$emsdk_stamp" ] || \
       [ "$(cat "$emsdk_stamp" 2>/dev/null || true)" != "$emsdk_version" ]; then
        echo_info "Preparing project-local Emscripten SDK $emsdk_version..."
        BUDO_EMSDK_VERSION="$emsdk_version" BUDO_EMSDK_DIR="$emsdk_dir" \
            bash "$PROJECT_ROOT/scripts/setup-emscripten.sh"
    fi

    export EMSDK_QUIET=1
    export EM_CONFIG="$emsdk_dir/.emscripten"
    export EM_CACHE="$emsdk_dir/upstream/emscripten/cache"

    # emsdk can leave Python bytecode behind when switching versions. Remove it
    # so the vendored tools cannot accidentally execute stale module code from a
    # different Emscripten release.
    find "$emsdk_dir/upstream/emscripten" -name __pycache__ -type d -prune -exec rm -rf {} + 2>/dev/null || true

    # shellcheck disable=SC1091
    source "$emsdk_dir/emsdk_env.sh" >/dev/null

    emsdk_physical="$(cd "$emsdk_dir" && pwd -P)"
    emsdk_node_bin="$(find "$emsdk_physical/node" -mindepth 3 -maxdepth 3 -type f -name node -perm -111 -printf '%h\n' 2>/dev/null | head -n 1 || true)"

    PATH="$(printf '%s' "$PATH" | awk -v RS=: -v ORS=: \
        -v logical="$emsdk_dir" -v physical="$emsdk_physical" \
        '$0 != logical && $0 != logical "/upstream/emscripten" && index($0, logical "/node/") != 1 && $0 != physical && $0 != physical "/upstream/emscripten" && index($0, physical "/node/") != 1 { print }')"
    PATH="$emsdk_physical:$emsdk_physical/upstream/emscripten${emsdk_node_bin:+:$emsdk_node_bin}:${PATH%:}"
    export EMSDK="$emsdk_physical"
    export PATH

    export EM_CONFIG="$emsdk_physical/.emscripten"
    export EM_CACHE="$emsdk_physical/upstream/emscripten/cache"

    require_cmd emcc "project-local Emscripten (emcc)"
    require_cmd emcmake "project-local Emscripten (emcmake)"
    require_cmd emmake "project-local Emscripten (emmake)"

    export BUDO_EMSDK_VERSION="$emsdk_version"
    export BUDO_EMSDK_DIR="$emsdk_physical"
}

reset_web_build_if_emscripten_changed() {
    local build_dir="$1"
    local cache_file="$build_dir/CMakeCache.txt"

    if [ -f "$cache_file" ] && ! grep -Fq "$BUDO_EMSDK_DIR" "$cache_file"; then
        echo_info "Existing web build was configured with another Emscripten SDK; resetting $build_dir"
        rm -rf "$build_dir"
    fi
}

# ── Terminal colours ──────────────────────────────────────────────────────────

RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
CYAN='\033[0;36m'
NC='\033[0m'

echo_info()  { echo -e "${GREEN}[INFO]${NC}  $1"; }
echo_warn()  { echo -e "${YELLOW}[WARN]${NC}  $1"; }
echo_error() { echo -e "${RED}[ERROR]${NC} $1"; }
echo_step()  { echo -e "${CYAN}[STEP]${NC}  $1"; }

stage_website_download() {
    local src="$1"
    local dest_name
    if [[ $# -ge 2 ]]; then
        dest_name="$2"
    else
        dest_name="$(basename "$src")"
    fi
    local download_dir="$PROJECT_ROOT/website/download"
    local dest="$download_dir/$dest_name"

    if [[ ! -e "$src" ]]; then
        echo_error "Artifact not found: $src"
        return 1
    fi

    mkdir -p "$download_dir"
    rm -rf "$dest"
    cp -R "$src" "$dest"
    echo_info "Website download staged: $dest"
}

# ── Cross-platform sed -i ────────────────────────────────────────────────────

sedi() {
    if [[ "$OSTYPE" == "darwin"* ]]; then
        sed -i '' "$@"
    else
        sed -i "$@"
    fi
}

# ── JSON helpers (uses python3, no jq dependency) ────────────────────────────

json_get() {
    # json_get <file> <key> [default]
    local file="$1" key="$2" default="${3:-}"
    python3 -c "
import json, sys
with open(sys.argv[1]) as f:
    d = json.load(f)
v = d.get(sys.argv[2])
if v is None:
    print(sys.argv[3])
elif isinstance(v, bool):
    print('true' if v else 'false')
elif isinstance(v, list):
    print(' '.join(str(x) for x in v))
else:
    print(v)
" "$file" "$key" "$default" 2>/dev/null || echo "$default"
}

json_get_nested() {
    # json_get_nested <file> <dot.separated.keys> [default]
    local file="$1" keys="$2" default="${3:-}"
    python3 -c "
import json, sys
with open(sys.argv[1]) as f:
    d = json.load(f)
keys = sys.argv[2].split('.')
v = d
for k in keys:
    if isinstance(v, dict):
        v = v.get(k)
    else:
        v = None
        break
if v is None:
    print(sys.argv[3])
elif isinstance(v, bool):
    print('true' if v else 'false')
elif isinstance(v, list):
    print(' '.join(str(x) for x in v))
else:
    print(v)
" "$file" "$keys" "$default" 2>/dev/null || echo "$default"
}

# ── Java version check (Android Gradle Plugin 8.x needs Java 11-21) ─────────

check_java_version() {
    local java_version
    java_version=$(java -version 2>&1 | head -1 | cut -d'"' -f2 | cut -d'.' -f1)

    # Handle "1.8" format (Java 8) vs "11", "17" format
    if [ "$java_version" = "1" ]; then
        java_version=$(java -version 2>&1 | head -1 | cut -d'"' -f2 | cut -d'.' -f2)
    fi

    if [ "$java_version" -lt 11 ] 2>/dev/null || [ "$java_version" -gt 21 ] 2>/dev/null; then
        for jv in 21 17; do
            for prefix in /opt/homebrew/opt /usr/local/opt; do
                local jpath="$prefix/openjdk@${jv}/libexec/openjdk.jdk/Contents/Home"
                if [ -d "$jpath" ]; then
                    export JAVA_HOME="$jpath"
                    echo_info "Using Homebrew OpenJDK $jv: $JAVA_HOME"
                    return
                fi
            done
        done
        echo_error "Java 11-21 not found. Install with: brew install openjdk@21"
        exit 1
    fi
}

# ── Requirement checks ───────────────────────────────────────────────────────

require_cmd() {
    # require_cmd <command> [human-readable name]
    if ! command -v "$1" &>/dev/null; then
        echo_error "${2:-$1} is required but not installed"
        exit 1
    fi
}

resolve_adb() {
    if command -v adb >/dev/null 2>&1; then
        ADB_BIN="$(command -v adb)"
        export ADB_BIN
        return 0
    fi

    if command -v adb.exe >/dev/null 2>&1; then
        ADB_BIN="$(command -v adb.exe)"
        export ADB_BIN
        return 0
    fi

    for candidate in \
        "$HOME/Android/Sdk/platform-tools/adb" \
        "$HOME/Android/sdk/platform-tools/adb" \
        "$HOME/Library/Android/sdk/platform-tools/adb" \
        "${ANDROID_HOME:-}/platform-tools/adb" \
        "${ANDROID_SDK_ROOT:-}/platform-tools/adb" \
        "${LOCALAPPDATA:-}/Android/Sdk/platform-tools/adb.exe"
    do
        if [ -n "$candidate" ] && [ -x "$candidate" ]; then
            ADB_BIN="$candidate"
            export ADB_BIN
            return 0
        fi
    done

    echo_error "adb not found. Install Android platform-tools or add adb to PATH."
    exit 1
}
