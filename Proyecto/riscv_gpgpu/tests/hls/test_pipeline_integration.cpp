// test_pipeline_integration.cpp - T022+T023 end-to-end integration test
//
// Wires the real compute_pipeline() (T022) directly to the real
// memory_pipeline() (T023) via hls::stream, each on its own thread - no
// stand-in/mock memory this time, exercising the actual cache/m_axi logic
// underneath a real LW/SW kernel. This is the capstone check that the two
// kernels' stream contracts (docs/hls/interfaces.md SS2.2's memory contract:
// one mem_resp_t per mem_req_t, store response data don't-care) actually
// agree in practice, not just against each side's own mocks.

#include <gtest/gtest.h>
#include <hls_stream.h>
#include <thread>
#include <vector>

#include "compute_unit/compute_pipeline.h"
#include "memory/memory_pipeline.h"
#include "../../models/systemc/src/common/kernel_programs.h"

using namespace riscv_gpgpu_hls;

namespace {

Instruction toHls(const riscv_gpgpu::Instruction& gi) {
    Instruction hi;
    hi.pc = gi.pc; hi.opcode = static_cast<Opcode>(gi.opcode);
    hi.rs1 = gi.rs1; hi.rs2 = gi.rs2; hi.rd = gi.rd; hi.imm = gi.imm;
    hi.is_vector = gi.is_vector; hi.is_memory = gi.is_memory; hi.is_branch = gi.is_branch;
    return hi;
}

void feedSlice(hls::stream<instr_word_t>& instr_in,
               const std::vector<riscv_gpgpu::Instruction>& program,
               size_t begin, size_t count) {
    for (size_t i = 0; i < count; ++i) instr_in.write(toHls(program[begin + i]));
}

}  // namespace

TEST(PipelineIntegration, MemoryRoundTripThroughRealMemoryPipeline) {
    static reg_t regs[MAX_THREADS_PER_WARP][NUM_REGS_PER_THREAD];
    for (int t = 0; t < MAX_THREADS_PER_WARP; ++t) {
        for (int r = 0; r < NUM_REGS_PER_THREAD; ++r) regs[t][r] = 0;
        regs[t][1] = t;
        // Address well above SHARED_MEM_SIZE_BYTES (48KB default) so this
        // exercises the L1/L2/m_axi path, not the shared-memory bypass.
        regs[t][2] = 0x200000 + t * 4;
    }

    auto program = riscv_gpgpu::kernels::memoryRoundTrip();
    hls::stream<instr_word_t>  instr_in("instr_in");
    hls::stream<mem_req_t>     mem_req("mem_req");
    hls::stream<mem_resp_t>    mem_resp("mem_resp");
    hls::stream<warp_status_t> status_out("status_out");
    for (const auto& gi : program) instr_in.write(toHls(gi));

    // global_mem backing store for memory_pipeline's m_axi port - large
    // enough to cover every lane's address plus a full cache line past the
    // highest one (t=31 -> 0x200000 + 31*4 + 127 well under this size).
    static std::vector<ap_uint<32>> backing(1u << 20, ap_uint<32>(0));

    std::thread memory_thread([&]() {
        memory_pipeline(mem_req, mem_resp, backing.data());
    });

    compute_pipeline(/*cu_id=*/0, /*warp_id=*/0, thread_mask_t(-1),
                      program.size(), instr_in, regs,
                      mem_req, mem_resp, status_out);

    EXPECT_EQ(status_out.read().code, WarpStatusCode::COMPLETE);

    // Golden: r3 (first LW, right after the SW) and r4 (second LW) both
    // equal global_tid - see kernel_programs.h's memoryRoundTrip() comment.
    for (int t = 0; t < MAX_THREADS_PER_WARP; ++t) {
        EXPECT_EQ(static_cast<uint32_t>(regs[t][3]), static_cast<uint32_t>(t)) << "lane " << t << " first LW";
        EXPECT_EQ(static_cast<uint32_t>(regs[t][4]), static_cast<uint32_t>(t)) << "lane " << t << " second LW";
    }

    // memory_pipeline is free-running (memory_pipeline.h's file header) -
    // it's permanently blocked on the next mem_req.read() now that
    // compute_pipeline is done issuing requests. Detach, don't join.
    memory_thread.detach();
}

