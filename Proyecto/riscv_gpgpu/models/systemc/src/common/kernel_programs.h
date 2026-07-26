// kernel_programs.h – Named kernel programs for the RISC-V GPGPU model
//
// Header-only library of ready-to-launch std::vector<Instruction> programs.
//
// ── Register convention (buildWarpContext) ────────────────────────────────────
//   r0[t]  = 0                         (zero register, always)
//   r1[t]  = global_tid                (warp_id_offset + warp_id)*tpw + t
//   r2[t]  = 0x10000 + global_tid * 4 (unique 4-byte-aligned memory address)
//   r3..r31 = 0
//
// ── Kernel tags ───────────────────────────────────────────────────────────────
//   [TOP]    — use via top.launchKernel() or sys.launchKernel()
//   [DIRECT] — use via cu.executeWarp(ctx); caller sets registers manually
//
// ── BARRIER semantics ─────────────────────────────────────────────────────────
//   BARRIER imm=barrier_id: suspends the warp until all warps in the kernel
//   have reached the same barrier_id. Multiple barrier IDs are supported.
//
// ── VBRANCH / VJOIN semantics (Option A) ─────────────────────────────────────
//   VBRANCH rs1, imm: threads where rs1[t] == 0 fall through (stay active);
//                     threads where rs1[t] != 0 are masked until VJOIN.
//

#ifndef RISCV_GPGPU_KERNEL_PROGRAMS_H
#define RISCV_GPGPU_KERNEL_PROGRAMS_H

#include <vector>
#include <cstdint>
#include "types.h"

