// types.h – Shared data structures for the RISC-V GPGPU model
//
// Phase 4 adds: Opcode enum, program field in WarpContext
//

#ifndef RISCV_GPGPU_TYPES_H
#define RISCV_GPGPU_TYPES_H

#include <cstdint>
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

// ── Opcode (Phase 4) ──────────────────────────────────────────────────────────
// Encoding ranges:
//   0x00-0x1F  Scalar ALU
//   0x20-0x2F  Memory
//   0x30-0x3F  Branch
//   0x40-0x5F  Vector (RVV-style)
//   0x60-0x6F  Control flow
//   0x70-0x7F  SIMT
//   0xFF       Halt
enum class Opcode : uint32_t {
    // Scalar ALU
    ADD    = 0x00,
    SUB    = 0x01,
    AND    = 0x02,
    OR     = 0x03,
    XOR    = 0x04,
    SLT    = 0x05,
    ADDI   = 0x10,
    LUI    = 0x11,
    // Memory
    LW     = 0x20,
    SW     = 0x21,
    // Branch
    BEQ    = 0x30,
    BNE    = 0x31,
    // Vector (RVV-style: operates across active thread lanes)
    VADD   = 0x40,
    VSUB   = 0x41,
    VMUL   = 0x42,
    VFMADD = 0x43,   // rd[t] = rs1[t] * rs2[t] + rd[t]
    // Control
    JAL    = 0x60,
    JALR   = 0x61,
    // SIMT
    BARRIER = 0x70,
    // Halt – ends warp execution
    HALT   = 0xFF
};

// ── Instruction ───────────────────────────────────────────────────────────────
struct Instruction {
    uint32_t pc        = 0;
    uint32_t opcode    = 0;   // cast to/from Opcode
    uint8_t  rs1 = 0, rs2 = 0, rd = 0;
    int32_t  imm       = 0;
    bool     is_vector = false;
    bool     is_memory = false;
    bool     is_branch = false;
};

// Convenience builder used in tests and programs
inline Instruction makeInstr(Opcode op,
                              uint8_t rd  = 0,
                              uint8_t rs1 = 0,
                              uint8_t rs2 = 0,
                              int32_t imm = 0) {
    Instruction i;
    i.opcode    = static_cast<uint32_t>(op);
    i.rd = rd; i.rs1 = rs1; i.rs2 = rs2; i.imm = imm;
    uint32_t opc = i.opcode;
    i.is_vector = (opc >= 0x40 && opc < 0x60);
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
    // Scalar register file: regs[thread_index][reg_index]
    std::vector<std::vector<uint32_t>> regs;
    // Vector registers (RVV): vregs[vreg_index][lane]
    std::vector<std::vector<uint32_t>> vregs;
    // Instruction memory for this warp (Phase 4+)
    std::vector<Instruction> program;
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