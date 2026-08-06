// test_pipeline_integration.cpp - compute_pipeline + memory_pipeline
// end-to-end integration test
//
// Wires the real compute_pipeline() to the real memory_pipeline() via
// hls::stream, each on its own thread - no stand-in/mock memory, exercising
// the actual cache/m_axi logic underneath a real LW/SW kernel. This is the
// capstone check that the two kernels' stream contracts (docs/hls/
// interfaces.md SS2.2's memory contract: one mem_resp_t per mem_req_t, store
// response data don't-care) actually agree in practice, not just against
// each side's own mocks.
//
// Rewritten per docs/hls/interfaces.md SS2.5.3: compute_pipeline is now
// free-running/stream-dispatched. Per user direction, every kernel program
// and every expected value below is UNCHANGED from the original T022+T023
// version of this file - only the invocation harness changed (CpFixture,
// matching test_compute_pipeline.cpp's).

#include <gtest/gtest.h>
#include <hls_stream.h>
#include <thread>
#include <vector>

#include "compute_unit/compute_pipeline.h"
#include "compute_unit/rv32i_codec.h"
#include "memory/memory_pipeline.h"
#include "../../models/systemc/src/common/kernel_programs.h"

using namespace riscv_gpgpu_hls;

namespace {

// docs/hls/interfaces.md SS13/SS13.12: see test_compute_pipeline.cpp's
// loadProgram() for the full rationale (identical here) - program[] now
// holds raw_instr_t, and one golden instruction can expand to two raw
// words (LUI+ADDI), so the output index is tracked separately.
// Returns the actual number of raw words written - see
// test_compute_pipeline.cpp's loadProgram() for why this must be used
// instead of src.size() (SS13.12 expansion).
size_t loadProgram(instr_word_t program[MAX_PROGRAM_LEN],
                    const std::vector<riscv_gpgpu::Instruction>& src) {
    size_t out_i = 0;
    for (size_t i = 0; i < src.size(); ++i) {
        const riscv_gpgpu::Instruction& gi = src[i];
        raw_instr_t words[2];
        int n = encodeInstructionExpanded(static_cast<Opcode>(gi.opcode),
                                           gi.rd, gi.rs1, gi.rs2, gi.imm, words);
        for (int k = 0; k < n; ++k) program[out_i++] = words[k];
    }
    return out_i;
}

// Same shape as test_compute_pipeline.cpp's CpFixture (duplicated locally,
// matching this file's existing convention of not sharing helpers with
// test_compute_pipeline.cpp - toHls() above is likewise a separate copy).
// mem_req_out/mem_resp_in here connect to a real memory_pipeline instance,
// not a mock.
//
// IMPORTANT: every instance MUST be declared `static CpFixture cp;` at its
// call site, not a plain local - see test_compute_pipeline.cpp's copy of
// this comment for the dangling-reference bug this avoids (both
// compute_thread and memory_thread here are detached and outlive the
// owning TEST() function).
struct CpFixture {
    hls::stream<warp_dispatch_t> dispatch_in{"dispatch_in"};
    hls::stream<mem_req_t>       mem_req{"mem_req"};
    hls::stream<mem_resp_t>      mem_resp{"mem_resp"};
    hls::stream<warp_status_t>   status_out{"status_out"};
    instr_word_t program[MAX_PROGRAM_LEN];
    std::thread compute_thread, memory_thread;

    void start(reg_t regs[MAX_WARPS_PER_CU][MAX_THREADS_PER_WARP][NUM_REGS_PER_THREAD],
               uint32_t program_len, ap_uint<32>* global_mem, cu_id_t cu_id = 0) {
        compute_thread = std::thread([this, regs, program_len, cu_id]() {
            hls::stream<reg_seed_t> reg_seed_in;
            compute_pipeline(cu_id, dispatch_in, program, program_len, regs,
                              reg_seed_in, mem_req, mem_resp, status_out);
        });
        memory_thread = std::thread([this, global_mem]() {
            memory_pipeline(mem_req, mem_resp, global_mem);
        });
    }

    warp_status_t dispatchAndWait(slot_id_t slot, warp_id_t warp_id,
                                   thread_mask_t mask, ap_uint<16> resume_pc = 0) {
        warp_dispatch_t d;
        d.slot_id = slot; d.warp_id = warp_id;
        d.active_mask_init = mask; d.resume_pc = resume_pc;
        dispatch_in.write(d);
        return status_out.read();
    }

    // Both compute_pipeline and memory_pipeline are free-running - neither
    // returns, so both threads are permanently blocked on their next read
    // once a test is done with them. Detach, don't join (same convention
    // the pre-rewrite version of this file already used for memory_thread).
    ~CpFixture() { compute_thread.detach(); memory_thread.detach(); }
};

}  // namespace

