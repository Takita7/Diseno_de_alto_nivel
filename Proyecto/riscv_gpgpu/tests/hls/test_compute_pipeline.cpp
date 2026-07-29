// test_compute_pipeline.cpp - regression tests for compute_pipeline (T022)
//
// Drives compute_pipeline with the SAME kernel programs used against the
// golden SystemC model (models/systemc/src/common/kernel_programs.h),
// translated instruction-by-instruction into riscv_gpgpu_hls::Instruction.
// Expected register values below are derived by hand-tracing the same
// kernels through ComputeUnit's documented semantics (kernel_programs.h's
// own per-kernel comments), not independently invented.
//
// Kernels requiring memory (memoryRoundTrip) are serviced by a worker
// thread acting as a temporary stand-in for memory_pipeline (T023, not yet
// implemented) - hls::stream in this Vitis HLS version is mutex+condvar
// protected specifically to support this producer/consumer-thread csim
// pattern (see hls_stream.h's stream_entity::read()/write()), so this is
// the intended usage, not a workaround.

#include <gtest/gtest.h>
#include <hls_stream.h>
#include <thread>
#include <cstdint>

#include "compute_unit/compute_pipeline.h"
#include "../../models/systemc/src/common/kernel_programs.h"

using namespace riscv_gpgpu_hls;

namespace {

Instruction toHls(const riscv_gpgpu::Instruction& gi) {
    Instruction hi;
    hi.pc         = gi.pc;
    hi.opcode     = static_cast<Opcode>(gi.opcode);
    hi.rs1        = gi.rs1;
    hi.rs2        = gi.rs2;
    hi.rd         = gi.rd;
    hi.imm        = gi.imm;
    hi.is_vector  = gi.is_vector;
    hi.is_memory  = gi.is_memory;
    hi.is_branch  = gi.is_branch;
    return hi;
}

void feedProgram(hls::stream<instr_word_t>& instr_in,
                 const std::vector<riscv_gpgpu::Instruction>& program,
                 size_t begin, size_t count) {
    for (size_t i = 0; i < count; ++i) instr_in.write(toHls(program[begin + i]));
}

// Standard per-lane register convention (matches kernel_programs.h's header
// comment / GPGPUTop::buildWarpContext): r0=0, r1=global_tid, r2=unique addr,
// r3=local_warp_id (added upstream in commit 9c4dfea "GPGPU READY" -
// buildWarpContext() now sets regs[t][3] = warp_id - kernel_start_warp_id_;
// see kernels::parallelReduction()'s use of it). Single-warp tests want
// local_warp_id=0, which the zero-fill loop already gives for free - the
// explicit assignment below exists so the convention is visible here rather
// than relying on an incidental zero-fill, and so multi-warp tests
// (test_pipeline_integration.cpp's ParallelReduction) have an obvious line
// to override.
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

}  // namespace

TEST(ComputePipeline, IntSaxpy) {
    static reg_t regs[MAX_THREADS_PER_WARP][NUM_REGS_PER_THREAD];
    initRegs(regs);

    auto program = riscv_gpgpu::kernels::intSaxpy(/*alpha=*/2, /*y=*/10);
    hls::stream<instr_word_t>  instr_in("instr_in");
    hls::stream<mem_req_t>     mem_req_out("mem_req_out");
    hls::stream<mem_resp_t>    mem_resp_in("mem_resp_in");
    hls::stream<warp_status_t> status_out("status_out");
    feedProgram(instr_in, program, 0, program.size());

    compute_pipeline(/*cu_id=*/0, /*warp_id=*/0, thread_mask_t(-1),
                      program.size(), instr_in, regs,
                      mem_req_out, mem_resp_in, status_out);

    warp_status_t st = status_out.read();
    EXPECT_EQ(st.code, WarpStatusCode::COMPLETE);

    // Golden: r6[t] = (global_tid + 1) * alpha + y = (t+1)*2 + 10
    for (int t = 0; t < MAX_THREADS_PER_WARP; ++t) {
        uint32_t expect = (t + 1) * 2 + 10;
        EXPECT_EQ(static_cast<uint32_t>(regs[t][6]), expect) << "lane " << t;
    }
}

