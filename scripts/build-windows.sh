#!/usr/bin/env bash
# Build Budo for Windows using a QEMU-based Vagrant VM.
#
# Usage:
#   ./scripts/build-windows.sh [--setup-only] [--build-only] [--destroy]
#
# Options:
#   --setup-only   Create and provision the VM, then stop (don't build).
#   --build-only   Assume VM is running; sync, build, and fetch the binary.
#   --destroy      Destroy the VM after retrieving the binary.
#   --no-fetch     Skip fetching the binary back to the host.
#
# The Windows binary is written to: build/windows/budo.exe
#
# Prerequisites:
#   - QEMU        (brew install qemu / apt install qemu-system-x86)
#   - Vagrant     (brew install vagrant / apt install vagrant)
#   - vagrant-qemu plugin  (vagrant plugin install vagrant-qemu)
#   - A QEMU-compatible Windows Vagrant box (see README section below)

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "$SCRIPT_DIR/_common.sh"

# ── Defaults ──────────────────────────────────────────────────────────────────

VM_NAME="windows-build"
OUTPUT_DIR="$PROJECT_ROOT/build/windows"
SETUP_ONLY=false
BUILD_ONLY=false
DESTROY_AFTER=false
SKIP_FETCH=false
SSH_PORT=2222  # must match Vagrantfile forwarded_port for guest 22

# ── Parse arguments ───────────────────────────────────────────────────────────

while [[ $# -gt 0 ]]; do
    case "$1" in
        --setup-only)  SETUP_ONLY=true;  shift ;;
        --build-only)  BUILD_ONLY=true;  shift ;;
        --destroy)     DESTROY_AFTER=true; shift ;;
        --no-fetch)    SKIP_FETCH=true;  shift ;;
        *)
            echo_error "Unknown option: $1"
            echo "Usage: $0 [--setup-only] [--build-only] [--destroy] [--no-fetch]"
            exit 1
            ;;
    esac
done

# ── Prerequisite checks ──────────────────────────────────────────────────────

check_prerequisites() {
    echo_step "Checking prerequisites..."
    local missing=0

    require_cmd python3 "python3"

    if ! command -v qemu-system-x86_64 &>/dev/null; then
        echo_error "qemu-system-x86_64 not found."
        echo "  macOS:  brew install qemu"
        echo "  Linux:  sudo apt install qemu-system-x86 qemu-utils"
        missing=1
    fi

    if ! command -v vagrant &>/dev/null; then
        echo_error "vagrant not found."
        echo "  macOS:  brew install vagrant"
        echo "  Linux:  sudo apt install vagrant"
        missing=1
    fi

    if vagrant plugin list 2>/dev/null | grep -q vagrant-qemu; then
        echo_info "vagrant-qemu plugin found."
    else
        echo_warn "vagrant-qemu plugin not installed — installing now..."
        vagrant plugin install vagrant-qemu || {
            echo_error "Failed to install vagrant-qemu plugin."
            missing=1
        }
    fi

    if [[ $missing -ne 0 ]]; then
        echo_error "Missing prerequisites. Please install them and try again."
        exit 1
    fi

    echo_info "All prerequisites satisfied."
}

# ── VM management ─────────────────────────────────────────────────────────────

vm_status() {
    cd "$PROJECT_ROOT"
    vagrant status "$VM_NAME" 2>/dev/null | grep "$VM_NAME" | awk '{print $2}'
}

ensure_vm_running() {
    local status
    status="$(vm_status)"

    case "$status" in
        running)
            echo_info "VM is already running."
            ;;
        saved|suspended)
            echo_step "Resuming VM..."
            vagrant resume "$VM_NAME"
            ;;
        poweroff|shutoff|aborted)
            echo_step "Starting VM..."
            vagrant up "$VM_NAME" --provider=qemu --no-provision
            ;;
        "not")
            # "not created" — two-word status
            echo_step "Creating and provisioning VM (this may take a while on first run)..."
            vagrant up "$VM_NAME" --provider=qemu
            ;;
        *)
            echo_step "Starting VM (status: ${status:-unknown})..."
            vagrant up "$VM_NAME" --provider=qemu
            ;;
    esac
}

# ── Sync source code ─────────────────────────────────────────────────────────

sync_source() {
    echo_step "Syncing source code to VM..."
    cd "$PROJECT_ROOT"
    vagrant rsync "$VM_NAME"
}

