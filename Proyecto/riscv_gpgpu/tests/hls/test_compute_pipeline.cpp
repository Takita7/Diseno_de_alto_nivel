// test_compute_pipeline.cpp - regression tests for compute_pipeline
//
// Drives compute_pipeline with the SAME kernel programs used against the
// golden SystemC model (models/systemc/src/common/kernel_programs.h),
// translated instruction-by-instruction into riscv_gpgpu_hls::Instruction.
// Expected register values below are derived by hand-tracing the same
// kernels through ComputeUnit's documented semantics (kernel_programs.h's
// own per-kernel comments), not independently invented.
//
// Rewritten per docs/hls/interfaces.md SS2.5.3: compute_pipeline is now
// free-running/stream-dispatched instead of a per-invocation ap_ctrl_hs
// call. Per user direction, every kernel program and every expected value
// below is UNCHANGED from the original T022 version of this file - only
// the invocation harness (CpFixture) changed, to match compute_pipeline's
// new signature (docs/hls/interfaces.md SS2.5.3): a persistent background
// thread fed via dispatch_in/status_out instead of one blocking call per
// warp, and a plain program[] array instead of a fed instr_in stream.
//
// Kernels requiring memory (memoryRoundTrip) are serviced by a worker
// thread acting as a temporary stand-in for memory_pipeline - hls::stream
// in this Vitis HLS version is mutex+condvar protected specifically to
// support this producer/consumer-thread csim pattern (see hls_stream.h's
// stream_entity::read()/write()), so this is the intended usage, not a
// workaround.

#include <gtest/gtest.h>
#include <hls_stream.h>
#include <thread>
#include <cstdint>
#include <map>

#include "compute_unit/compute_pipeline.h"
#include "compute_unit/rv32i_codec.h"
#include "../../models/systemc/src/common/kernel_programs.h"

using namespace riscv_gpgpu_hls;

namespace {

// docs/hls/interfaces.md SS13: program[] now holds raw_instr_t (real
// RV32I/custom-opcode encoded words), not decoded Instruction structs -
// encodeInstructionExpanded() replaces the old field-by-field struct copy.
// gi.is_vector/is_memory/is_branch and gi.pc aren't passed through: the
// former are re-derived by decodeInstruction() from the opcode alone
// (identical to how the golden model's own makeInstr() derives them,
// types.h:112-114 - not new information), and the latter was never read
// downstream (grep-confirmed before rv32i_codec.h was written).
//
// SS13.12: one golden instruction can expand to two raw words (LUI+ADDI),
// so the output index is tracked separately from the input index - this is
// exactly the kernel_programs.h::fpUniformSaxpy() case (ADDI loading a
// float's raw bit pattern, which doesn't fit real RV32I's 12-bit ADDI
// immediate). Copies a golden instruction sequence into a plain program[]
// array - replaces the old feedProgram()'s stream-writing role now that
// compute_pipeline reads a local array instead of a sequential stream
// (docs/hls/interfaces.md SS10.8).
// Returns the actual number of raw words written - the caller's program_len
// (compute_pipeline's fetch-loop bound), which is NOT necessarily src.size()
// once expansion (SS13.12) is possible. Every call site below uses this
// return value, not src.size(), for exactly that reason.
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

// Standard per-lane register convention (matches kernel_programs.h's header
// comment / GPGPUTop::buildWarpContext): r0=0, r1=global_tid, r2=unique addr,
// r3=local_warp_id. UNCHANGED from the original T022 test file - still
// operates on a single slot's RegFile (MAX_THREADS_PER_WARP x
// NUM_REGS_PER_THREAD), the same shape it always did; only the caller now
// passes regs[slot] instead of a bare regs[][].
void initRegs(reg_t regs[MAX_THREADS_PER_WARP][NUM_REGS_PER_THREAD],
              uint32_t global_tid_offset = 0, uint32_t local_warp_id = 0) {
    for (int t = 0; t < MAX_THREADS_PER_WARP; ++t) {
        for (int r = 0; r < NUM_REGS_PER_THREAD; ++r) regs[t][r] = 0;
        uint32_t global_tid = global_tid_offset + t;
        regs[t][1] = global_tid;
        regs[t][2] = 0x1000 + global_tid * 4;  // unique word-aligned address per lane
        regs[t][3] = local_warp_id;
    }
}

// Wraps a free-running compute_pipeline instance for one test: starts it on
// a background thread (detached at scope exit - it never returns, same
// convention test_pipeline_integration.cpp's memory_thread already used for
// the free-running memory_pipeline), and provides a blocking
// dispatch-then-wait-for-status call matching the shape every test below
// needs (docs/hls/interfaces.md SS2.5.3's dispatch_in/status_out contract).
//
// IMPORTANT: every instance MUST be declared `static CpFixture cp;` at its
// call site, not a plain local. Real bug found and fixed while formalizing
// test_gpgpu_top.cpp (docs/hls/interfaces.md SS10.12): the detach()'d
// thread's lambda captures `this`, and keeps running (blocked reading
// dispatch_in) long after the owning TEST() function returns - a plain
// stack-local CpFixture would be destroyed at that point while the
// detached thread still holds a dangling reference to it. `static` gives
// it process-lifetime duration instead, matching the fix already applied
// to test_gpgpu_top.cpp's equivalent objects.
struct CpFixture {
    hls::stream<warp_dispatch_t> dispatch_in{"dispatch_in"};
    hls::stream<mem_req_t>       mem_req_out{"mem_req_out"};
    hls::stream<mem_resp_t>      mem_resp_in{"mem_resp_in"};
    hls::stream<warp_status_t>   status_out{"status_out"};
    instr_word_t program[MAX_PROGRAM_LEN];
    std::thread th;