TEST(ComputePipeline, FpUniformSaxpy) {
    static reg_t regs[MAX_THREADS_PER_WARP][NUM_REGS_PER_THREAD];
    initRegs(regs);

    auto program = riscv_gpgpu::kernels::fpUniformSaxpy(2.0f, 3.0f, 1.0f);
    hls::stream<instr_word_t>  instr_in("instr_in");
    hls::stream<mem_req_t>     mem_req_out("mem_req_out");
    hls::stream<mem_resp_t>    mem_resp_in("mem_resp_in");
    hls::stream<warp_status_t> status_out("status_out");
    feedProgram(instr_in, program, 0, program.size());

    compute_pipeline(0, 0, thread_mask_t(-1), program.size(),
                      instr_in, regs, mem_req_out, mem_resp_in, status_out);

    EXPECT_EQ(status_out.read().code, WarpStatusCode::COMPLETE);

    // Golden: r6 = alpha*x + y = 2*3+1 = 7.0, same for every lane (no
    // per-thread dependency in this kernel).
    for (int t = 0; t < MAX_THREADS_PER_WARP; ++t) {
        EXPECT_FLOAT_EQ(regAsFloat(regs[t][6]), 7.0f) << "lane " << t;
    }
}

TEST(ComputePipeline, DivergentOddEven) {
    static reg_t regs[MAX_THREADS_PER_WARP][NUM_REGS_PER_THREAD];
    initRegs(regs);

    auto program = riscv_gpgpu::kernels::divergentOddEven();
    hls::stream<instr_word_t>  instr_in("instr_in");
    hls::stream<mem_req_t>     mem_req_out("mem_req_out");
    hls::stream<mem_resp_t>    mem_resp_in("mem_resp_in");
    hls::stream<warp_status_t> status_out("status_out");
    feedProgram(instr_in, program, 0, program.size());

    compute_pipeline(0, 0, thread_mask_t(-1), program.size(),
                      instr_in, regs, mem_req_out, mem_resp_in, status_out);

    EXPECT_EQ(status_out.read().code, WarpStatusCode::COMPLETE);

    // Golden: r5[t] = 100 for even threads (fall through VBRANCH), 0 for odd
    // (masked off, never executes the ADDI that sets r5).
    for (int t = 0; t < MAX_THREADS_PER_WARP; ++t) {
        uint32_t expect = (t % 2 == 0) ? 100u : 0u;
        EXPECT_EQ(static_cast<uint32_t>(regs[t][5]), expect) << "lane " << t;
    }
}

// Exercises BARRIER stall/resume without needing memory_pipeline: a minimal
// program (not verbatim kernel_programs.h - barrierRoundTrip's SW/LW would
// need a memory responder, tested separately below) that isolates the
// barrier mechanism itself: does compute_pipeline (a) report
// STALLED_AT_BARRIER with the right bid and stop exactly after the BARRIER
// instruction, and (b) correctly resume from a second invocation sharing the
// same regs[][], per the host contract in docs/hls/interfaces.md SS2.2.
TEST(ComputePipeline, BarrierStallThenResume) {
    static reg_t regs[MAX_THREADS_PER_WARP][NUM_REGS_PER_THREAD];
    initRegs(regs);

    std::vector<riscv_gpgpu::Instruction> pre_barrier = {
        riscv_gpgpu::makeInstr(riscv_gpgpu::Opcode::ADDI, /*rd=*/7, /*rs1=*/0, /*rs2=*/0, /*imm=*/42),
        riscv_gpgpu::makeInstr(riscv_gpgpu::Opcode::BARRIER, 0, 0, 0, /*barrier_id=*/5),
    };
    std::vector<riscv_gpgpu::Instruction> post_barrier = {
        riscv_gpgpu::makeInstr(riscv_gpgpu::Opcode::ADDI, /*rd=*/8, /*rs1=*/7, /*rs2=*/0, /*imm=*/1),
        riscv_gpgpu::makeInstr(riscv_gpgpu::Opcode::HALT),
    };

    hls::stream<instr_word_t>  instr_in("instr_in");
    hls::stream<mem_req_t>     mem_req_out("mem_req_out");
    hls::stream<mem_resp_t>    mem_resp_in("mem_resp_in");
    hls::stream<warp_status_t> status_out("status_out");
    feedProgram(instr_in, pre_barrier, 0, pre_barrier.size());

    compute_pipeline(0, 0, thread_mask_t(-1), pre_barrier.size(),
                      instr_in, regs, mem_req_out, mem_resp_in, status_out);

    warp_status_t st1 = status_out.read();
    ASSERT_EQ(st1.code, WarpStatusCode::STALLED_AT_BARRIER);
    EXPECT_EQ(st1.barrier_id, 5);
    for (int t = 0; t < MAX_THREADS_PER_WARP; ++t) {
        EXPECT_EQ(static_cast<uint32_t>(regs[t][7]), 42u) << "lane " << t << " pre-barrier write lost";
    }

    // Resume: same regs[][], host feeds only the post-barrier instructions,
    // mask re-widened to full per SS2.2's "after a barrier all threads are
    // synchronised" contract (mirrors compute_unit.cpp's initializeWarp()
    // call on every invocation, including resumes).
    feedProgram(instr_in, post_barrier, 0, post_barrier.size());
    compute_pipeline(0, 0, thread_mask_t(-1), post_barrier.size(),
                      instr_in, regs, mem_req_out, mem_resp_in, status_out);

    warp_status_t st2 = status_out.read();
    EXPECT_EQ(st2.code, WarpStatusCode::COMPLETE);
    for (int t = 0; t < MAX_THREADS_PER_WARP; ++t) {
        EXPECT_EQ(static_cast<uint32_t>(regs[t][8]), 43u) << "lane " << t << " post-barrier state lost";
    }
}

