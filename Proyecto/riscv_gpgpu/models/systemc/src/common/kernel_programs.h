// kernel_programs.h – Named kernel programs for the RISC-V GPGPU model
//
// Header-only library of ready-to-launch std::vector<Instruction> programs.
// Include this file wherever you need to build a kernel.
//
// ── Register convention (buildWarpContext) ────────────────────────────────────
// When launched via top.launchKernel() or sys.launchKernel(), each warp
// context is initialised by buildWarpContext() which sets:
//
//   r0[t]  = 0                          (zero register, always)
//   r1[t]  = global_tid                 (warp_id_offset + warp_id)*tpw + t)
//   r2[t]  = 0x10000 + global_tid * 4  (unique 4-byte-aligned memory address)
//   r3..r31 = 0
//
// Kernels tagged [TOP] rely on this convention and can be passed directly to
// top.launchKernel() or sys.launchKernel().
//
// Kernels tagged [DIRECT] are used with cu.executeWarp(ctx) where the caller
// sets up registers manually before the call.  The required setup is documented
// in each function's comment.
//
// ── Usage ─────────────────────────────────────────────────────────────────────
//
//   #include "common/kernel_programs.h"
//   using namespace riscv_gpgpu::kernels;
//
//   // [TOP] launch through the full system
//   top.launchKernel(4, 1, intSaxpy(2, 10));
//   sys.launchKernel(8, 1, divergentOddEven());
//
//   // [DIRECT] run directly on a standalone compute unit
//   WarpContext ctx = makeContext(0, 4, fpSaxpy());
//   for (uint32_t t = 0; t < 4; ++t) {
//       ctx.regs[t][3] = floatAsReg(2.0f);          // alpha
//       ctx.regs[t][4] = floatAsReg(float(t + 1));  // x[t]
//       ctx.regs[t][5] = floatAsReg(10.0f);         // y
//   }
//   cu.executeWarp(ctx);
//   // result in r6[t]: alpha * x[t] + y
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
// Computes: r6[t] = alpha * (global_tid + 1) + y  (all integer)
//
// Parameters:
//   alpha – integer multiplier   (default 2)
//   y     – integer addend       (default 10)
//
// Result register: r6
//
// Example:
//   top.launchKernel(2, 1, intSaxpy());
//   // thread 0: r6 = 2*(0+1)+10 = 12
//   // thread 1: r6 = 2*(1+1)+10 = 14
// ─────────────────────────────────────────────────────────────────────────────
inline std::vector<Instruction> intSaxpy(int32_t alpha = 2, int32_t y = 10) {
    return {
        makeInstr(Opcode::ADDI, 3, 0, 0, alpha),  // r3 = alpha
        makeInstr(Opcode::ADDI, 4, 1, 0, 1),      // r4 = tid + 1  (x value)
        makeInstr(Opcode::ADDI, 5, 0, 0, y),      // r5 = y
        makeInstr(Opcode::VMUL, 6, 4, 3,  0),     // r6 = x * alpha
        makeInstr(Opcode::VADD, 6, 6, 5,  0),     // r6 = x*alpha + y
        makeInstr(Opcode::HALT)
    };
}

