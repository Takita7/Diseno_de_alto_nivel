// types.h – Shared data structures for the RISC-V GPGPU model
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

// ── Instruction ───────────────────────────────────────────────────────────────
struct Instruction {
    uint32_t pc     = 0;
    uint32_t opcode = 0;
    uint8_t  rs1 = 0, rs2 = 0, rd = 0;
    int32_t  imm       = 0;
    bool     is_vector = false;
    bool     is_memory = false;
    bool     is_branch = false;
};

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