namespace riscv_gpgpu {
namespace kernels {

// ─────────────────────────────────────────────────────────────────────────────
// [TOP] Integer SAXPY
//
// r6[t] = alpha * (global_tid + 1) + y  (integer)
// Result register: r6
// ─────────────────────────────────────────────────────────────────────────────
inline std::vector<Instruction> intSaxpy(int32_t alpha = 2, int32_t y = 10) {
    return {
        makeInstr(Opcode::ADDI, 3, 0, 0, alpha),
        makeInstr(Opcode::ADDI, 4, 1, 0, 1),
        makeInstr(Opcode::ADDI, 5, 0, 0, y),
        makeInstr(Opcode::VMUL, 6, 4, 3, 0),
        makeInstr(Opcode::VADD, 6, 6, 5, 0),
        makeInstr(Opcode::HALT)
    };
}

// ─────────────────────────────────────────────────────────────────────────────
// [TOP] FP uniform SAXPY
//
// All threads compute: r6 = alpha * x + y  (float, same value per thread)
// Result register: r6 (IEEE 754 bits)
// ─────────────────────────────────────────────────────────────────────────────
inline std::vector<Instruction> fpUniformSaxpy(float alpha = 2.0f,
                                                float x     = 3.0f,
                                                float y     = 1.0f) {
    return {
        makeInstr(Opcode::ADDI,  3, 0, 0, static_cast<int32_t>(floatAsReg(alpha))),
        makeInstr(Opcode::ADDI,  4, 0, 0, static_cast<int32_t>(floatAsReg(x))),
        makeInstr(Opcode::ADDI,  5, 0, 0, static_cast<int32_t>(floatAsReg(y))),
        makeInstr(Opcode::VFMUL, 6, 4, 3, 0),
        makeInstr(Opcode::VFADD, 6, 6, 5, 0),
        makeInstr(Opcode::HALT)
    };
}

// ─────────────────────────────────────────────────────────────────────────────
// [TOP] Memory round-trip
//
// Each thread stores global_tid to r2 (unique address), loads it back twice.
// First LW → L1 miss; second LW → L1 hit.
// Result registers: r3 (miss), r4 (hit)
// ─────────────────────────────────────────────────────────────────────────────
inline std::vector<Instruction> memoryRoundTrip() {
    return {
        makeInstr(Opcode::SW, 0, 2, 1, 0),
        makeInstr(Opcode::LW, 3, 2, 0, 0),
        makeInstr(Opcode::LW, 4, 2, 0, 0),
        makeInstr(Opcode::HALT)
    };
}

// ─────────────────────────────────────────────────────────────────────────────
// [TOP] Divergent odd/even
//
// Even-tid threads fall through VBRANCH, odd-tid threads are masked.
// r5[t] = 100 for even threads, 0 for odd threads.
// Divergence events: 1 per warp.
// ─────────────────────────────────────────────────────────────────────────────
inline std::vector<Instruction> divergentOddEven() {
    return {
        makeInstr(Opcode::ADDI,    3, 0, 0, 1),
        makeInstr(Opcode::AND,     4, 1, 3, 0),
        makeInstr(Opcode::VBRANCH, 0, 4, 0, 2),
        makeInstr(Opcode::ADDI,    5, 0, 0, 100),
        makeInstr(Opcode::VJOIN,   0, 0, 0, 0),
        makeInstr(Opcode::HALT)
    };
}

// ─────────────────────────────────────────────────────────────────────────────
// [TOP] Barrier round-trip
//
// Each warp stores global_tid to memory, synchronises at barrier_id, then
// loads the value back. Verifies that all warps are suspended at the barrier
// and correctly resume after it fires.
//
// Program: SW r1,0(r2) → BARRIER(barrier_id) → LW r3,0(r2) → HALT
//
// Result: r3[t] == global_tid  (value written before barrier, read after)
//
// Expected metrics per warp:
//   L1 misses += threads_per_warp   (first LW, write-through no-write-alloc)
//   L1 hits   += 0
//   Divergence = 0
// ─────────────────────────────────────────────────────────────────────────────
inline std::vector<Instruction> barrierRoundTrip(uint32_t barrier_id = 0) {
    return {
        makeInstr(Opcode::SW,      0, 2, 1, 0),
        makeInstr(Opcode::BARRIER, 0, 0, 0, static_cast<int32_t>(barrier_id)),
        makeInstr(Opcode::LW,      3, 2, 0, 0),
        makeInstr(Opcode::HALT)
    };
}

// ─────────────────────────────────────────────────────────────────────────────
// [DIRECT] FP SAXPY
//
// Caller sets: r3[t]=floatAsReg(alpha), r4[t]=floatAsReg(x[t]),
//              r5[t]=floatAsReg(y)
// Result: r6[t] = floatAsReg(alpha * x[t] + y)
// ─────────────────────────────────────────────────────────────────────────────
inline std::vector<Instruction> fpSaxpy() {
    return {
        makeInstr(Opcode::VFMUL, 6, 4, 3, 0),
        makeInstr(Opcode::VFADD, 6, 6, 5, 0),
        makeInstr(Opcode::HALT)
    };
}

// ─────────────────────────────────────────────────────────────────────────────
// [DIRECT] FP fused multiply-add (VFFMADD)
//
// Caller sets: r3[t]=floatAsReg(a[t]), r4[t]=floatAsReg(b[t]),
//              r5[t]=floatAsReg(c[t])
// Result: r5[t] = floatAsReg(a[t] * b[t] + c[t])
// ─────────────────────────────────────────────────────────────────────────────
inline std::vector<Instruction> fpFmadd() {
    return {
        makeInstr(Opcode::VFFMADD, 5, 3, 4, 0),
        makeInstr(Opcode::HALT)
    };
}

// ─────────────────────────────────────────────────────────────────────────────
// [DIRECT] FP divergent SAXPY
//
// Even threads compute FP SAXPY; odd threads are masked.
// Caller sets: r0[t]=thread_index, r3[t]=alpha, r4[t]=x[t], r5[t]=y
// Result: r6[t] = floatAsReg(alpha*x[t]+y) for even threads, 0 for odd
// ─────────────────────────────────────────────────────────────────────────────
inline std::vector<Instruction> fpDivergentSaxpy() {
    return {
        makeInstr(Opcode::ADDI,    8, 0, 0, 1),
        makeInstr(Opcode::AND,     7, 0, 8, 0),
        makeInstr(Opcode::VBRANCH, 0, 7, 0, 3),
        makeInstr(Opcode::VFMUL,   6, 4, 3, 0),
        makeInstr(Opcode::VFADD,   6, 6, 5, 0),
        makeInstr(Opcode::VJOIN,   0, 0, 0, 0),
        makeInstr(Opcode::HALT)
    };
}

}  // namespace kernels
}  // namespace riscv_gpgpu

#endif  // RISCV_GPGPU_KERNEL_PROGRAMS_H