# ── Run the build ─────────────────────────────────────────────────────────────

run_build() {
    echo_step "Running Windows build inside VM..."
    cd "$PROJECT_ROOT"
    vagrant provision "$VM_NAME" --provision-with build
}

# ── Fetch the binary via SCP ─────────────────────────────────────────────────

fetch_binary() {
    echo_step "Fetching build artifacts from VM..."

    mkdir -p "$OUTPUT_DIR"

    # Detect actual forwarded SSH port (auto_correct may change it)
    local actual_port
    actual_port=$(cd "$PROJECT_ROOT" && vagrant port "$VM_NAME" --guest 22 2>/dev/null || echo "$SSH_PORT")

    local vagrant_key="$HOME/.vagrant.d/insecure_private_key"
    local scp_opts="-P $actual_port -i $vagrant_key -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null -o LogLevel=ERROR"

    echo_info "Using SSH port $actual_port"

    # Download budo.exe
    # shellcheck disable=SC2086
    scp $scp_opts "vagrant@127.0.0.1:C:/budo-output/budo.exe" "$OUTPUT_DIR/budo.exe" || {
        echo_error "Failed to download budo.exe"
        echo_warn  "You can retrieve it manually:"
        echo       "  scp $scp_opts vagrant@127.0.0.1:C:/budo-output/budo.exe build/windows/"
        return 1
    }

    # Download SDL2.dll if present
    # shellcheck disable=SC2086
    scp $scp_opts "vagrant@127.0.0.1:C:/budo-output/SDL2.dll" "$OUTPUT_DIR/SDL2.dll" 2>/dev/null || true

    # Download wasmtime DLL if present
    # shellcheck disable=SC2086
    scp $scp_opts "vagrant@127.0.0.1:C:/budo-output/wasmtime*.dll" "$OUTPUT_DIR/" 2>/dev/null || true
    # shellcheck disable=SC2086
    scp $scp_opts "vagrant@127.0.0.1:C:/budo-output/libssl-*.dll" "$OUTPUT_DIR/" 2>/dev/null || true
    # shellcheck disable=SC2086
    scp $scp_opts "vagrant@127.0.0.1:C:/budo-output/libcrypto-*.dll" "$OUTPUT_DIR/" 2>/dev/null || true

    echo_info "Artifacts saved to $OUTPUT_DIR/"
    ls -lh "$OUTPUT_DIR/"

    local zip_path="$OUTPUT_DIR/Budo-windows-x64.zip"
    echo_step "Creating Windows download bundle..."
    python3 - "$OUTPUT_DIR" "$zip_path" <<'PY'
import glob
import os
import sys
import zipfile

output_dir, zip_path = sys.argv[1:3]
patterns = ["budo.exe", "SDL2.dll", "wasmtime*.dll", "libssl-*.dll", "libcrypto-*.dll"]
files = []
for pattern in patterns:
    files.extend(glob.glob(os.path.join(output_dir, pattern)))

if not files:
    raise SystemExit("No Windows files found to package")

with zipfile.ZipFile(zip_path, "w", zipfile.ZIP_DEFLATED) as zf:
    for path in sorted(set(files)):
        zf.write(path, os.path.basename(path))
PY
    echo_info "Windows ZIP created: $zip_path"
    stage_website_download "$zip_path" "Budo-windows-x64.zip"
}

# ── Main ──────────────────────────────────────────────────────────────────────

main() {
    check_prerequisites

    cd "$PROJECT_ROOT"

    if $SETUP_ONLY; then
        ensure_vm_running
        echo_info "VM is provisioned and running. Use --build-only to build."
        return 0
    fi

    if $BUILD_ONLY; then
        sync_source
        run_build
        if ! $SKIP_FETCH; then
            fetch_binary
        fi
    else
        ensure_vm_running
        sync_source
        run_build
        if ! $SKIP_FETCH; then
            fetch_binary
        fi
    fi

    if $DESTROY_AFTER; then
        echo_step "Destroying VM..."
        vagrant destroy "$VM_NAME" -f
    fi

    echo ""
    echo_info "Windows build complete!"
    if ! $SKIP_FETCH && [ -f "$OUTPUT_DIR/budo.exe" ]; then
        echo "  Binary: $OUTPUT_DIR/budo.exe"
    fi
}

main "$@"
