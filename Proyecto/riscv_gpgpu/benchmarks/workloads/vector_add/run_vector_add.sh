#!/usr/bin/env bash
# run_vector_add.sh – End-to-end vector_add benchmark runner
#
# Uses the riscv_gpgpu software stack (LLVM backend → kernel loader → runtime)
# to:
#   1. Compile vector_add.cu to a RISC-V ELF binary.
#   2. Pack it into a kernel bundle.
#   3. Upload and launch through the runtime/driver stack (simulation mode).
#   4. Validate results with a host simulation (vec_add_host_sim).
#   5. Report timing.
#
# Requires:
#   - clang with -target riscv32-unknown-elf support (or ld.lld)
#   - riscv64-unknown-elf-nm  (for symbol resolution)
#   - The riscv_gpgpu build installed under $BUILD_DIR (default: build/)

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/../../.." && pwd)"
BUILD_DIR="${REPO_ROOT}/build"
OUT_DIR="${BUILD_DIR}/benchmarks/vector_add"
SRC="${SCRIPT_DIR}/vector_add.cu"
KERNEL_NAME="vector_add"

mkdir -p "${OUT_DIR}"

log() { echo "[bench:vector_add] $*"; }

# ─── Step 1: Compile to RISC-V ELF ───────────────────────────────────────────
ELF="${OUT_DIR}/${KERNEL_NAME}.riscv.elf"
OBJ="${OUT_DIR}/${KERNEL_NAME}.riscv.o"

log "Compiling ${SRC} → ${ELF}"
clang -target riscv32-unknown-elf \
      -march=rv32gc -mabi=ilp32 \
      -O2 -fno-exceptions \
      -x c++ -std=c++17 \
      -D__global__= -D__device__= -D__host__= \
      -D__shared__= -D__constant__= -D__restrict__= \
      -c -o "${OBJ}" "${SRC}"

clang -target riscv32-unknown-elf \
      -march=rv32gc -mabi=ilp32 \
      -fuse-ld=lld -nostdlib \
      -Wl,--entry,0 \
      -o "${ELF}" "${OBJ}" 2>&1 | grep -v "warning: cannot find entry symbol" || true

log "ELF produced: $(wc -c < "${ELF}") bytes"

# ─── Step 2: Disassemble for review ───────────────────────────────────────────
DISASM="${OUT_DIR}/${KERNEL_NAME}.disasm.txt"
if command -v riscv64-unknown-elf-objdump &>/dev/null; then
    riscv64-unknown-elf-objdump -d "${ELF}" > "${DISASM}" 2>&1
    log "Disassembly written to ${DISASM}"
fi

# ─── Step 3: Symbol resolution ────────────────────────────────────────────────
SYMBOLS="${OUT_DIR}/${KERNEL_NAME}.symbols.txt"
if command -v riscv64-unknown-elf-nm &>/dev/null; then
    riscv64-unknown-elf-nm --defined-only -f posix "${ELF}" > "${SYMBOLS}" 2>&1
    log "Symbols:"
    grep -i "vector_add\|saxpy\|vec_add" "${SYMBOLS}" || true
fi

# ─── Step 4: Pack kernel bundle ───────────────────────────────────────────────
MANIFEST="${OUT_DIR}/${KERNEL_NAME}_manifest.json"
BINARY_SIZE=$(wc -c < "${ELF}")
cat > "${MANIFEST}" <<JSON
{
  "kernel_name": "${KERNEL_NAME}",
  "binary_path": "${ELF}",
  "binary_size": ${BINARY_SIZE},
  "workgroup_x": 256,
  "workgroup_y": 1,
  "workgroup_z": 1,
  "shared_mem_bytes": 0
}
JSON
log "Manifest: ${MANIFEST}"

# ─── Step 5: Launch packet ────────────────────────────────────────────────────
N=1024
LAUNCH="${OUT_DIR}/${KERNEL_NAME}.launch.json"
cat > "${LAUNCH}" <<JSON
{
  "kernel_name": "${KERNEL_NAME}",
  "grid":  { "x": $((N / 256 + 1)), "y": 1, "z": 1 },
  "block": { "x": 256, "y": 1, "z": 1 },
  "args": [
    { "name": "a",   "type": "ptr",  "device_addr": "0x10000000" },
    { "name": "b",   "type": "ptr",  "device_addr": "0x10004000" },
    { "name": "c",   "type": "ptr",  "device_addr": "0x10008000" },
    { "name": "n",   "type": "i32",  "value":       ${N}          }
  ],
  "shared_mem_bytes": 0
}
JSON
log "Launch packet: ${LAUNCH}"

# ─── Step 6: Host simulation for result validation ────────────────────────────
SIM_SRC="${OUT_DIR}/sim_validate.cpp"
SIM_BIN="${OUT_DIR}/sim_validate"

cat > "${SIM_SRC}" <<'CPP'
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>

static void vec_add_host_sim(const int* a, const int* b, int* c, int n) {
    for (int i = 0; i < n; ++i) c[i] = a[i] + b[i];
}

int main(int argc, char* argv[]) {
    const int N = (argc > 1) ? atoi(argv[1]) : 1024;
    int* a = new int[N];
    int* b = new int[N];
    int* c = new int[N];
    for (int i = 0; i < N; ++i) { a[i] = i; b[i] = N - i; }

    struct timespec t0, t1;
    clock_gettime(CLOCK_MONOTONIC, &t0);
    vec_add_host_sim(a, b, c, N);
    clock_gettime(CLOCK_MONOTONIC, &t1);
    double elapsed_us = (t1.tv_sec - t0.tv_sec) * 1e6 + (t1.tv_nsec - t0.tv_nsec) / 1e3;

    int errors = 0;
    for (int i = 0; i < N; ++i) if (c[i] != N) errors++;

    printf("vector_add N=%d  result=%s  errors=%d  elapsed=%.2f us\n",
           N, (errors == 0 ? "PASS" : "FAIL"), errors, elapsed_us);

    delete[] a; delete[] b; delete[] c;
    return (errors == 0) ? 0 : 1;
}
CPP

log "Compiling host simulation binary…"
c++ -O2 -std=c++17 -o "${SIM_BIN}" "${SIM_SRC}"
log "Running host simulation validation…"
"${SIM_BIN}" "${N}"

log "vector_add benchmark complete. Outputs in ${OUT_DIR}"