// memoryRoundTrip needs a request/response service loop standing in for
// memory_pipeline. Runs compute_pipeline on a worker thread while this
// thread services mem_req_out/mem_resp_in with a trivial word-addressed
// map - not a cache, just enough to prove executeMemOp's per-lane
// request/response protocol (docs/hls/interfaces.md SS2.2's memory
// contract: one response per request, store response data don't-care).
TEST(ComputePipeline, MemoryRoundTrip) {
    static reg_t regs[MAX_THREADS_PER_WARP][NUM_REGS_PER_THREAD];
    initRegs(regs);

    auto program = riscv_gpgpu::kernels::memoryRoundTrip();
    hls::stream<instr_word_t>  instr_in("instr_in");
    hls::stream<mem_req_t>     mem_req_out("mem_req_out");
    hls::stream<mem_resp_t>    mem_resp_in("mem_resp_in");
    hls::stream<warp_status_t> status_out("status_out");
    feedProgram(instr_in, program, 0, program.size());

    // 1 SW + 2 LW per active lane, 32 active lanes.
    const int kExpectedRequests = 3 * MAX_THREADS_PER_WARP;

    std::thread compute_thread([&]() {
        compute_pipeline(0, 0, thread_mask_t(-1), program.size(),
                          instr_in, regs, mem_req_out, mem_resp_in, status_out);
    });

    std::map<uint64_t, uint32_t> mem;
    for (int i = 0; i < kExpectedRequests; ++i) {
        mem_req_t req = mem_req_out.read();
        mem_resp_t resp;
        resp.cu_id = req.cu_id; resp.warp_id = req.warp_id; resp.lane_id = req.lane_id;
        if (req.is_write) {
            mem[static_cast<uint64_t>(req.address)] = static_cast<uint32_t>(req.write_data);
            resp.data = 0; // don't-care per the memory contract
        } else {
            auto it = mem.find(static_cast<uint64_t>(req.address));
            resp.data = (it != mem.end()) ? it->second : 0u;
        }
        mem_resp_in.write(resp);
    }
    compute_thread.join();

    EXPECT_EQ(status_out.read().code, WarpStatusCode::COMPLETE);
    for (int t = 0; t < MAX_THREADS_PER_WARP; ++t) {
        EXPECT_EQ(static_cast<uint32_t>(regs[t][3]), static_cast<uint32_t>(t)) << "lane " << t << " first LW";
        EXPECT_EQ(static_cast<uint32_t>(regs[t][4]), static_cast<uint32_t>(t)) << "lane " << t << " second LW";
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
    static reg_t regs[MAX_THREADS_PER_WARP][NUM_REGS_PER_THREAD];
    for (int t = 0; t < MAX_THREADS_PER_WARP; ++t)
        for (int r = 0; r < NUM_REGS_PER_THREAD; ++r) regs[t][r] = 0;

    // A = [[1,2,3,4],[5,6,7,8]], B = [[1,2],[3,4],[5,6],[7,8]] (B[k][col])
    // Golden reference: kernel_programs.h's fpGemm() doc comment / example.
    float A[2][4] = {{1, 2, 3, 4}, {5, 6, 7, 8}};
    float B[4][2] = {{1, 2}, {3, 4}, {5, 6}, {7, 8}};
    int rows[] = {0, 0, 1, 1};
    int cols[] = {0, 1, 0, 1};
    for (int t = 0; t < 4; ++t) {
        int row = rows[t], col = cols[t];
        regs[t][3]  = floatAsReg(A[row][0]);
        regs[t][4]  = floatAsReg(A[row][1]);
        regs[t][5]  = floatAsReg(A[row][2]);
        regs[t][6]  = floatAsReg(A[row][3]);
        regs[t][8]  = floatAsReg(B[0][col]);
        regs[t][9]  = floatAsReg(B[1][col]);
        regs[t][10] = floatAsReg(B[2][col]);
        regs[t][11] = floatAsReg(B[3][col]);
        regs[t][7]  = floatAsReg(0.0f);  // accumulator init
    }

    auto program = riscv_gpgpu::kernels::fpGemm();
    hls::stream<instr_word_t>  instr_in("instr_in");
    hls::stream<mem_req_t>     mem_req_out("mem_req_out");
    hls::stream<mem_resp_t>    mem_resp_in("mem_resp_in");
    hls::stream<warp_status_t> status_out("status_out");
    feedProgram(instr_in, program, 0, program.size());

    compute_pipeline(0, 0, thread_mask_t(0xF), program.size(),
                      instr_in, regs, mem_req_out, mem_resp_in, status_out);

    EXPECT_EQ(status_out.read().code, WarpStatusCode::COMPLETE);
    EXPECT_FLOAT_EQ(regAsFloat(regs[0][7]),  50.0f) << "C[0][0]";
    EXPECT_FLOAT_EQ(regAsFloat(regs[1][7]),  60.0f) << "C[0][1]";
    EXPECT_FLOAT_EQ(regAsFloat(regs[2][7]), 114.0f) << "C[1][0]";
    EXPECT_FLOAT_EQ(regAsFloat(regs[3][7]), 140.0f) << "C[1][1]";
}

TEST(ComputePipeline, Conv2d3x3) {
    static reg_t regs[MAX_THREADS_PER_WARP][NUM_REGS_PER_THREAD];
    for (int t = 0; t < MAX_THREADS_PER_WARP; ++t)
        for (int r = 0; r < NUM_REGS_PER_THREAD; ++r) regs[t][r] = 0;

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
            regs[t][3  + k] = neighborhoods[t][k];
            regs[t][12 + k] = filter[k];
        }
        regs[t][21] = 0;
    }

    auto program = riscv_gpgpu::kernels::conv2d3x3();
    hls::stream<instr_word_t>  instr_in("instr_in");
    hls::stream<mem_req_t>     mem_req_out("mem_req_out");
    hls::stream<mem_resp_t>    mem_resp_in("mem_resp_in");
    hls::stream<warp_status_t> status_out("status_out");
    feedProgram(instr_in, program, 0, program.size());

    compute_pipeline(0, 0, thread_mask_t(0xF), program.size(),
                      instr_in, regs, mem_req_out, mem_resp_in, status_out);

    EXPECT_EQ(status_out.read().code, WarpStatusCode::COMPLETE);
    EXPECT_EQ(static_cast<uint32_t>(regs[0][21]),  96u) << "out[0][0]";
    EXPECT_EQ(static_cast<uint32_t>(regs[1][21]), 112u) << "out[0][1]";
    EXPECT_EQ(static_cast<uint32_t>(regs[2][21]), 160u) << "out[1][0]";
    EXPECT_EQ(static_cast<uint32_t>(regs[3][21]), 176u) << "out[1][1]";
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
    static reg_t regs[MAX_THREADS_PER_WARP][NUM_REGS_PER_THREAD];
    for (int t = 0; t < MAX_THREADS_PER_WARP; ++t) {
        for (int r = 0; r < NUM_REGS_PER_THREAD; ++r) regs[t][r] = 0;
        regs[t][0] = t;                          // thread_index (this kernel repurposes r0 - see its doc)
        regs[t][3] = floatAsReg(2.0f);            // alpha, uniform across lanes
        regs[t][4] = floatAsReg(static_cast<float>(t + 1));  // x[t], per-lane
        regs[t][5] = floatAsReg(10.0f);           // y, uniform across lanes
        // r6 (result) already 0 from the zero-fill above - must stay 0 for masked lanes.
    }

    auto program = riscv_gpgpu::kernels::fpDivergentSaxpy();
    hls::stream<instr_word_t>  instr_in("instr_in");
    hls::stream<mem_req_t>     mem_req_out("mem_req_out");
    hls::stream<mem_resp_t>    mem_resp_in("mem_resp_in");
    hls::stream<warp_status_t> status_out("status_out");
    feedProgram(instr_in, program, 0, program.size());

    compute_pipeline(0, 0, thread_mask_t(-1), program.size(),
                      instr_in, regs, mem_req_out, mem_resp_in, status_out);

    EXPECT_EQ(status_out.read().code, WarpStatusCode::COMPLETE);

    bool falls_through[MAX_THREADS_PER_WARP];
    for (int t = 0; t < MAX_THREADS_PER_WARP; ++t) {
        falls_through[t] = ((static_cast<uint32_t>(t) & (static_cast<uint32_t>(t) + 1)) == 0);
    }
    for (int t = 0; t < MAX_THREADS_PER_WARP; ++t) {
        if (falls_through[t]) {
            float expect = 2.0f * static_cast<float>(t + 1) + 10.0f;
            EXPECT_FLOAT_EQ(regAsFloat(regs[t][6]), expect) << "lane " << t << " (fall-through)";
        } else {
            EXPECT_EQ(static_cast<uint32_t>(regs[t][6]), 0u) << "lane " << t << " (masked, should stay 0)";
        }
    }
}
