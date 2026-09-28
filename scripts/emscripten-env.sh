#!/usr/bin/env bash
# Source this file to use Budo's project-local Emscripten SDK.
#
# Usage:
#   source ./scripts/emscripten-env.sh
#   emcmake cmake ...

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd -L)"
source "$SCRIPT_DIR/_common.sh"

activate_vendored_emscripten
