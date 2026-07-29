// test_gpgpu_top.cpp - capstone end-to-end test for the on-chip scheduler
//
// Unlike every other barrier test in this project (ComputePipeline.
// BarrierStallThenResume, PipelineIntegration.
// ParallelReductionAcrossTwoWarpsWithBarrier), which manually sequence
// "dispatch, wait, dispatch, wait, resume both" from the test itself, this
// file drives real kernels through GpgpuTop/schedulerStep() (docs/hls/
// interfaces.md SS10.12) with ZERO test-side dispatch/barrier
// orchestration - the scheduler decides everything on its own. This is the
// concrete proof that the on-chip architecture decided across
// SS10.1-SS10.11 actually works end-to-end, not just that its pieces
// compile individually.
//
// Formalizes the smoke test written during SS10.12's implementation pass
// into real GTest coverage - same two scenarios, same expected values.

#include <gtest/gtest.h>
#include <thread>
#include <vector>

#include "scheduler/gpgpu_top.h"
#include "scheduler/mem_arbiter.h"
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

void loadProgram(instr_word_t program[MAX_PROGRAM_LEN],
                  const std::vector<riscv_gpgpu::Instruction>& src) {
    for (size_t i = 0; i < src.size(); ++i) program[i] = toHls(src[i]);
}

}  // namespace

TEST(GpgpuTop, SingleWarpKernelRunsToCompletionAutonomously) {
    auto golden = riscv_gpgpu::kernels::intSaxpy(/*alpha=*/2, /*y=*/10);
    static instr_word_t dram_program[MAX_PROGRAM_LEN];
    loadProgram(dram_program, golden);

    // static, not stack-local: compute_pipeline's thread below is detached,
    // not joined - it's a free-running kernel that never returns, so it's
    // still blocked reading dispatch_out/mem_req/etc. long after this TEST
    // function itself has returned. Capturing stack-local objects by
    // reference into a detached thread is a real dangling-reference bug
    // (found while formalizing this test: it crashed on process exit with
    // NUM_CUS>1... test cases sharing one process, non-deterministically,
    // classic UB) - static duration keeps them alive for the process
    // lifetime, matching the convention this file already used for
    // dram_program/backing.
    static GpgpuTop top;
    for (int t = 0; t < MAX_THREADS_PER_WARP; ++t) {
        for (int r = 0; r < NUM_REGS_PER_THREAD; ++r) top.cu(0).regsArray()[0][t][r] = 0;
        top.cu(0).regsArray()[0][t][1] = t;
    }

    static hls::stream<warp_dispatch_t> dispatch_out[NUM_CUS];
    static hls::stream<warp_status_t>   status_in[NUM_CUS];
    static hls::stream<mem_req_t>       mem_req[NUM_CUS];
    static hls::stream<mem_resp_t>      mem_resp[NUM_CUS];

    std::thread cp[NUM_CUS];
    for (int i = 0; i < NUM_CUS; ++i) {
        cp[i] = std::thread([&, i]() {
            compute_pipeline(cu_id_t(i), dispatch_out[i], top.cu(i).programArray(),
                              golden.size(), top.cu(i).regsArray(),
                              mem_req[i], mem_resp[i], status_in[i]);
        });
    }

    ASSERT_TRUE(top.launchKernel(dram_program, golden.size(), /*total_warps=*/1));

    int rounds = 0;
    while (!top.kernelComplete()) {
        schedulerStep(top, dispatch_out, status_in);
        ASSERT_LT(++rounds, 100000) << "scheduler must make progress, not spin forever";
    }

    // Golden: r6[t] = (global_tid + 1) * alpha + y = (t+1)*2 + 10 - same
    // expected values as ComputePipeline.IntSaxpy (test_compute_pipeline.cpp).
    for (int t = 0; t < MAX_THREADS_PER_WARP; ++t) {
        uint32_t expect = (t + 1) * 2 + 10;
        EXPECT_EQ(static_cast<uint32_t>(top.cu(0).regsArray()[0][t][6]), expect) << "lane " << t;
    }
    for (auto& th : cp) th.detach();
}

