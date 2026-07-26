// kernel_programs.h – Named kernel programs for the RISC-V GPGPU model
//
// ── Register convention (buildWarpContext) ────────────────────────────────────
//   r0[t]  = 0                         (zero register, always)
//   r1[t]  = global_tid
//   r2[t]  = 0x10000 + global_tid * 4  (unique 4-byte-aligned memory address)
//   r3..r31 = 0
//
// ── Kernel tags ───────────────────────────────────────────────────────────────
//   [TOP]    use via top.launchKernel() or sys.launchKernel()
//   [DIRECT] use via cu.executeWarp(); caller sets registers manually
//
// ── VBRANCH / VJOIN semantics (Option A) ─────────────────────────────────────
//   VBRANCH rs1: rs1[t]==0 → thread falls through (active)
//                rs1[t]!=0 → thread is masked until VJOIN
//
// ── BARRIER semantics ─────────────────────────────────────────────────────────
//   BARRIER imm=barrier_id: all warps suspend until every warp in the kernel
//   has reached the same barrier_id, then all resume together.
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
// Instructions per warp: 6
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
// All threads: r6 = alpha * x + y  (float, same value per thread)
// Result register: r6 (IEEE 754 bits)
// Instructions per warp: 6
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
// Each thread: SW global_tid to r2, LW back twice (L1 miss then hit).
// Result registers: r3 (L1 miss load), r4 (L1 hit load)
// Instructions per warp: 4
// Expected metrics per warp: L1_misses += tpw, L1_hits += tpw
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
// Divergence events: 1 per warp (every 32-thread warp has both even and odd).
// Instructions per warp: 6
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
// Each thread stores global_tid to r2, barrier syncs all warps, loads back.
// Result register: r3
// Instructions per warp: 4
// Expected metrics per warp: L1_misses += tpw  (SW no-write-alloc → LW cold miss)
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
// [TOP] Parallel reduction – pairwise sum across 2 warps
//
// Requires: exactly 2 warps, threads_per_warp = 32.
// Launch with: top.launchKernel(2, 1, parallelReduction())
//
// Algorithm:
//   Phase 1 (pre-barrier): each thread stores (global_tid + 1) to mem[r2].
//   Barrier:               all warps synchronise at BARRIER(0).
//   Phase 2 (post-barrier): each warp loads its partner warp's values using
//     a signed offset of ±128 bytes (= ±32 threads × 4 bytes/thread).
//     VBRANCH/VJOIN select the correct offset per warp without divergence
//     (each warp is homogeneous so the all-jump/all-fall-through paths apply).
//   Result: r6[t] = own_value + partner_value, stored at mem[r2+8].
//
// Expected values:
//   Warp 0 thread t : r6 = (t+1) + (32+t+1) = 2t+34   e.g. thread 0 → 34
//   Warp 1 thread t : r6 = (32+t+1) + (t+1) = 2t+34   same formula
//
// Expected metrics for 2 warps:
//   Instructions    : 32  (16 per warp × 2 warps)
//   L1 misses       : 64  (32 per warp – one cross-warp LW per thread)
//   L1 hits         : 0   (write-through SW doesn't fill L1)
//   Divergence      : 0   (each warp is homogeneous; no intra-warp disagreement)
//
// Result verification (via top.readWord):
//   top.readWord(0x10008) == 34   (warp 0, thread 0, stored at r2+8 = 0x10008)
//   top.readWord(0x1000C) == 36   (warp 0, thread 1, stored at 0x1000C)
// ─────────────────────────────────────────────────────────────────────────────
inline std::vector<Instruction> parallelReduction() {
    // Register setup by buildWarpContext:
    //   r0=0, r1=global_tid, r2=unique_addr, r3=local_warp_id (0 or 1)
    //
    // Phase 1 (pre-barrier): store (global_tid+1) to mem[r2].
    // Phase 2 (post-barrier): use r3 to detect warp identity without
    //   a fixed global_tid threshold, then cross-load partner data.
    //
    // Expected result: r6[t] = own_value + partner_value
    //   Warp 0 thread t: r6 = (W*32+t+1) + ((W+1)*32+t+1)
    //   Warp 1 thread t: r6 = same (symmetric)
    //   where W = first warp ID of this kernel (from getNextWarpId())
    //
    // Instructions per warp: 15  →  2 warps = 30 total
    // L1 misses per warp:    32  →  2 warps = 64 total
    // Divergence events:      0  (each warp is homogeneous)
    return {
        //  0: r4 = global_tid + 1  (value to store)
        makeInstr(Opcode::ADDI, 4, 1, 0, 1),
        //  1: mem[r2] = r4
        makeInstr(Opcode::SW,   0, 2, 4, 0),
        //  2: sync – all warps wait
        makeInstr(Opcode::BARRIER, 0, 0, 0, 0),
        //  3: r9 = 1
        makeInstr(Opcode::ADDI, 9, 0, 0, 1),
        //  4: r7 = (local_warp_id < 1) ? 1 : 0  → warp-0 flag
        //     r3[t] = local_warp_id set by buildWarpContext
        makeInstr(Opcode::SLT,  7, 3, 9, 0),
        //  5: r8 = r7 XOR 1  → warp-1 flag
        makeInstr(Opcode::XOR,  8, 7, 9, 0),
        //  6: VBRANCH r7: warp-0 (r7=1) all-jump (mask=0); warp-1 (r7=0) falls through
        makeInstr(Opcode::VBRANCH, 0, 7, 0, 2),
        //  7: warp-1 only: load warp-0's value (128 bytes back)
        makeInstr(Opcode::LW,  5, 2, 0, -128),
        //  8: VJOIN (warp-0: restores from stack; warp-1: stack empty, no-op)
        makeInstr(Opcode::VJOIN, 0, 0, 0, 0),
        //  9: VBRANCH r8: warp-1 (r8=1) all-jump (mask=0); warp-0 (r8=0) falls through
        makeInstr(Opcode::VBRANCH, 0, 8, 0, 2),
        // 10: warp-0 only: load warp-1's value (128 bytes ahead)
        makeInstr(Opcode::LW,  5, 2, 0, 128),
        // 11: VJOIN (warp-1: restores; warp-0: stack empty, no-op)
        makeInstr(Opcode::VJOIN, 0, 0, 0, 0),
        // 12: r6 = own + partner
        makeInstr(Opcode::VADD, 6, 4, 5, 0),
        // 13: store result at r2+8 for verification
        makeInstr(Opcode::SW,  0, 2, 6, 8),
        // 14: done
        makeInstr(Opcode::HALT)
    };
}

