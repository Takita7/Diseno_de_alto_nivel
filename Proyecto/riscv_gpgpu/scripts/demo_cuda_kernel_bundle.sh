#!/usr/bin/env bash

set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
DEMO_DIR="$ROOT_DIR/build/cuda_demo"
KERNEL_NAME="vector_add"
SOURCE_FILE="$DEMO_DIR/${KERNEL_NAME}.cu"
BINARY_FILE="$DEMO_DIR/${KERNEL_NAME}.riscv.elf"
MANIFEST_FILE="$DEMO_DIR/${KERNEL_NAME}_manifest.json"
DISASM_FILE="$DEMO_DIR/${KERNEL_NAME}.disasm.txt"
ELF_HEADER_FILE="$DEMO_DIR/${KERNEL_NAME}.elf_header.txt"
SECTIONS_FILE="$DEMO_DIR/${KERNEL_NAME}.sections.txt"
SYMBOLS_FILE="$DEMO_DIR/${KERNEL_NAME}.symbols.txt"
RELOCS_FILE="$DEMO_DIR/${KERNEL_NAME}.relocs.txt"
LAUNCH_PACKET_FILE="$DEMO_DIR/${KERNEL_NAME}.launch.json"
EXPECTED_OUTPUT_FILE="$DEMO_DIR/${KERNEL_NAME}.expected_output.json"
SOURCE_SHA_FILE="$DEMO_DIR/${KERNEL_NAME}.sha256.txt"

find_disassembler() {
  if command -v riscv64-unknown-elf-objdump >/dev/null 2>&1; then
    echo "riscv64-unknown-elf-objdump"
    return 0
  fi
  if command -v llvm-objdump >/dev/null 2>&1; then
    echo "llvm-objdump"
    return 0
  fi
  if command -v riscv32-unknown-elf-objdump >/dev/null 2>&1; then
    echo "riscv32-unknown-elf-objdump"
    return 0
  fi
  if command -v objdump >/dev/null 2>&1 && objdump -i 2>/dev/null | grep -qi riscv; then
    echo "objdump"
    return 0
  fi
  return 1
}

mkdir -p "$DEMO_DIR"
cd "$DEMO_DIR"

DISASSEMBLER="$(find_disassembler || true)"
if [[ -z "$DISASSEMBLER" ]]; then
  echo "[demo] error: no RISC-V capable disassembler found in PATH." >&2
  echo "[demo] install binutils-riscv64-unknown-elf or llvm-objdump before running this demo." >&2
  exit 1
fi

echo "[demo] Generating CUDA-style kernel source..."
cat > "$SOURCE_FILE" <<'EOF'
// CUDA-style vector add demo for compiler/runtime/driver inspection.
// This avoids CUDA runtime intrinsics so it can compile with the
// current frontend path while still exercising multiple arguments
// and a loop in the generated instruction stream.
__global__ void vector_add(const int *a, const int *b, int *c, int n) {
  for (int i = 0; i < n; ++i) {
    c[i] = a[i] + b[i];
  }
}
EOF

echo "[demo] Compiling CUDA-style kernel to RISC-V ELF..."
if ! command -v ld.lld >/dev/null 2>&1; then
  echo "[demo] error: ld.lld is required for RISC-V linking but was not found in PATH." >&2
  exit 1
fi

clang -target riscv32-unknown-elf -march=rv32gc -mabi=ilp32 \
    -x c++ -std=c++17 \
    -D__global__= -D__device__= -D__host__= \
    -D__shared__= -D__constant__= -D__restrict__= \
  -O2 -c "$SOURCE_FILE" -o "$DEMO_DIR/${KERNEL_NAME}.o"

clang -target riscv32-unknown-elf -march=rv32gc -mabi=ilp32 \
  -fuse-ld=lld \
  -nostdlib -shared -o "$BINARY_FILE" "$DEMO_DIR/${KERNEL_NAME}.o"

echo "[demo] Kernel binary created at: $BINARY_FILE"
echo "[demo] Binary size: $(stat -c%s "$BINARY_FILE") bytes"

echo "[demo] Writing bundle manifest..."
cat > "$MANIFEST_FILE" <<EOF
{
  "kernel_name": "$KERNEL_NAME",
  "binary_path": "$BINARY_FILE",
  "binary_size": $(stat -c%s "$BINARY_FILE"),
  "entry_point": "$KERNEL_NAME",
  "workgroup": {
    "x": 1,
    "y": 1,
    "z": 1
  },
  "metadata": {
    "shared_mem_bytes": 0,
    "registers_per_thread": 32,
    "argument_count": 4
  }
}
EOF

