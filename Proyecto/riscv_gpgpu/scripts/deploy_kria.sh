#!/usr/bin/env bash
# deploy_kria.sh - End-to-end Kria KV260/KR260 deployment (T055)
#
# Cross-compiles the software stack for aarch64, transfers the artifacts and
# the kernel ELF to the Kria board, loads the FPGA bitstream via fpgautil,
# runs the requested test, and produces a pass/fail report.
#
# Usage:
#   scripts/deploy_kria.sh --bitstream <file.bit.bin> --kernel <kernel.elf> --test <test_binary> \
#       [--host <user@kria-ip>] [--report <path>] [--skip-build]
#
# Requirements on the build host: aarch64-linux-gnu-g++ toolchain, cmake, ssh/scp.
# Requirements on the Kria board:  fpgautil, an accessible SSH account (default: ubuntu@kria).

set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

BITSTREAM=""
KERNEL=""
TEST_BIN=""
KRIA_HOST="${KRIA_HOST:-ubuntu@kria}"
REPORT="${REPO_ROOT}/docs/verification/kria_results.md"
SKIP_BUILD=0
BUILD_DIR="${REPO_ROOT}/build-kria-aarch64"
REMOTE_DIR="/home/${KRIA_HOST%%@*}/riscv_gpgpu_deploy"

usage() { grep '^#' "$0" | sed 's/^# \{0,1\}//'; exit 1; }

while [[ $# -gt 0 ]]; do
    case "$1" in
        --bitstream) BITSTREAM="$2"; shift 2 ;;
        --kernel)    KERNEL="$2"; shift 2 ;;
        --test)      TEST_BIN="$2"; shift 2 ;;
        --host)      KRIA_HOST="$2"; shift 2 ;;
        --report)    REPORT="$2"; shift 2 ;;
        --skip-build) SKIP_BUILD=1; shift ;;
        -h|--help)   usage ;;
        *) echo "Unknown argument: $1" >&2; usage ;;
    esac
done

[[ -n "$BITSTREAM" && -n "$KERNEL" && -n "$TEST_BIN" ]] || {
    echo "ERROR: --bitstream, --kernel, and --test are required." >&2; usage; }
[[ -f "$BITSTREAM" ]] || { echo "ERROR: bitstream not found: $BITSTREAM" >&2; exit 1; }
[[ -f "$KERNEL" ]]    || { echo "ERROR: kernel ELF not found: $KERNEL" >&2; exit 1; }

log() { echo "[deploy_kria] $*"; }

# ── 1. Cross-compile the software stack for aarch64 ─────────────────────────
if [[ "$SKIP_BUILD" -eq 0 ]]; then
    command -v aarch64-linux-gnu-g++ >/dev/null || {
        echo "ERROR: aarch64-linux-gnu-g++ not found. Install gcc-aarch64-linux-gnu." >&2; exit 1; }
    log "Cross-compiling for aarch64 into ${BUILD_DIR}"
    cmake -S "$REPO_ROOT" -B "$BUILD_DIR" \
        -DCMAKE_TOOLCHAIN_FILE="${REPO_ROOT}/fpga/toolchain-aarch64.cmake" \
        -DFPGA_TARGET=ON \
        -DBUILD_SYSTEMC_MODELS=OFF \
        -DBUILD_SYSTEMC_INTEGRATION=OFF \
        -DBUILD_BENCHMARKS=OFF \
        -DBUILD_TESTS=OFF
    cmake --build "$BUILD_DIR" -j"$(nproc)"
fi

TEST_PATH="${BUILD_DIR}/bin/${TEST_BIN}"
[[ -f "$TEST_PATH" ]] || TEST_PATH="$TEST_BIN"
[[ -f "$TEST_PATH" ]] || { echo "ERROR: test binary not found: $TEST_BIN" >&2; exit 1; }

# ── 2. Transfer artifacts to the board ───────────────────────────────────────
log "Transferring artifacts to ${KRIA_HOST}:${REMOTE_DIR}"
ssh "$KRIA_HOST" "mkdir -p '$REMOTE_DIR'"
scp "$BITSTREAM" "$KERNEL" "$TEST_PATH" "$KRIA_HOST:$REMOTE_DIR/"

BITSTREAM_NAME="$(basename "$BITSTREAM")"
KERNEL_NAME="$(basename "$KERNEL")"
TEST_NAME="$(basename "$TEST_PATH")"

# ── 3. Load the bitstream and run the test on the board ─────────────────────
log "Loading bitstream and running ${TEST_NAME} on the board"
RUN_LOG="$(mktemp "${TMPDIR:-/tmp}/kria_run.XXXXXX.log")"
STATUS=PASS
if ! ssh "$KRIA_HOST" bash -s -- \
        "$REMOTE_DIR" "$BITSTREAM_NAME" "$KERNEL_NAME" "$TEST_NAME" <<'REMOTE' >"$RUN_LOG" 2>&1
set -euo pipefail
REMOTE_DIR="$1"; BITSTREAM="$2"; KERNEL="$3"; TEST="$4"
cd "$REMOTE_DIR"
sudo fpgautil -b "$BITSTREAM"
chmod +x "$TEST"
GPGPU_KERNEL_ELF="$REMOTE_DIR/$KERNEL" "./$TEST"
REMOTE
then
    STATUS=FAIL
fi
cat "$RUN_LOG"

# ── 4. Emit pass/fail report ──────────────────────────────────────────────────
mkdir -p "$(dirname "$REPORT")"
{
    echo "# Kria Deployment Report"
    echo
    echo "- Date: $(date -u '+%Y-%m-%d %H:%M UTC')"
    echo "- Board: ${KRIA_HOST}"
    echo "- Bitstream: ${BITSTREAM_NAME}"
    echo "- Kernel ELF: ${KERNEL_NAME}"
    echo "- Test: ${TEST_NAME}"
    echo "- Result: **${STATUS}**"
    echo
    echo "## Test output"
    echo
    echo '```'
    cat "$RUN_LOG"
    echo '```'
} > "$REPORT"
rm -f "$RUN_LOG"

log "Report written to ${REPORT}"
log "Result: ${STATUS}"
[[ "$STATUS" == PASS ]]