TEST(PipelineIntegration, MemoryRoundTripThroughRealMemoryPipeline) {
    static reg_t regs[MAX_WARPS_PER_CU][MAX_THREADS_PER_WARP][NUM_REGS_PER_THREAD];
    for (int t = 0; t < MAX_THREADS_PER_WARP; ++t) {
        for (int r = 0; r < NUM_REGS_PER_THREAD; ++r) regs[0][t][r] = 0;
        regs[0][t][1] = t;
        // Address well above SHARED_MEM_SIZE_BYTES (48KB default) so this
        // exercises the L1/L2/m_axi path, not the shared-memory bypass.
        regs[0][t][2] = 0x200000 + t * 4;
    }

    auto program = riscv_gpgpu::kernels::memoryRoundTrip();

    // global_mem backing store for memory_pipeline's m_axi port - large
    // enough to cover every lane's address plus a full cache line past the
    // highest one (t=31 -> 0x200000 + 31*4 + 127 well under this size).
    static std::vector<ap_uint<32>> backing(1u << 20, ap_uint<32>(0));

    static CpFixture cp;
    size_t program_len = loadProgram(cp.program, program);
    cp.start(regs, program_len, backing.data());

    warp_status_t st = cp.dispatchAndWait(0, 0, thread_mask_t(-1));
    EXPECT_EQ(st.code, WarpStatusCode::COMPLETE);

    // Golden: r3 (first LW, right after the SW) and r4 (second LW) both
    // equal global_tid - see kernel_programs.h's memoryRoundTrip() comment.
    for (int t = 0; t < MAX_THREADS_PER_WARP; ++t) {
        EXPECT_EQ(static_cast<uint32_t>(regs[0][t][3]), static_cast<uint32_t>(t)) << "lane " << t << " first LW";
        EXPECT_EQ(static_cast<uint32_t>(regs[0][t][4]), static_cast<uint32_t>(t)) << "lane " << t << " second LW";
    }
}

