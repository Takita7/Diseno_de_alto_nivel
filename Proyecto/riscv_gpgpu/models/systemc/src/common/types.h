// types.h – Shared data structures for the RISC-V GPGPU model
//

#ifndef RISCV_GPGPU_TYPES_H
#define RISCV_GPGPU_TYPES_H

#include <cstdint>
#include <cstring>
#include <vector>

namespace riscv_gpgpu {

// ── Primitive aliases ─────────────────────────────────────────────────────────
using WarpID           = uint32_t;
using GridID           = uint32_t;
using BlockID          = uint32_t;
using ThreadID         = uint32_t;
using ComputeUnitID    = uint32_t;
using Address          = uint64_t;
using CycleCount       = uint64_t;
using InstructionCount = uint64_t;

// ── Cache lookup result ───────────────────────────────────────────────────────
enum class CacheStatus { L1_HIT, L2_HIT, MISS };

// ── Warp lifecycle ────────────────────────────────────────────────────────────
enum class WarpState { IDLE, READY, RUNNING, STALLED, WAITING_MEM, COMPLETE };

// ── Opcode ────────────────────────────────────────────────────────────────────
// Encoding ranges:
//   0x00–0x1F  Scalar integer ALU
//   0x06,0x08  Scalar FP (FADD, FMUL)          
//   0x20–0x2F  Memory
//   0x30–0x3F  Branch / SIMT control
//   0x40–0x47  Vector integer
//   0x48–0x4F  Vector FP                        
//   0x60–0x6F  Control flow
//   0x70–0x7F  SIMT barriers
//   0xFF       Halt
enum class Opcode : uint32_t {
    // Scalar integer ALU
    ADD    = 0x00,
    SUB    = 0x01,
    AND    = 0x02,
    OR     = 0x03,
    XOR    = 0x04,
    SLT    = 0x05,
    // Scalar FP (registers hold IEEE 754 single-precision bits)
    FADD   = 0x06,   // rd[t] = rs1[t] + rs2[t]  (float)
    FMUL   = 0x08,   // rd[t] = rs1[t] * rs2[t]  (float)
    // Scalar integer ALU (continued)
    ADDI   = 0x10,
    LUI    = 0x11,
    // Memory
    LW     = 0x20,
    SW     = 0x21,
    // Branch / SIMT control
    BEQ    = 0x30,
    BNE    = 0x31,
    VBRANCH = 0x32,
    VJOIN   = 0x33,
    // Vector integer
    VADD   = 0x40,
    VSUB   = 0x41,
    VMUL   = 0x42,
    VFMADD = 0x43,   // rd[t] = rs1[t]*rs2[t] + rd[t]  (integer)
    // Vector FP (registers hold IEEE 754 single-precision bits)
    VFADD   = 0x48,  // rd[t] = rs1[t] + rs2[t]         (float)
    VFSUB   = 0x49,  // rd[t] = rs1[t] - rs2[t]         (float)
    VFMUL   = 0x4A,  // rd[t] = rs1[t] * rs2[t]         (float)
    VFFMADD = 0x4B,  // rd[t] = rs1[t]*rs2[t] + rd[t]   (float)
    // Control
    JAL    = 0x60,
    JALR   = 0x61,
    // SIMT barriers
    BARRIER = 0x70,
    // Halt
    HALT   = 0xFF
};

// ── Floating-point register helpers ──────────────────────────────────────────
// Registers store uint32_t; these helpers reinterpret the bits as IEEE 754
// single-precision float without any value conversion.
// Used in ComputeUnit FP execution AND in tests to set/check FP register values.
inline float    regAsFloat(uint32_t bits) {
    float f; std::memcpy(&f, &bits, sizeof(f)); return f;
}
inline uint32_t floatAsReg(float f) {
    uint32_t b; std::memcpy(&b, &f, sizeof(b)); return b;
}

// ── Instruction ───────────────────────────────────────────────────────────────
struct Instruction {
    uint32_t pc        = 0;
    uint32_t opcode    = 0;
    uint8_t  rs1 = 0, rs2 = 0, rd = 0;
    int32_t  imm       = 0;
    bool     is_vector = false;
    bool     is_memory = false;
    bool     is_branch = false;
};

// Convenience builder
inline Instruction makeInstr(Opcode op,
                              uint8_t rd  = 0,
                              uint8_t rs1 = 0,
                              uint8_t rs2 = 0,
                              int32_t imm = 0) {
    Instruction i;
    i.opcode    = static_cast<uint32_t>(op);
    i.rd = rd; i.rs1 = rs1; i.rs2 = rs2; i.imm = imm;
    uint32_t opc = i.opcode;
    i.is_vector = (opc >= 0x40 && opc < 0x60);   // covers both int and FP vector
    i.is_memory = (opc >= 0x20 && opc < 0x30);
    i.is_branch = (opc >= 0x30 && opc < 0x40);
    return i;
}

// ── Per-warp execution context ────────────────────────────────────────────────
struct WarpContext {
    WarpID    warp_id    = 0;
    GridID    kernel_id  = 0;
    uint32_t  block_id_x = 0, block_id_y = 0;
    uint32_t  pc          = 0;
    uint32_t  active_mask = 0;
    WarpState state       = WarpState::IDLE;
    std::vector<std::vector<uint32_t>> regs;
    std::vector<std::vector<uint32_t>> vregs;
    std::vector<Instruction>           program;
};

// ── Memory transaction ────────────────────────────────────────────────────────
struct MemTransaction {
    Address              address    = 0;
    uint32_t             size_bytes = 0;
    bool                 is_write   = false;
    std::vector<uint8_t> data;
    WarpID               warp_id     = 0;
    uint32_t             thread_mask = 0;
};

}  // namespace riscv_gpgpu
#endif  // RISCV_GPGPU_TYPES_H