// ─────────────────────────────────────────────────────────────────────────────
// [DIRECT] FP SAXPY
//
// Caller sets: r3[t]=floatAsReg(alpha), r4[t]=floatAsReg(x[t]),
//              r5[t]=floatAsReg(y)
// Result: r6[t] = floatAsReg(alpha * x[t] + y)
// Instructions: 3
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
// Instructions: 2
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
// Caller sets: r0[t]=thread_index, r3=alpha, r4[t]=x[t], r5=y
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


// ─────────────────────────────────────────────────────────────────────────────
// [DIRECT] FP GEMM  –  2×2 output tile, K=4 reduction depth
//
// Computes one output element C[row][col] per thread using 4 VFFMADD ops.
// This is the inner loop of any tiled GEMM: C[row][col] = dot(A_row, B_col).
//
// Caller must set registers before executeWarp():
//   r3[t]  = floatAsReg(A[row][0])    r8[t]  = floatAsReg(B[0][col])
//   r4[t]  = floatAsReg(A[row][1])    r9[t]  = floatAsReg(B[1][col])
//   r5[t]  = floatAsReg(A[row][2])    r10[t] = floatAsReg(B[2][col])
//   r6[t]  = floatAsReg(A[row][3])    r11[t] = floatAsReg(B[3][col])
//   r7[t]  = floatAsReg(0.0f)         (accumulator, must start at 0)
//
// Result: r7[t] = floatAsReg(A[row][0]*B[0][col] + ... + A[row][3]*B[3][col])
//
// Instructions: 5  (VFFMADD×4 + HALT)
//
// Example – 4 threads computing a 2×2 output tile:
//   A = [[1,2,3,4],[5,6,7,8]]      B = [[1,2],[3,4],[5,6],[7,8]]
//
//   thread  row  col   expected r7
//      0     0    0       50.0      (1*1+2*3+3*5+4*7)
//      1     0    1       60.0      (1*2+2*4+3*6+4*8)
//      2     1    0      114.0      (5*1+6*3+7*5+8*7)
//      3     1    1      140.0      (5*2+6*4+7*6+8*8)
// ─────────────────────────────────────────────────────────────────────────────
inline std::vector<Instruction> fpGemm() {
    return {
        makeInstr(Opcode::VFFMADD,  7,  3,  8, 0),  // r7 += r3  * r8   (k=0)
        makeInstr(Opcode::VFFMADD,  7,  4,  9, 0),  // r7 += r4  * r9   (k=1)
        makeInstr(Opcode::VFFMADD,  7,  5, 10, 0),  // r7 += r5  * r10  (k=2)
        makeInstr(Opcode::VFFMADD,  7,  6, 11, 0),  // r7 += r6  * r11  (k=3)
        makeInstr(Opcode::HALT)
    };
}