// ─────────────────────────────────────────────────────────────────────────────
// [TOP] FP uniform SAXPY
//
// All threads compute the same result: r6 = alpha * x + y  (float)
// Useful for verifying the FP pipeline end-to-end through launchKernel().
// Not data-parallel (same input for all threads), but demonstrates FP ops.
//
// Parameters:
//   alpha – float multiplier  (default 2.0)
//   x     – float input       (default 3.0)
//   y     – float addend      (default 1.0)
//
// Result register: r6 (holds IEEE 754 bits of the result float)
//
// Example:
//   top.launchKernel(1, 1, fpUniformSaxpy(2.0f, 3.0f, 1.0f));
//   // all threads: r6 = floatAsReg(7.0f)
// ─────────────────────────────────────────────────────────────────────────────
inline std::vector<Instruction> fpUniformSaxpy(float alpha = 2.0f,
                                                float x     = 3.0f,
                                                float y     = 1.0f) {
    // Load float bit patterns as signed immediates via ADDI.
    // The bit-exact round-trip  uint32_t → int32_t → uint32_t  is guaranteed
    // in C++ (two's complement), so this works for all finite float values.
    return {
        makeInstr(Opcode::ADDI, 3, 0, 0, static_cast<int32_t>(floatAsReg(alpha))),
        makeInstr(Opcode::ADDI, 4, 0, 0, static_cast<int32_t>(floatAsReg(x))),
        makeInstr(Opcode::ADDI, 5, 0, 0, static_cast<int32_t>(floatAsReg(y))),
        makeInstr(Opcode::VFMUL, 6, 4, 3, 0),  // r6 = x * alpha  (float)
        makeInstr(Opcode::VFADD, 6, 6, 5, 0),  // r6 = x*alpha + y (float)
        makeInstr(Opcode::HALT)
    };
}

// ─────────────────────────────────────────────────────────────────────────────
// [TOP] Memory round-trip
//
// Each thread stores its global_tid to its unique memory address (r2),
// then loads it back twice to exercise the L1 cache (miss then hit).
//
// Result registers:
//   r3[t] = tid  (loaded from memory, L1 miss on first access)
//   r4[t] = tid  (loaded from memory, L1 hit  on second access)
//
// Use top.getL1CacheMisses() / getL1CacheHits() to verify cache behaviour.
//
// Example:
//   top.launchKernel(1, 1, memoryRoundTrip());
//   sc_start(100, SC_NS);
//   // expect: L1 misses += threads_per_warp (32)
//   //         L1 hits  += threads_per_warp (32)
// ─────────────────────────────────────────────────────────────────────────────
inline std::vector<Instruction> memoryRoundTrip() {
    return {
        makeInstr(Opcode::SW, 0, 2, 1, 0),  // mem[r2] = r1 (write-through)
        makeInstr(Opcode::LW, 3, 2, 0, 0),  // r3 = mem[r2]  — L1 miss, fills cache
        makeInstr(Opcode::LW, 4, 2, 0, 0),  // r4 = mem[r2]  — L1 hit
        makeInstr(Opcode::HALT)
    };
}

// ─────────────────────────────────────────────────────────────────────────────
// [TOP] Divergent odd/even kernel
//
// Threads with even global_tid fall through the VBRANCH body.
// Threads with odd  global_tid are masked until VJOIN.
//
// Result register:
//   r5[t] = 100  for even-tid threads
//   r5[t] = 0    for odd-tid  threads
//
// Divergence events: 1 per warp (always, since every 32-thread warp has
// both even and odd thread IDs).
//
// Example:
//   top.launchKernel(2, 1, divergentOddEven());
//   sc_start(100, SC_NS);
//   // expect: getDivergenceEvents() == 2  (one per warp)
// ─────────────────────────────────────────────────────────────────────────────
inline std::vector<Instruction> divergentOddEven() {
    return {
        makeInstr(Opcode::ADDI,    3, 0, 0, 1),    // r3 = 1
        makeInstr(Opcode::AND,     4, 1, 3, 0),    // r4 = tid & 1  (0=even, 1=odd)
        makeInstr(Opcode::VBRANCH, 0, 4, 0, 2),    // odd masked, even fall through
        makeInstr(Opcode::ADDI,    5, 0, 0, 100),  // even only: r5 = 100
        makeInstr(Opcode::VJOIN,   0, 0, 0, 0),    // all threads rejoin
        makeInstr(Opcode::HALT)
    };
}

