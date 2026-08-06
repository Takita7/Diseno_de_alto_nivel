#!/usr/bin/env bash
# run_fpga_rodinia_matrix.sh - Run the Rodinia BFS benchmark matrix on FPGA hardware.
#
# Mirrors run_rodinia_real_matrix.sh but uses the real FPGA instead of SystemC
# simulation. Iterates over warp-count configurations, records elapsed_ms,
# and writes summary TSV + JSON to results/benchmarks/fpga_rodinia_matrix/.
#
# Prerequisites:
#   1. Board has been set up (memmap=64M$0x60000000 active):
#        bash scripts/setup_kria_fpga_mem.sh && ssh kria 'sudo reboot'
#   2. Bitstream loaded:
#        bash scripts/deploy_kria.sh --skip-build --bitstream ... --sudo-pass ...
#   3. fpga_rodinia_bench aarch64 binary + FPGA-linked kernel ELF deployed:
#        (this script copies them over via SSH)
#
# Usage:
#   bash scripts/benchmark/run_fpga_rodinia_matrix.sh [N_WARPS ...]
#
# Environment:
#   KRIA_HOST     - SSH host (default: kria, override for jump host)
#   KRIA_USER     - SSH user (default: ubuntu)
#   SUDO_PASS     - sudo password on board (default: petalinux)
#   BUILD_DIR     - cross-build directory containing aarch64 binaries
#   KERNEL_ELF    - RISC-V ELF (default: OCM-linked, no memmap needed)
#   TIMEOUT_MS    - per-run kernel timeout in ms (default: 10000)
#   DRY_RUN       - set non-empty to print commands without executing

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(dirname "$(dirname "$SCRIPT_DIR")")"
BUILD_DIR="${BUILD_DIR:-$PROJECT_ROOT/build-kria-aarch64}"
# RISC-V kernels are host-independent; prefer native build where benchmarks are on.
NATIVE_BUILD_DIR="${NATIVE_BUILD_DIR:-$PROJECT_ROOT/build-all}"
KRIA_HOST="${KRIA_HOST:-kria}"
KRIA_USER="${KRIA_USER:-ubuntu}"
SUDO_PASS="${SUDO_PASS:-petalinux}"
TIMEOUT_MS="${TIMEOUT_MS:-10000}"
DRY_RUN="${DRY_RUN:-}"
DEFAULT_WARPS="${FPGA_WARP_VALUES:-1 2 4 8 16 32}"
KERNEL_ELF="${KERNEL_ELF:-$NATIVE_BUILD_DIR/kernels/rodinia_bfs_kernel_ocm.elf}"
BENCH_BIN="${BENCH_BIN:-$BUILD_DIR/bin/fpga_rodinia_bench}"
RESULTS_DIR="${RESULTS_DIR:-$PROJECT_ROOT/results/benchmarks/fpga_rodinia_matrix}"
SUMMARY_TSV="$RESULTS_DIR/summary.tsv"
SUMMARY_JSON="$RESULTS_DIR/summary.json"

SSH_CMD="ssh ${KRIA_USER}@${KRIA_HOST}"
REMOTE_DIR="/home/${KRIA_USER}/fpga_bench"

# ── Check local binaries ──────────────────────────────────────────────────────
for f in "$BENCH_BIN" "$KERNEL_ELF"; do
    if [[ ! -f "$f" ]]; then
        echo "ERROR: file not found: $f" >&2
        echo "Build with:" >&2
        echo "  cmake -B build-kria-aarch64 -DFPGA_TARGET=ON -DCMAKE_TOOLCHAIN_FILE=... ." >&2
        echo "  cmake --build build-kria-aarch64 --target fpga_rodinia_bench" >&2
        exit 1
    fi
done