    void start(reg_t regs[MAX_WARPS_PER_CU][MAX_THREADS_PER_WARP][NUM_REGS_PER_THREAD],
               uint32_t program_len, cu_id_t cu_id = 0) {
        th = std::thread([this, regs, program_len, cu_id]() {
            // nullptr: docs/hls/interfaces.md SS16's initial_regs_ptr is only
            // dereferenced when a dispatch has fresh_launch=true, and every
            // warp_dispatch_t this fixture builds (dispatchAndWait() below)
            // defaults it false - this fixture pokes regs directly instead,
            // same as before SS16 existed.
            compute_pipeline(cu_id, dispatch_in, program, program_len, regs, nullptr,
                              mem_req_out, mem_resp_in, status_out);
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

    ~CpFixture() { th.detach(); }
};

}  // namespace

TEST(ComputePipeline, IntSaxpy) {
    static reg_t regs[MAX_WARPS_PER_CU][MAX_THREADS_PER_WARP][NUM_REGS_PER_THREAD];
    initRegs(regs[0]);

    auto program = riscv_gpgpu::kernels::intSaxpy(/*alpha=*/2, /*y=*/10);
    static CpFixture cp;
    size_t program_len = loadProgram(cp.program, program);
    cp.start(regs, program_len);

    warp_status_t st = cp.dispatchAndWait(0, 0, thread_mask_t(-1));
    EXPECT_EQ(st.code, WarpStatusCode::COMPLETE);

    // Golden: r6[t] = (global_tid + 1) * alpha + y = (t+1)*2 + 10
    for (int t = 0; t < MAX_THREADS_PER_WARP; ++t) {
        uint32_t expect = (t + 1) * 2 + 10;
        EXPECT_EQ(static_cast<uint32_t>(regs[0][t][6]), expect) << "lane " << t;
    }
}

TEST(ComputePipeline, FpUniformSaxpy) {
    static reg_t regs[MAX_WARPS_PER_CU][MAX_THREADS_PER_WARP][NUM_REGS_PER_THREAD];
    initRegs(regs[0]);

    auto program = riscv_gpgpu::kernels::fpUniformSaxpy(2.0f, 3.0f, 1.0f);
    static CpFixture cp;
    size_t program_len = loadProgram(cp.program, program);
    cp.start(regs, program_len);

    EXPECT_EQ(cp.dispatchAndWait(0, 0, thread_mask_t(-1)).code, WarpStatusCode::COMPLETE);

    // Golden: r6 = alpha*x + y = 2*3+1 = 7.0, same for every lane (no
    // per-thread dependency in this kernel).
    for (int t = 0; t < MAX_THREADS_PER_WARP; ++t) {
        EXPECT_FLOAT_EQ(regAsFloat(regs[0][t][6]), 7.0f) << "lane " << t;
    }
}

TEST(ComputePipeline, DivergentOddEven) {
    static reg_t regs[MAX_WARPS_PER_CU][MAX_THREADS_PER_WARP][NUM_REGS_PER_THREAD];
    initRegs(regs[0]);

    auto program = riscv_gpgpu::kernels::divergentOddEven();
    static CpFixture cp;
    size_t program_len = loadProgram(cp.program, program);
    cp.start(regs, program_len);

    EXPECT_EQ(cp.dispatchAndWait(0, 0, thread_mask_t(-1)).code, WarpStatusCode::COMPLETE);

    // Golden: r5[t] = 100 for even threads (fall through VBRANCH), 0 for odd
    // (masked off, never executes the ADDI that sets r5).
    for (int t = 0; t < MAX_THREADS_PER_WARP; ++t) {
        uint32_t expect = (t % 2 == 0) ? 100u : 0u;
        EXPECT_EQ(static_cast<uint32_t>(regs[0][t][5]), expect) << "lane " << t;
    }
}

// Exercises BARRIER stall/resume: does compute_pipeline (a) report
// STALLED_AT_BARRIER with the right bid and the right resume_pc (SS2.5.3 -
// NEW field, the old per-invocation design never needed one, see
// hls_types.h), and (b) correctly resume from the reported resume_pc,
// sharing the same regs[slot] across both dispatches.
TEST(ComputePipeline, BarrierStallThenResume) {
    static reg_t regs[MAX_WARPS_PER_CU][MAX_THREADS_PER_WARP][NUM_REGS_PER_THREAD];
    initRegs(regs[0]);

    std::vector<riscv_gpgpu::Instruction> program = {
        riscv_gpgpu::makeInstr(riscv_gpgpu::Opcode::ADDI, /*rd=*/7, /*rs1=*/0, /*rs2=*/0, /*imm=*/42),
        riscv_gpgpu::makeInstr(riscv_gpgpu::Opcode::BARRIER, 0, 0, 0, /*barrier_id=*/5),
        riscv_gpgpu::makeInstr(riscv_gpgpu::Opcode::ADDI, /*rd=*/8, /*rs1=*/7, /*rs2=*/0, /*imm=*/1),
        riscv_gpgpu::makeInstr(riscv_gpgpu::Opcode::HALT),
    };

    static CpFixture cp;
    size_t program_len = loadProgram(cp.program, program);
    cp.start(regs, program_len);

    warp_status_t st1 = cp.dispatchAndWait(0, 0, thread_mask_t(-1));
    ASSERT_EQ(st1.code, WarpStatusCode::STALLED_AT_BARRIER);
    EXPECT_EQ(st1.barrier_id, 5);
    EXPECT_EQ(st1.resume_pc, 2) << "resume_pc must point past the BARRIER instruction";
    for (int t = 0; t < MAX_THREADS_PER_WARP; ++t) {
        EXPECT_EQ(static_cast<uint32_t>(regs[0][t][7]), 42u) << "lane " << t << " pre-barrier write lost";
    }

    // Resume from the reported resume_pc - mask re-widened to full per
    // SS2.5.3's "after a barrier all threads are synchronised" contract
    // (mirrors compute_unit.cpp's initializeWarp() call on every
    // invocation, including resumes).
    warp_status_t st2 = cp.dispatchAndWait(0, 0, thread_mask_t(-1), st1.resume_pc);
    EXPECT_EQ(st2.code, WarpStatusCode::COMPLETE);
    for (int t = 0; t < MAX_THREADS_PER_WARP; ++t) {
        EXPECT_EQ(static_cast<uint32_t>(regs[0][t][8]), 43u) << "lane " << t << " post-barrier state lost";
    }
}

// memoryRoundTrip needs a request/response service loop standing in for
// memory_pipeline. Services mem_req_out/mem_resp_in with a trivial
// word-addressed map on this thread while compute_pipeline's own thread
// (started by CpFixture) issues the requests - not a cache, just enough to
// prove executeMemOp's per-lane request/response protocol (docs/hls/
// interfaces.md SS2.2's memory contract: one response per request, store
// response data don't-care).
TEST(ComputePipeline, MemoryRoundTrip) {
    static reg_t regs[MAX_WARPS_PER_CU][MAX_THREADS_PER_WARP][NUM_REGS_PER_THREAD];
    initRegs(regs[0]);

    auto program = riscv_gpgpu::kernels::memoryRoundTrip();
    static CpFixture cp;
    size_t program_len = loadProgram(cp.program, program);
    cp.start(regs, program_len);

    // 1 SW + 2 LW per active lane, 32 active lanes.
    const int kExpectedRequests = 3 * MAX_THREADS_PER_WARP;

    std::thread mem_service([&]() {
        std::map<uint64_t, uint32_t> mem;
        for (int i = 0; i < kExpectedRequests; ++i) {
            mem_req_t req = cp.mem_req_out.read();
            mem_resp_t resp;
            resp.cu_id = req.cu_id; resp.warp_id = req.warp_id; resp.lane_id = req.lane_id;
            if (req.is_write) {
                mem[static_cast<uint64_t>(req.address)] = static_cast<uint32_t>(req.write_data);
                resp.data = 0; // don't-care per the memory contract
            } else {
                auto it = mem.find(static_cast<uint64_t>(req.address));
                resp.data = (it != mem.end()) ? it->second : 0u;
            }
            cp.mem_resp_in.write(resp);
        }
    });

    warp_status_t st = cp.dispatchAndWait(0, 0, thread_mask_t(-1));
    mem_service.join();

    EXPECT_EQ(st.code, WarpStatusCode::COMPLETE);
    for (int t = 0; t < MAX_THREADS_PER_WARP; ++t) {
        EXPECT_EQ(static_cast<uint32_t>(regs[0][t][3]), static_cast<uint32_t>(t)) << "lane " << t << " first LW";
        EXPECT_EQ(static_cast<uint32_t>(regs[0][t][4]), static_cast<uint32_t>(t)) << "lane " << t << " second LW";
    }
}

// Added when upstream (commit 9c4dfea "GPGPU READY") added fpGemm()/
// conv2d3x3() to kernel_programs.h. Both are [DIRECT] kernels - straight-line
// chains of an opcode already covered elsewhere (VFFMADD by fpFmadd(),
// VFMADD's integer semantics by intSaxpy()'s VMUL/VADD siblings) - these
// tests exist for realistic-workload coverage (GEMM/convolution are the
// kind of kernel this accelerator actually exists for), not to validate a
// new mechanism.
TEST(ComputePipeline, FpGemm2x2TileK4) {
    static reg_t regs[MAX_WARPS_PER_CU][MAX_THREADS_PER_WARP][NUM_REGS_PER_THREAD];
    for (int t = 0; t < MAX_THREADS_PER_WARP; ++t)
        for (int r = 0; r < NUM_REGS_PER_THREAD; ++r) regs[0][t][r] = 0;

    // A = [[1,2,3,4],[5,6,7,8]], B = [[1,2],[3,4],[5,6],[7,8]] (B[k][col])
    // Golden reference: kernel_programs.h's fpGemm() doc comment / example.
    float A[2][4] = {{1, 2, 3, 4}, {5, 6, 7, 8}};
    float B[4][2] = {{1, 2}, {3, 4}, {5, 6}, {7, 8}};
    int rows[] = {0, 0, 1, 1};
    int cols[] = {0, 1, 0, 1};
    for (int t = 0; t < 4; ++t) {
        int row = rows[t], col = cols[t];
        regs[0][t][3]  = floatAsReg(A[row][0]);
        regs[0][t][4]  = floatAsReg(A[row][1]);
        regs[0][t][5]  = floatAsReg(A[row][2]);
        regs[0][t][6]  = floatAsReg(A[row][3]);
        regs[0][t][8]  = floatAsReg(B[0][col]);
        regs[0][t][9]  = floatAsReg(B[1][col]);
        regs[0][t][10] = floatAsReg(B[2][col]);
        regs[0][t][11] = floatAsReg(B[3][col]);
        regs[0][t][7]  = floatAsReg(0.0f);  // accumulator init
    }

    auto program = riscv_gpgpu::kernels::fpGemm();
    static CpFixture cp;
    size_t program_len = loadProgram(cp.program, program);
    cp.start(regs, program_len);

    EXPECT_EQ(cp.dispatchAndWait(0, 0, thread_mask_t(0xF)).code, WarpStatusCode::COMPLETE);
    EXPECT_FLOAT_EQ(regAsFloat(regs[0][0][7]),  50.0f) << "C[0][0]";
    EXPECT_FLOAT_EQ(regAsFloat(regs[0][1][7]),  60.0f) << "C[0][1]";
    EXPECT_FLOAT_EQ(regAsFloat(regs[0][2][7]), 114.0f) << "C[1][0]";
    EXPECT_FLOAT_EQ(regAsFloat(regs[0][3][7]), 140.0f) << "C[1][1]";
}

TEST(ComputePipeline, Conv2d3x3) {
    static reg_t regs[MAX_WARPS_PER_CU][MAX_THREADS_PER_WARP][NUM_REGS_PER_THREAD];
    for (int t = 0; t < MAX_THREADS_PER_WARP; ++t)
        for (int r = 0; r < NUM_REGS_PER_THREAD; ++r) regs[0][t][r] = 0;

    // Input (4x4) / filter (3x3) - golden reference: kernel_programs.h's
    // conv2d3x3() doc comment example.
    uint32_t neighborhoods[4][9] = {
        {1, 2, 3,  5,  6,  7,  9, 10, 11},
        {2, 3, 4,  6,  7,  8, 10, 11, 12},
        {5, 6, 7,  9, 10, 11, 13, 14, 15},
        {6, 7, 8, 10, 11, 12, 14, 15, 16}
    };
    uint32_t filter[9] = {1, 2, 1, 2, 4, 2, 1, 2, 1};
    for (int t = 0; t < 4; ++t) {
        for (int k = 0; k < 9; ++k) {
            regs[0][t][3  + k] = neighborhoods[t][k];
            regs[0][t][12 + k] = filter[k];
        }
        regs[0][t][21] = 0;
    }

    auto program = riscv_gpgpu::kernels::conv2d3x3();
    static CpFixture cp;
    size_t program_len = loadProgram(cp.program, program);
    cp.start(regs, program_len);

    EXPECT_EQ(cp.dispatchAndWait(0, 0, thread_mask_t(0xF)).code, WarpStatusCode::COMPLETE);
    EXPECT_EQ(static_cast<uint32_t>(regs[0][0][21]),  96u) << "out[0][0]";
    EXPECT_EQ(static_cast<uint32_t>(regs[0][1][21]), 112u) << "out[0][1]";
    EXPECT_EQ(static_cast<uint32_t>(regs[0][2][21]), 160u) << "out[1][0]";
    EXPECT_EQ(static_cast<uint32_t>(regs[0][3][21]), 176u) << "out[1][1]";
}

// Golden reference: kernel_programs.h's fpDivergentSaxpy() - a [DIRECT]
// kernel never exercised by ANY golden-model test (regression_test.cpp,
// benchmark_test.cpp - grep-verified, neither references it). No independent
// golden execution to cross-check against, unlike every other kernel in this
// file - expected values below are derived purely from tracing the
// instruction semantics, same rigor as everywhere else this session but
// flagged here since there's no second source confirming it.
//
// The kernel's own doc comment ("Even threads compute FP SAXPY; odd threads
// are masked") is WRONG for this exact instruction sequence, for sequential
// r0[t]=t thread indices - looks like a copy-paste from divergentOddEven()'s
// similar-sounding comment without re-deriving the actual bit pattern.
// The real mask condition is r7 = r0 & (r0+1), branching on r7==0:
//   r0 & (r0+1) == 0  iff  r0 == 0  or  r0's binary form is all 1s (2^k - 1)
// For r0[t]=t across 32 lanes that's t in {0,1,3,7,15,31} falling through -
// a sparse 6-of-32 pattern, not alternating even/odd. Verified by direct
// Python trace before writing this test, not assumed from the comment.
TEST(ComputePipeline, FpDivergentSaxpy) {
    static reg_t regs[MAX_WARPS_PER_CU][MAX_THREADS_PER_WARP][NUM_REGS_PER_THREAD];
    for (int t = 0; t < MAX_THREADS_PER_WARP; ++t) {
        for (int r = 0; r < NUM_REGS_PER_THREAD; ++r) regs[0][t][r] = 0;
        regs[0][t][0] = t;                          // thread_index (this kernel repurposes r0 - see its doc)
        regs[0][t][3] = floatAsReg(2.0f);            // alpha, uniform across lanes
        regs[0][t][4] = floatAsReg(static_cast<float>(t + 1));  // x[t], per-lane
        regs[0][t][5] = floatAsReg(10.0f);           // y, uniform across lanes
        // r6 (result) already 0 from the zero-fill above - must stay 0 for masked lanes.
    }

    auto program = riscv_gpgpu::kernels::fpDivergentSaxpy();
    static CpFixture cp;
    size_t program_len = loadProgram(cp.program, program);
    cp.start(regs, program_len);

    EXPECT_EQ(cp.dispatchAndWait(0, 0, thread_mask_t(-1)).code, WarpStatusCode::COMPLETE);

    bool falls_through[MAX_THREADS_PER_WARP];
    for (int t = 0; t < MAX_THREADS_PER_WARP; ++t) {
        falls_through[t] = ((static_cast<uint32_t>(t) & (static_cast<uint32_t>(t) + 1)) == 0);
    }
    for (int t = 0; t < MAX_THREADS_PER_WARP; ++t) {
        if (falls_through[t]) {
            float expect = 2.0f * static_cast<float>(t + 1) + 10.0f;
            EXPECT_FLOAT_EQ(regAsFloat(regs[0][t][6]), expect) << "lane " << t << " (fall-through)";
        } else {
            EXPECT_EQ(static_cast<uint32_t>(regs[0][t][6]), 0u) << "lane " << t << " (masked, should stay 0)";
        }
    }
}