// ─────────────────────────────────────────────────────────────────────────────
// [DIRECT] 2D Convolution  –  3×3 filter, 2×2 output tile
//
// Each thread computes one output pixel of C = Input ★ Filter.
// Uses integer VFMADD (not FP) since filter coefficients are integers.
//
// Caller must set registers before executeWarp():
//   r3[t]..r11[t]  = 9 input neighborhood values (row-major, top-left→bottom-right)
//   r12[t]..r20[t] = 9 filter coefficients (same layout, identical for all threads)
//   r21[t]         = 0  (accumulator, must be initialised to 0)
//
// Result: r21[t] = dot(neighborhood[t], filter)
//
// Instructions: 10  (VFMADD×9 + HALT)
//
// Example – 4 threads computing a 2×2 output tile:
//
//   Input (4×4):           Filter (3×3):
//    1  2  3  4             1 2 1
//    5  6  7  8             2 4 2
//    9 10 11 12             1 2 1
//   13 14 15 16
//
//   thread  output pixel  neighborhood                  expected r21
//      0      out[0][0]   {1,2,3,5,6,7,9,10,11}           96
//      1      out[0][1]   {2,3,4,6,7,8,10,11,12}          112
//      2      out[1][0]   {5,6,7,9,10,11,13,14,15}        160
//      3      out[1][1]   {6,7,8,10,11,12,14,15,16}       176
// ─────────────────────────────────────────────────────────────────────────────
inline std::vector<Instruction> conv2d3x3() {
    return {
        makeInstr(Opcode::VFMADD, 21,  3, 12, 0),  // r21 += r3  * r12  (pos 0,0)
        makeInstr(Opcode::VFMADD, 21,  4, 13, 0),  // r21 += r4  * r13  (pos 0,1)
        makeInstr(Opcode::VFMADD, 21,  5, 14, 0),  // r21 += r5  * r14  (pos 0,2)
        makeInstr(Opcode::VFMADD, 21,  6, 15, 0),  // r21 += r6  * r15  (pos 1,0)
        makeInstr(Opcode::VFMADD, 21,  7, 16, 0),  // r21 += r7  * r16  (pos 1,1)
        makeInstr(Opcode::VFMADD, 21,  8, 17, 0),  // r21 += r8  * r17  (pos 1,2)
        makeInstr(Opcode::VFMADD, 21,  9, 18, 0),  // r21 += r9  * r18  (pos 2,0)
        makeInstr(Opcode::VFMADD, 21, 10, 19, 0),  // r21 += r10 * r19  (pos 2,1)
        makeInstr(Opcode::VFMADD, 21, 11, 20, 0),  // r21 += r11 * r20  (pos 2,2)
        makeInstr(Opcode::HALT)
    };
}

}  // kernels

}  // riscv_gpgpu


#endif  // RISCV_GPGPU_KERNEL_PROGRAMS_H