echo "[demo] Manifest created at: $MANIFEST_FILE"
echo "[demo] Bundle metadata:"
cat "$MANIFEST_FILE"

sha256sum "$BINARY_FILE" > "$SOURCE_SHA_FILE"

cat > "$LAUNCH_PACKET_FILE" <<EOF
{
  "kernel_name": "$KERNEL_NAME",
  "entry_point": "$KERNEL_NAME",
  "binary_path": "$BINARY_FILE",
  "binary_sha256": "$(awk '{print $1}' "$SOURCE_SHA_FILE")",
  "workgroup": {
    "x": 1,
    "y": 1,
    "z": 1
  },
  "args": [
    {
      "index": 0,
      "name": "a",
      "kind": "device_pointer",
      "width_bits": 64,
      "example_value": "0x0000000010000000"
    },
    {
      "index": 1,
      "name": "b",
      "kind": "device_pointer",
      "width_bits": 64,
      "example_value": "0x0000000010001000"
    },
    {
      "index": 2,
      "name": "c",
      "kind": "device_pointer",
      "width_bits": 64,
      "example_value": "0x0000000010002000"
    },
    {
      "index": 3,
      "name": "n",
      "kind": "scalar",
      "width_bits": 32,
      "example_value": 4
    }
  ],
  "shared_mem_bytes": 0
}
EOF

cat > "$EXPECTED_OUTPUT_FILE" <<EOF
{
  "kernel_name": "$KERNEL_NAME",
  "input": {
    "a": [1, 2, 3, 4],
    "b": [10, 20, 30, 40],
    "c_initial": [0, 0, 0, 0],
    "n": 4
  },
  "expected_output": {
    "c_final": [11, 22, 33, 44]
  },
  "validation_rule": "for all i in [0, n), c[i] must equal a[i] + b[i]"
}
EOF

readelf -h "$BINARY_FILE" > "$ELF_HEADER_FILE"
readelf -S "$BINARY_FILE" > "$SECTIONS_FILE"
readelf -s "$BINARY_FILE" > "$SYMBOLS_FILE"
readelf -r "$BINARY_FILE" > "$RELOCS_FILE"

echo "[demo] Hardware interface summary:"
echo "  Kernel Name: $KERNEL_NAME"
echo "  Binary Path: $BINARY_FILE"
echo "  Binary Size: $(stat -c%s "$BINARY_FILE")"
echo "  Binary SHA256: $(awk '{print $1}' "$SOURCE_SHA_FILE")"
echo "  Entry Point: $KERNEL_NAME"
echo "  Workgroup: 1 x 1 x 1"
echo "  Shared Memory: 0 bytes"
echo "  Argument Count: 4"

echo "[demo] Additional analysis artifacts:"
echo "  ELF Header: $ELF_HEADER_FILE"
echo "  Sections: $SECTIONS_FILE"
echo "  Symbols: $SYMBOLS_FILE"
echo "  Relocations: $RELOCS_FILE"
echo "  Launch Packet: $LAUNCH_PACKET_FILE"
echo "  Expected Output: $EXPECTED_OUTPUT_FILE"
echo "  SHA256: $SOURCE_SHA_FILE"

echo "[demo] Instruction view:"
case "$DISASSEMBLER" in
  llvm-objdump)
    "$DISASSEMBLER" -d --no-show-raw-insn "$BINARY_FILE" | tee "$DISASM_FILE"
    ;;
  riscv64-unknown-elf-objdump)
    "$DISASSEMBLER" -d "$BINARY_FILE" | tee "$DISASM_FILE"
    ;;
  riscv32-unknown-elf-objdump)
    "$DISASSEMBLER" -d "$BINARY_FILE" | tee "$DISASM_FILE"
    ;;
  objdump)
    "$DISASSEMBLER" -d "$BINARY_FILE" | tee "$DISASM_FILE"
    ;;
esac
echo "[demo] Disassembly saved to: $DISASM_FILE"

echo "[demo] Symbol summary:"
grep -E 'vector_add|FUNC|OBJECT' "$SYMBOLS_FILE" || true

echo "[demo] Relocation summary:"
if grep -q 'There are no relocations in this file' "$RELOCS_FILE"; then
  echo "  no relocations"
else
  sed -n '1,40p' "$RELOCS_FILE"
fi

echo "[demo] Demo complete. Use the manifest and binary with runtime/driver upload interfaces."