// Centerpiece test for the divergence_stack.h bug fix (pulled in from
// upstream commit 9c4dfea "GPGPU READY" - see divergence_stack.h's
// handleBranch() comment): runs kernels::parallelReduction() through TWO
// independent compute_pipeline warp invocations, coordinated by a host loop
// that implements docs/hls/interfaces.md SS2.2's barrier contract exactly
// (track arrivals per barrier_id across all outstanding warps, resume once
// every warp for this kernel has reported that bid) - the same contract
// section 2.4 already assumed, just exercised here with 2 warps instead of 1
// for the first time. Wired to the REAL memory_pipeline (cross-warp LW/SW
// needs real, shared memory - no mock would prove anything here).
//
// Both warps hit the bug-fixed "all active lanes masked" case exactly once
// each (at different VBRANCH instructions - see the walkthrough this test's
// expected values were derived from): warp 0 at the first VBRANCH, warp 1 at
// the second. Expected values below are the SAME ones
// models/systemc/test/regression_test.cpp's Phase 11a checks against the
// golden model (0x10008->34, 0x1000C->36, 0x10088->34), not independently
// invented.
TEST(PipelineIntegration, ParallelReductionAcrossTwoWarpsWithBarrier) {
    static reg_t regsA[MAX_THREADS_PER_WARP][NUM_REGS_PER_THREAD];  // warp 0 (local_warp_id=0)
    static reg_t regsB[MAX_THREADS_PER_WARP][NUM_REGS_PER_THREAD];  // warp 1 (local_warp_id=1)
    for (int t = 0; t < MAX_THREADS_PER_WARP; ++t) {
        for (int r = 0; r < NUM_REGS_PER_THREAD; ++r) { regsA[t][r] = 0; regsB[t][r] = 0; }
        uint32_t tidA = 0 * 32 + t, tidB = 1 * 32 + t;      // global_tid, warp offset 0 and 32
        regsA[t][1] = tidA;  regsA[t][2] = 0x10000 + tidA * 4;  regsA[t][3] = 0;  // local_warp_id
        regsB[t][1] = tidB;  regsB[t][2] = 0x10000 + tidB * 4;  regsB[t][3] = 1;
    }

    auto program = riscv_gpgpu::kernels::parallelReduction();
    ASSERT_EQ(program.size(), 15u) << "pre/post-barrier split below assumes this exact layout";
    const size_t kPreBarrier  = 3;   // ADDI, SW, BARRIER
    const size_t kPostBarrier = program.size() - kPreBarrier;

    hls::stream<instr_word_t>  instrA("instrA"), instrB("instrB");
    hls::stream<mem_req_t>     mem_req("mem_req");
    hls::stream<mem_resp_t>    mem_resp("mem_resp");
    hls::stream<warp_status_t> status_out("status_out");

    static std::vector<ap_uint<32>> backing(1u << 16, ap_uint<32>(0));  // covers 0x10000..0x1FFFF
    std::thread memory_thread([&]() { memory_pipeline(mem_req, mem_resp, backing.data()); });

    // ── Pre-barrier: dispatch both warps, both must stall at bid=0 ─────────
    feedSlice(instrA, program, 0, kPreBarrier);
    compute_pipeline(0, /*warp_id=*/0, thread_mask_t(-1), kPreBarrier,
                      instrA, regsA, mem_req, mem_resp, status_out);
    warp_status_t stA1 = status_out.read();
    ASSERT_EQ(stA1.code, WarpStatusCode::STALLED_AT_BARRIER);
    ASSERT_EQ(stA1.barrier_id, 0);

    feedSlice(instrB, program, 0, kPreBarrier);
    compute_pipeline(0, /*warp_id=*/1, thread_mask_t(-1), kPreBarrier,
                      instrB, regsB, mem_req, mem_resp, status_out);
    warp_status_t stB1 = status_out.read();
    ASSERT_EQ(stB1.code, WarpStatusCode::STALLED_AT_BARRIER);
    ASSERT_EQ(stB1.barrier_id, 0);

    // ── Both warps (total_warps=2) have now reported bid=0 - barrier         ──
    // ── satisfied, host resumes both (order doesn't matter; resuming A then ──
    // ── B here) ──────────────────────────────────────────────────────────────
    feedSlice(instrA, program, kPreBarrier, kPostBarrier);
    compute_pipeline(0, 0, thread_mask_t(-1), kPostBarrier,
                      instrA, regsA, mem_req, mem_resp, status_out);
    EXPECT_EQ(status_out.read().code, WarpStatusCode::COMPLETE);

    feedSlice(instrB, program, kPreBarrier, kPostBarrier);
    compute_pipeline(0, 1, thread_mask_t(-1), kPostBarrier,
                      instrB, regsB, mem_req, mem_resp, status_out);
    EXPECT_EQ(status_out.read().code, WarpStatusCode::COMPLETE);

    // ── Verify against the golden model's own documented expected values ───
    EXPECT_EQ(static_cast<uint32_t>(regsA[0][6]), 34u) << "warp-0 thread-0: r6";
    EXPECT_EQ(static_cast<uint32_t>(regsA[1][6]), 36u) << "warp-0 thread-1: r6";
    EXPECT_EQ(static_cast<uint32_t>(regsB[0][6]), 34u) << "warp-1 thread-0: r6 (symmetric)";

    // Cross-check via the actual memory written (SW r6 to r2+8), not just the
    // register file - mirrors regression_test.cpp's top.readWord() checks.
    EXPECT_EQ(static_cast<uint32_t>(backing[0x10008 / 4]), 34u) << "mem[0x10008] (warp-0 thread-0)";
    EXPECT_EQ(static_cast<uint32_t>(backing[0x1000C / 4]), 36u) << "mem[0x1000C] (warp-0 thread-1)";
    EXPECT_EQ(static_cast<uint32_t>(backing[0x10088 / 4]), 34u) << "mem[0x10088] (warp-1 thread-0)";

    memory_thread.detach();
}