TEST(GpgpuTop, TwoWarpBarrierKernelDrivenEntirelyByTheScheduler) {
    auto golden = riscv_gpgpu::kernels::parallelReduction();
    static instr_word_t dram_program[MAX_PROGRAM_LEN];
    loadProgram(dram_program, golden);

    // static for the same reason as the single-warp test above: three
    // separate threads here (compute_pipeline, mem_arbiter, memory_pipeline)
    // all get detached, not joined, and stay blocked on these streams long
    // after this function returns.
    static GpgpuTop top;
    for (int t = 0; t < MAX_THREADS_PER_WARP; ++t) {
        for (int r = 0; r < NUM_REGS_PER_THREAD; ++r) {
            top.cu(0).regsArray()[0][t][r] = 0;   // warp 0 -> CU0 slot 0
            top.cu(0).regsArray()[1][t][r] = 0;   // warp 1 -> CU0 slot 1 (NUM_CUS=1)
        }
        uint32_t tidA = 0 * 32 + t, tidB = 1 * 32 + t;
        top.cu(0).regsArray()[0][t][1] = tidA; top.cu(0).regsArray()[0][t][2] = 0x10000 + tidA * 4; top.cu(0).regsArray()[0][t][3] = 0;
        top.cu(0).regsArray()[1][t][1] = tidB; top.cu(0).regsArray()[1][t][2] = 0x10000 + tidB * 4; top.cu(0).regsArray()[1][t][3] = 1;
    }

    static hls::stream<warp_dispatch_t> dispatch_out[NUM_CUS];
    static hls::stream<warp_status_t>   status_in[NUM_CUS];
    static hls::stream<mem_req_t>       cu_mem_req[NUM_CUS];
    static hls::stream<mem_resp_t>      cu_mem_resp[NUM_CUS];
    static hls::stream<mem_req_t>       mem_req_single("mem_req_single");
    static hls::stream<mem_resp_t>      mem_resp_single("mem_resp_single");

    static std::vector<ap_uint<32>> backing(1u << 16, ap_uint<32>(0));  // covers 0x10000..0x1FFFF

    std::thread cp[NUM_CUS];
    for (int i = 0; i < NUM_CUS; ++i) {
        cp[i] = std::thread([&, i]() {
            compute_pipeline(cu_id_t(i), dispatch_out[i], top.cu(i).programArray(),
                              golden.size(), top.cu(i).regsArray(),
                              cu_mem_req[i], cu_mem_resp[i], status_in[i]);
        });
    }
    std::thread arb_thread([&]() {
        mem_arbiter(cu_mem_req, cu_mem_resp, mem_req_single, mem_resp_single);
    });
    std::thread mem_thread([&]() {
        memory_pipeline(mem_req_single, mem_resp_single, backing.data());
    });

    ASSERT_TRUE(top.launchKernel(dram_program, golden.size(), /*total_warps=*/2))
        << "2 <= NUM_CUS*MAX_WARPS_PER_CU, must be accepted";

    int rounds = 0;
    while (!top.kernelComplete()) {
        schedulerStep(top, dispatch_out, status_in);
        ASSERT_LT(++rounds, 1000000) << "scheduler must make progress through the barrier, not deadlock";
    }

    // Same golden-model expected values as PipelineIntegration.
    // ParallelReductionAcrossTwoWarpsWithBarrier (test_pipeline_integration.cpp),
    // reached this time via the autonomous on-chip scheduler.
    EXPECT_EQ(static_cast<uint32_t>(top.cu(0).regsArray()[0][0][6]), 34u) << "warp-0 thread-0: r6";
    EXPECT_EQ(static_cast<uint32_t>(top.cu(0).regsArray()[0][1][6]), 36u) << "warp-0 thread-1: r6";
    EXPECT_EQ(static_cast<uint32_t>(top.cu(0).regsArray()[1][0][6]), 34u) << "warp-1 thread-0: r6 (symmetric)";
    EXPECT_EQ(static_cast<uint32_t>(backing[0x10008 / 4]), 34u) << "mem[0x10008] (warp-0 thread-0)";
    EXPECT_EQ(static_cast<uint32_t>(backing[0x1000C / 4]), 36u) << "mem[0x1000C] (warp-0 thread-1)";
    EXPECT_EQ(static_cast<uint32_t>(backing[0x10088 / 4]), 34u) << "mem[0x10088] (warp-1 thread-0)";

    for (auto& th : cp) th.detach();
    arb_thread.detach();
    mem_thread.detach();
}

TEST(GpgpuTop, OverCapacityLaunchIsRejectedNotSilentlyHung) {
    auto golden = riscv_gpgpu::kernels::intSaxpy(2, 10);
    static instr_word_t dram_program[MAX_PROGRAM_LEN];
    loadProgram(dram_program, golden);

    GpgpuTop top;
    bool ok = top.launchKernel(dram_program, golden.size(),
                                warp_id_t(NUM_CUS * MAX_WARPS_PER_CU + 1));
    EXPECT_FALSE(ok);
    EXPECT_TRUE(top.launchFault());
}