// ─────────────────────────────────────────────────────────────────────────────
// [DIRECT] FP SAXPY
//
// Caller must set registers before executeWarp():
//   r3[t] = floatAsReg(alpha)   — multiplier (same for all threads)
//   r4[t] = floatAsReg(x[t])   — per-thread input value
//   r5[t] = floatAsReg(y)      — addend (same for all threads)
//
// Result register:
//   r6[t] = floatAsReg(alpha * x[t] + y)
//
// Example:
//   WarpContext ctx = makeContext(0, 4, kernels::fpSaxpy());
//   float xs[] = {1.0f, 2.0f, 3.0f, 4.0f};
//   for (uint32_t t = 0; t < 4; ++t) {
//       ctx.regs[t][3] = floatAsReg(2.0f);
//       ctx.regs[t][4] = floatAsReg(xs[t]);
//       ctx.regs[t][5] = floatAsReg(10.0f);
//   }
//   cu.executeWarp(ctx);
//   // regAsFloat(ctx.regs[0][6]) == 12.0f
// ─────────────────────────────────────────────────────────────────────────────
inline std::vector<Instruction> fpSaxpy() {
    return {
        makeInstr(Opcode::VFMUL, 6, 4, 3, 0),  // r6 = r4 * r3  (x * alpha)
        makeInstr(Opcode::VFADD, 6, 6, 5, 0),  // r6 = r6 + r5  (+ y)
        makeInstr(Opcode::HALT)
    };
}

// ─────────────────────────────────────────────────────────────────────────────
// [DIRECT] FP fused multiply-add  (VFFMADD)
//
// Caller must set registers before executeWarp():
//   r3[t] = floatAsReg(a[t])
//   r4[t] = floatAsReg(b[t])
//   r5[t] = floatAsReg(c[t])   — also the result register (accumulator)
//
// Result register:
//   r5[t] = floatAsReg(a[t] * b[t] + c[t])
//
// Example:
//   WarpContext ctx = makeContext(0, 4, kernels::fpFmadd());
//   for (uint32_t t = 0; t < 4; ++t) {
//       ctx.regs[t][3] = floatAsReg(1.5f + t);
//       ctx.regs[t][4] = floatAsReg(2.0f);
//       ctx.regs[t][5] = floatAsReg(1.0f);
//   }
//   cu.executeWarp(ctx);
//   // regAsFloat(ctx.regs[0][5]) == 4.0f  (1.5*2+1)
// ─────────────────────────────────────────────────────────────────────────────
inline std::vector<Instruction> fpFmadd() {
    return {
        makeInstr(Opcode::VFFMADD, 5, 3, 4, 0),  // r5 = r3*r4 + r5  (float)
        makeInstr(Opcode::HALT)
    };
}

// ─────────────────────────────────────────────────────────────────────────────
// [DIRECT] FP divergent SAXPY
//
// Even threads compute FP SAXPY; odd threads are masked.
// Caller must set registers:
//   r3[t] = floatAsReg(alpha)
//   r4[t] = floatAsReg(x[t])
//   r5[t] = floatAsReg(y)
//
// Result:
//   r6[t] = floatAsReg(alpha*x[t]+y)  for even threads
//   r6[t] = 0                          for odd  threads (masked during FP ops)
//
// Divergence events: 1 (if warp has both even and odd threads)
// ─────────────────────────────────────────────────────────────────────────────
inline std::vector<Instruction> fpDivergentSaxpy() {
    return {
        // Compute tid parity using thread's zero register trick:
        // r0[t]=t when set by test, r7 = t & 1
        makeInstr(Opcode::ADDI,    8, 0, 0, 1),    // r8 = 1
        makeInstr(Opcode::AND,     7, 0, 8, 0),    // r7 = r0 & 1  (0=even, 1=odd)
        makeInstr(Opcode::VBRANCH, 0, 7, 0, 3),    // odd threads masked
        makeInstr(Opcode::VFMUL,   6, 4, 3, 0),    // even only: r6 = x * alpha
        makeInstr(Opcode::VFADD,   6, 6, 5, 0),    // even only: r6 += y
        makeInstr(Opcode::VJOIN,   0, 0, 0, 0),    // reconverge
        makeInstr(Opcode::HALT)
    };
}

}  // namespace kernels
}  // namespace riscv_gpgpu

#endif  // RISCV_GPGPU_KERNEL_PROGRAMS_H