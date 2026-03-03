#!/usr/bin/env bash
# Automated test runner for ImposOS
# Boots QEMU with -append autotest, captures serial output, parses results.
#
# Usage: ./test_auto.sh [--no-build] [--timeout SECS]
#
# Detects architecture from ARCH env var or the built kernel binary.
# Exit code: 0 if all tests pass, 1 if any test fails or timeout.

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
cd "$SCRIPT_DIR"

TIMEOUT=180
BUILD=1
LOG_FILE="test_output.log"

while [[ $# -gt 0 ]]; do
    case "$1" in
        --no-build)  BUILD=0; shift ;;
        --timeout)   TIMEOUT="$2"; shift 2 ;;
        *)           echo "Unknown option: $1"; exit 1 ;;
    esac
done

# ── Detect architecture ──────────────────────────────────────────
ARCH="${ARCH:-}"
if [[ -z "$ARCH" ]] && [[ -f kernel/myos.kernel ]]; then
    if file kernel/myos.kernel | grep -q 'aarch64'; then
        ARCH=aarch64
    else
        ARCH=i386
    fi
fi
ARCH="${ARCH:-aarch64}"
echo "==> Architecture: $ARCH"

# ── Build ──────────────────────────────────────────────────────────
if [[ "$BUILD" -eq 1 ]]; then
    echo "==> Building ImposOS ($ARCH)..."
    if [[ "$ARCH" == "i386" ]]; then
        make build-i386 -j"$(nproc)" 2>&1 | tail -3
    else
        make -j"$(nproc)" 2>&1 | tail -3
    fi
fi

# ── Detect KVM ─────────────────────────────────────────────────────
KVM_FLAG=""
if [[ "$ARCH" == "i386" ]] && [[ -w /dev/kvm ]] 2>/dev/null; then
    KVM_FLAG="-enable-kvm -cpu host"
fi

# ── Detect initrd modules ─────────────────────────────────────────
INITRD_MODS=""
if [[ -f doom1.wad ]]; then
    INITRD_MODS="-initrd doom1.wad,initrd.tar"
elif [[ -f initrd.tar ]]; then
    INITRD_MODS="-initrd initrd.tar"
fi

# ── Run QEMU ───────────────────────────────────────────────────────
echo "==> Starting QEMU (timeout: ${TIMEOUT}s)..."
rm -f "$LOG_FILE"

if [[ "$ARCH" == "aarch64" ]]; then
    # aarch64: QEMU virt machine, serial console
    timeout "$TIMEOUT" qemu-system-aarch64 \
        -machine virt \
        -cpu cortex-a72 \
        -smp 2 \
        -m 8G \
        -kernel kernel/myos.kernel \
        -nographic \
        -no-reboot \
        > "$LOG_FILE" 2>/dev/null || true
else
    # i386: legacy QEMU with IDE disk, RTL8139, VGA
    DISK_IMAGE="impos_disk_test.img"
    rm -f "$DISK_IMAGE"
    qemu-img create -f raw "$DISK_IMAGE" 280M >/dev/null 2>&1

    timeout "$TIMEOUT" qemu-system-i386 \
        -kernel kernel/myos.kernel \
        $INITRD_MODS \
        -append autotest \
        -drive "file=$DISK_IMAGE,format=raw,if=ide,index=0,media=disk" \
        -netdev user,id=net0 \
        -device rtl8139,netdev=net0 \
        -m 4G \
        -nographic \
        -no-reboot \
        $KVM_FLAG \
        > "$LOG_FILE" 2>/dev/null || true

    rm -f "$DISK_IMAGE"
fi

# ── Parse results ──────────────────────────────────────────────────
if [[ ! -s "$LOG_FILE" ]]; then
    echo "FAIL: No output captured (QEMU may have crashed or timed out)"
    exit 1
fi

# Check for the autotest markers
if ! grep -q "\[AUTOTEST\] Starting" "$LOG_FILE"; then
    echo "FAIL: Autotest did not start (boot failure?)"
    echo "--- Last 20 lines of output ---"
    tail -20 "$LOG_FILE"
    exit 1
fi

if ! grep -q "\[AUTOTEST\] Done" "$LOG_FILE"; then
    echo "FAIL: Autotest did not complete (crash or timeout)"
    echo "--- Last 20 lines of output ---"
    tail -20 "$LOG_FILE"
    exit 1
fi

# Extract the results line: "=== Results: X/Y passed, Z FAILED ==="
RESULTS_LINE=$(grep "=== Results:" "$LOG_FILE" | head -1)

if [[ -z "$RESULTS_LINE" ]]; then
    echo "FAIL: Could not find results summary"
    exit 1
fi

echo "$RESULTS_LINE"

# Known failures in headless mode (no GPU) and external service tests
KNOWN_HEADLESS=(
    "gfx is active"
    "gfx width > 0"
    "gfx height > 0"
    "gfx bpp == 32"
    "gfx backbuffer not null"
    "GFX_RGB black"
    "GFX_RGB white"
    "GFX_RGB red"
    "GFX_RGB green"
    "http: GET https://google.com succeeds"
    "http: google.com status"
    "http: google.com body"
)

# Print any FAIL lines, filtering out known headless failures
FAIL_LINES=$(grep "  FAIL " "$LOG_FILE" || true)
UNEXPECTED=""
if [[ -n "$FAIL_LINES" ]]; then
    while IFS= read -r line; do
        skip=0
        for known in "${KNOWN_HEADLESS[@]}"; do
            if [[ "$line" == *"$known"* ]]; then
                skip=1
                break
            fi
        done
        if [[ "$skip" -eq 0 ]]; then
            UNEXPECTED+="$line"$'\n'
        fi
    done <<< "$FAIL_LINES"
fi

KNOWN_COUNT=$(echo "$FAIL_LINES" | grep -c "  FAIL " 2>/dev/null || echo 0)
UNEXPECTED_COUNT=$(echo -n "$UNEXPECTED" | grep -c "  FAIL " 2>/dev/null || echo 0)

if [[ -n "$UNEXPECTED" ]]; then
    echo ""
    echo "Unexpected failures ($UNEXPECTED_COUNT):"
    echo "$UNEXPECTED"
    if [[ "$KNOWN_COUNT" -gt "$UNEXPECTED_COUNT" ]]; then
        echo "(+ $((KNOWN_COUNT - UNEXPECTED_COUNT)) known headless failures skipped)"
    fi
    exit 1
fi

if [[ "$KNOWN_COUNT" -gt 0 ]]; then
    echo "($KNOWN_COUNT known headless failures skipped: gfx/network)"
fi

echo "All tests passed."
exit 0