// Golden reference: kernel_programs.h's barrierRoundTrip() - "Each thread
// stores global_tid to r2, barrier syncs all warps, loads back. Result
// register: r3." Exercised upstream in benchmark_test.cpp (10-warp launch),
// not regression_test.cpp - ported here as a single warp, which is enough to
// prove what this kernel actually tests: that a value written to memory
// BEFORE a compute_pipeline stall/resume boundary is still correctly visible
// to the SAME warp's memory_pipeline access AFTER resuming - i.e. the
// barrier boundary doesn't lose or reorder memory state. This is a
// different property than ParallelReductionAcrossTwoWarpsWithBarrier's
// cross-WARP visibility check (and than ComputePipeline.BarrierStallThenResume's
// register-only, no-memory check) - own-warp, memory-crossing-the-barrier.
TEST(PipelineIntegration, BarrierRoundTripPreservesMemoryAcrossStall) {
    static reg_t regs[MAX_THREADS_PER_WARP][NUM_REGS_PER_THREAD];
    for (int t = 0; t < MAX_THREADS_PER_WARP; ++t) {
        for (int r = 0; r < NUM_REGS_PER_THREAD; ++r) regs[t][r] = 0;
        regs[t][1] = t;                          // global_tid (single warp, offset 0)
        regs[t][2] = 0x10000 + t * 4;             // above SHARED_MEM_SIZE_BYTES - exercises L1/L2/m_axi
    }

    auto program = riscv_gpgpu::kernels::barrierRoundTrip(/*barrier_id=*/0);
    ASSERT_EQ(program.size(), 4u) << "pre/post-barrier split below assumes this exact layout";
    const size_t kPreBarrier  = 2;  // SW, BARRIER
    const size_t kPostBarrier = program.size() - kPreBarrier;  // LW, HALT

    hls::stream<instr_word_t>  instr_in("instr_in");
    hls::stream<mem_req_t>     mem_req("mem_req");
    hls::stream<mem_resp_t>    mem_resp("mem_resp");
    hls::stream<warp_status_t> status_out("status_out");

    static std::vector<ap_uint<32>> backing(1u << 16, ap_uint<32>(0));  // covers 0x10000..0x1FFFF
    std::thread memory_thread([&]() { memory_pipeline(mem_req, mem_resp, backing.data()); });

    feedSlice(instr_in, program, 0, kPreBarrier);
    compute_pipeline(0, 0, thread_mask_t(-1), kPreBarrier,
                      instr_in, regs, mem_req, mem_resp, status_out);
    warp_status_t st1 = status_out.read();
    ASSERT_EQ(st1.code, WarpStatusCode::STALLED_AT_BARRIER);
    ASSERT_EQ(st1.barrier_id, 0);

    // Single-warp kernel: this one warp IS every warp for this launch -
    // the host contract (docs/hls/interfaces.md SS2.2/SS2.4) resumes as soon
    // as all outstanding warps for the kernel have reported the bid, which
    // for a 1-warp launch is immediate.
    feedSlice(instr_in, program, kPreBarrier, kPostBarrier);
    compute_pipeline(0, 0, thread_mask_t(-1), kPostBarrier,
                      instr_in, regs, mem_req, mem_resp, status_out);
    EXPECT_EQ(status_out.read().code, WarpStatusCode::COMPLETE);

    for (int t = 0; t < MAX_THREADS_PER_WARP; ++t) {
        EXPECT_EQ(static_cast<uint32_t>(regs[t][3]), static_cast<uint32_t>(t))
            << "lane " << t << ": r3 should equal global_tid (value written before the barrier)";
    }

    memory_thread.detach();
}