# ── Parse warp-count list ─────────────────────────────────────────────────────
if [[ $# -gt 0 ]]; then
    warp_values=("$@")
else
    read -r -a warp_values <<< "$DEFAULT_WARPS"
fi

mkdir -p "$RESULTS_DIR"
cat > "$SUMMARY_TSV" <<'EOF'
warps	status	elapsed_ms	log
EOF

# ── Wait for board SSH to be reachable ────────────────────────────────────────
wait_for_ssh() {
    local max_wait=180 waited=0
    echo "Waiting for board SSH at ${KRIA_USER}@${KRIA_HOST}..."
    while ! ssh -o ConnectTimeout=4 -o BatchMode=yes \
               "${KRIA_USER}@${KRIA_HOST}" 'true' 2>/dev/null; do
        sleep 5; waited=$((waited + 5))
        echo "  ${waited}s / ${max_wait}s ..."
        if [[ $waited -ge $max_wait ]]; then
            echo "ERROR: board did not respond after ${max_wait}s" >&2; return 1
        fi
    done
    echo "  board is up"
}

# ── Deploy binaries to board ──────────────────────────────────────────────────
echo "Deploying binaries to ${KRIA_USER}@${KRIA_HOST}:${REMOTE_DIR}..."
if [[ -z "$DRY_RUN" ]]; then
    $SSH_CMD "mkdir -p ${REMOTE_DIR}"
    scp "$BENCH_BIN" "$KERNEL_ELF" "${KRIA_USER}@${KRIA_HOST}:${REMOTE_DIR}/"
fi

BIN_NAME="$(basename "$BENCH_BIN")"
ELF_NAME="$(basename "$KERNEL_ELF")"

echo ""
echo "FPGA Rodinia BFS matrix benchmark"
echo "  binary:  ${REMOTE_DIR}/${BIN_NAME}"
echo "  kernel:  ${REMOTE_DIR}/${ELF_NAME}"
echo "  warps:   ${warp_values[*]}"
echo "  timeout: ${TIMEOUT_MS} ms"
echo ""

passes=0
failures=0
json_rows="["

for warps in "${warp_values[@]}"; do
    name="warps${warps}"
    log_file="$RESULTS_DIR/${name}.log"

    echo "== warps=${warps} =="

    cmd="echo '${SUDO_PASS}' | sudo -S env \
GPGPU_KERNEL_ELF=${REMOTE_DIR}/${ELF_NAME} \
GPGPU_TOTAL_WARPS=${warps} \
GPGPU_TIMEOUT_MS=${TIMEOUT_MS} \
${REMOTE_DIR}/${BIN_NAME}"

    start_ns="$(date +%s%N)"
    status=0
    if [[ -n "$DRY_RUN" ]]; then
        echo "[DRY RUN] $cmd"
        elapsed_ms=0
        status=0
    else
        if $SSH_CMD "$cmd" 2>&1 | tee "$log_file"; then
            status=0
        else
            status=1
        fi
        end_ns="$(date +%s%N)"
        elapsed_ms=$(( (end_ns - start_ns) / 1000000 ))
    fi

    # Parse kernel elapsed from log if available
    kernel_ms=$(grep -oP 'kernel_elapsed_ms=\K[0-9]+' "$log_file" 2>/dev/null | head -1 || echo "$elapsed_ms")

    if [[ $status -eq 0 ]]; then
        result="PASS"
        (( passes++ )) || true
    else
        result="FAIL"
        (( failures++ )) || true
    fi

    echo "  ${result}: elapsed_ms=${kernel_ms}"
    echo "${warps}	${result}	${kernel_ms}	${name}.log" >> "$SUMMARY_TSV"
    json_rows+="{\"warps\":${warps},\"status\":\"${result}\",\"elapsed_ms\":${kernel_ms}},"
done

json_rows="${json_rows%,}]"

# ── Write JSON summary ────────────────────────────────────────────────────────
cat > "$SUMMARY_JSON" <<EOF
{
  "benchmark": "fpga_rodinia_bfs",
  "timestamp": "$(date -u +%Y-%m-%dT%H:%M:%SZ)",
  "host": "${KRIA_HOST}",
  "timeout_ms": ${TIMEOUT_MS},
  "runs": ${json_rows}
}
EOF

echo ""
echo "Results:"
column -t -s$'\t' "$SUMMARY_TSV"
echo ""
echo "Summary: ${passes} passed, ${failures} failed"
echo "TSV:  $SUMMARY_TSV"
echo "JSON: $SUMMARY_JSON"

[[ $failures -eq 0 ]] && exit 0 || exit 1
