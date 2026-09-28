#!/usr/bin/env bash
set -euo pipefail

PROJECT_ROOT="$(cd "${1:?project root is required}" && pwd -P)"
FIXTURE_ROOT="$(mktemp -d "${TMPDIR:-/tmp}/budo-install-test.XXXXXX")"
trap 'rm -rf "$FIXTURE_ROOT"' EXIT

FAKE_BIN="$FIXTURE_ROOT/bin"
LOG="$FIXTURE_ROOT/commands.log"
mkdir -p "$FAKE_BIN"

cat > "$FAKE_BIN/id" <<'SH'
#!/bin/sh
if [ "$1" = "-u" ]; then
    echo 0
    exit 0
fi
exit 2
SH

cat > "$FAKE_BIN/find" <<'SH'
#!/bin/sh
printf 'find:%s\n' "$*" >> "$BUDO_INSTALL_TEST_LOG"
SH

cat > "$FAKE_BIN/make" <<'SH'
#!/bin/sh
printf 'make:%s\n' "$*" >> "$BUDO_INSTALL_TEST_LOG"
SH

cat > "$FAKE_BIN/cmake" <<'SH'
#!/bin/sh
printf 'cmake:%s\n' "$*" >> "$BUDO_INSTALL_TEST_LOG"
SH

cat > "$FAKE_BIN/sudo" <<'SH'
#!/bin/sh
printf 'sudo:%s\n' "$*" >> "$BUDO_INSTALL_TEST_LOG"
[ "$1" = "-u" ] || exit 2
shift 2
[ "$1" = "-H" ] || exit 2
shift
"$@"
SH

chmod +x "$FAKE_BIN"/*

PATH="$FAKE_BIN:$PATH" \
BUDO_INSTALL_TEST_LOG="$LOG" \
MAKE_COMMAND="$FAKE_BIN/make" \
SUDO_USER="test-user" \
SUDO_UID="1234" \
SUDO_GID="5678" \
    "$PROJECT_ROOT/scripts/install.sh" build

grep -F "sudo:-u test-user -H env BUILD_DIR=build BUILD_TYPE=Release CMAKE_FLAGS= QUICKJS_IMPL= BUDO_ANDROID_PACK_ROOT=private/android $FAKE_BIN/make --no-print-directory build" "$LOG" >/dev/null
grep -F "make:--no-print-directory build" "$LOG" >/dev/null
grep -F "cmake:--install build" "$LOG" >/dev/null

find_count="$(grep -c '^find:' "$LOG")"
if [ "$find_count" -ne 2 ]; then
    echo "Expected ownership repair before and after install, got $find_count calls" >&2
    cat "$LOG" >&2
    exit 1
fi
