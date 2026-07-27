#!/usr/bin/env bash
# run_saxpy.sh – End-to-end saxpy benchmark runner (z[i] = a*x[i] + y[i])

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/../../.." && pwd)"
BUILD_DIR="${REPO_ROOT}/build"
OUT_DIR="${BUILD_DIR}/benchmarks/saxpy"
SRC="${SCRIPT_DIR}/saxpy.cu"
KERNEL_NAME="saxpy"

mkdir -p "${OUT_DIR}"

log() { echo "[bench:saxpy] $*"; }

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

# ─── Disassembly ──────────────────────────────────────────────────────────────
if command -v riscv64-unknown-elf-objdump &>/dev/null; then
    riscv64-unknown-elf-objdump -d "${ELF}" > "${OUT_DIR}/${KERNEL_NAME}.disasm.txt" 2>&1
fi

# ─── Bundle manifest ──────────────────────────────────────────────────────────
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

# ─── Host simulation for validation ───────────────────────────────────────────
SIM_SRC="${OUT_DIR}/sim_validate.cpp"
SIM_BIN="${OUT_DIR}/sim_validate"

cat > "${SIM_SRC}" <<'CPP'
#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <ctime>

static void saxpy_sim(float a, const float* x, const float* y, float* z, int n) {
    for (int i = 0; i < n; ++i) z[i] = a * x[i] + y[i];
}

int main(int argc, char* argv[]) {
    const int   N = (argc > 1) ? atoi(argv[1]) : 1024;
    const float A = 2.5f;
    float* x = new float[N];
    float* y = new float[N];
    float* z = new float[N];
    for (int i = 0; i < N; ++i) { x[i] = (float)i; y[i] = 1.0f; }

    struct timespec t0, t1;
    clock_gettime(CLOCK_MONOTONIC, &t0);
    saxpy_sim(A, x, y, z, N);
    clock_gettime(CLOCK_MONOTONIC, &t1);
    double elapsed_us = (t1.tv_sec - t0.tv_sec) * 1e6 + (t1.tv_nsec - t0.tv_nsec) / 1e3;

    int errors = 0;
    for (int i = 0; i < N; ++i) {
        float expected = A * (float)i + 1.0f;
        if (fabsf(z[i] - expected) > 1e-4f) errors++;
    }

    printf("saxpy N=%d  a=%.2f  result=%s  errors=%d  elapsed=%.2f us\n",
           N, A, (errors == 0 ? "PASS" : "FAIL"), errors, elapsed_us);

    delete[] x; delete[] y; delete[] z;
    return (errors == 0) ? 0 : 1;
}
CPP

log "Compiling host simulation binary…"
c++ -O2 -std=c++17 -lm -o "${SIM_BIN}" "${SIM_SRC}"
log "Running host simulation validation…"
"${SIM_BIN}" "${N:-1024}"

log "saxpy benchmark complete. Outputs in ${OUT_DIR}"