// Centerpiece test for the divergence_stack.h bug fix (pulled in from
// upstream commit 9c4dfea "GPGPU READY" - see divergence_stack.h's
// handleBranch() comment): runs kernels::parallelReduction() through TWO
// warps sharing one CU's two resident slots (docs/hls/interfaces.md
// SS2.5.5/SS10.6 - both fit comfortably within MAX_WARPS_PER_CU=4), tracked
// by BarrierArbiter's global-arrival contract (superseding SS2.4's
// host-orchestrated one - track arrivals across all outstanding warps,
// resume once every warp for this kernel has reported the barrier). Wired
// to the REAL memory_pipeline (cross-warp LW/SW needs real, shared memory -
// no mock would prove anything here). This test plays BarrierArbiter's role
// by hand (dispatch both, wait for both STALLED, then dispatch both resumes)
// since BarrierArbiter itself isn't wired into a top-level system yet.
//
// Both warps hit the bug-fixed "all active lanes masked" case exactly once
// each (at different VBRANCH instructions - see the walkthrough this test's
// expected values were derived from): warp 0 at the first VBRANCH, warp 1 at
// the second. Expected values below are the SAME ones
// models/systemc/test/regression_test.cpp's Phase 11a checks against the
// golden model (0x10008->34, 0x1000C->36, 0x10088->34), not independently
// invented.
TEST(PipelineIntegration, ParallelReductionAcrossTwoWarpsWithBarrier) {
    static reg_t regs[MAX_WARPS_PER_CU][MAX_THREADS_PER_WARP][NUM_REGS_PER_THREAD];  // slot0=warp0, slot1=warp1
    for (int t = 0; t < MAX_THREADS_PER_WARP; ++t) {
        for (int r = 0; r < NUM_REGS_PER_THREAD; ++r) { regs[0][t][r] = 0; regs[1][t][r] = 0; }
        uint32_t tidA = 0 * 32 + t, tidB = 1 * 32 + t;      // global_tid, warp offset 0 and 32
        regs[0][t][1] = tidA;  regs[0][t][2] = 0x10000 + tidA * 4;  regs[0][t][3] = 0;  // local_warp_id
        regs[1][t][1] = tidB;  regs[1][t][2] = 0x10000 + tidB * 4;  regs[1][t][3] = 1;
    }

    auto program = riscv_gpgpu::kernels::parallelReduction();

    static std::vector<ap_uint<32>> backing(1u << 16, ap_uint<32>(0));  // covers 0x10000..0x1FFFF

    static CpFixture cp;
    size_t program_len = loadProgram(cp.program, program);
    cp.start(regs, program_len, backing.data());

    // ── Pre-barrier: dispatch both warps, both must stall at bid=0 ─────────
    warp_status_t stA1 = cp.dispatchAndWait(/*slot=*/0, /*warp_id=*/0, thread_mask_t(-1));
    ASSERT_EQ(stA1.code, WarpStatusCode::STALLED_AT_BARRIER);
    ASSERT_EQ(stA1.barrier_id, 0);

    warp_status_t stB1 = cp.dispatchAndWait(/*slot=*/1, /*warp_id=*/1, thread_mask_t(-1));
    ASSERT_EQ(stB1.code, WarpStatusCode::STALLED_AT_BARRIER);
    ASSERT_EQ(stB1.barrier_id, 0);

    // ── Both warps (total_warps=2) have now reported bid=0 - barrier         ──
    // ── satisfied, resume both from their own reported resume_pc            ──
    EXPECT_EQ(cp.dispatchAndWait(0, 0, thread_mask_t(-1), stA1.resume_pc).code, WarpStatusCode::COMPLETE);
    EXPECT_EQ(cp.dispatchAndWait(1, 1, thread_mask_t(-1), stB1.resume_pc).code, WarpStatusCode::COMPLETE);

    // ── Verify against the golden model's own documented expected values ───
    EXPECT_EQ(static_cast<uint32_t>(regs[0][0][6]), 34u) << "warp-0 thread-0: r6";
    EXPECT_EQ(static_cast<uint32_t>(regs[0][1][6]), 36u) << "warp-0 thread-1: r6";
    EXPECT_EQ(static_cast<uint32_t>(regs[1][0][6]), 34u) << "warp-1 thread-0: r6 (symmetric)";

    // Cross-check via the actual memory written (SW r6 to r2+8), not just the
    // register file - mirrors regression_test.cpp's top.readWord() checks.
    EXPECT_EQ(static_cast<uint32_t>(backing[0x10008 / 4]), 34u) << "mem[0x10008] (warp-0 thread-0)";
    EXPECT_EQ(static_cast<uint32_t>(backing[0x1000C / 4]), 36u) << "mem[0x1000C] (warp-0 thread-1)";
    EXPECT_EQ(static_cast<uint32_t>(backing[0x10088 / 4]), 34u) << "mem[0x10088] (warp-1 thread-0)";
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
//
// Note (docs/hls/interfaces.md SS10.3): a real 10-warp joint barrier, as the
// golden benchmark actually launches, is out of scope for this port
// regardless of single-CU vs multi-CU - MAX_WARPS_PER_CU=4 caps how many
// warps can jointly rendezvous at all (SS10.6's hazard mitigation). This
// test's single-warp scope was already correct for that reason, not just
// "multi-GPU distribution is out of scope" as an earlier note here said.
TEST(PipelineIntegration, BarrierRoundTripPreservesMemoryAcrossStall) {
    static reg_t regs[MAX_WARPS_PER_CU][MAX_THREADS_PER_WARP][NUM_REGS_PER_THREAD];
    for (int t = 0; t < MAX_THREADS_PER_WARP; ++t) {
        for (int r = 0; r < NUM_REGS_PER_THREAD; ++r) regs[0][t][r] = 0;
        regs[0][t][1] = t;                          // global_tid (single warp, offset 0)
        regs[0][t][2] = 0x10000 + t * 4;             // above SHARED_MEM_SIZE_BYTES - exercises L1/L2/m_axi
    }

    auto program = riscv_gpgpu::kernels::barrierRoundTrip(/*barrier_id=*/0);

    static std::vector<ap_uint<32>> backing(1u << 16, ap_uint<32>(0));  // covers 0x10000..0x1FFFF

    static CpFixture cp;
    size_t program_len = loadProgram(cp.program, program);
    cp.start(regs, program_len, backing.data());

    warp_status_t st1 = cp.dispatchAndWait(0, 0, thread_mask_t(-1));
    ASSERT_EQ(st1.code, WarpStatusCode::STALLED_AT_BARRIER);
    ASSERT_EQ(st1.barrier_id, 0);

    // Single-warp kernel: this one warp IS every warp for this launch - the
    // barrier contract (docs/hls/interfaces.md SS2.5.5) resumes as soon as
    // all outstanding warps for the kernel have reported the bid, which for
    // a 1-warp launch is immediate.
    warp_status_t st2 = cp.dispatchAndWait(0, 0, thread_mask_t(-1), st1.resume_pc);
    EXPECT_EQ(st2.code, WarpStatusCode::COMPLETE);

    for (int t = 0; t < MAX_THREADS_PER_WARP; ++t) {
        EXPECT_EQ(static_cast<uint32_t>(regs[0][t][3]), static_cast<uint32_t>(t))
            << "lane " << t << ": r3 should equal global_tid (value written before the barrier)";
    }
}
