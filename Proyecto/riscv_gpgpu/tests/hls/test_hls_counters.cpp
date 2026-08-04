// test_hls_counters.cpp - T077: observability counter parity tests
//
// Verifies that warp_status_t::instr_count is:
//   (a) non-zero for non-trivial kernels,
//   (b) correctly accumulated across barrier stalls,
//   (c) monotonically larger for longer kernels.
//
// Memory-side counters (l1_hits, l2_hits, mem_transactions) are verified
// via MemorySubsystem directly (it already exposes getL1CacheHits() etc.)
// and mapped into perf_counters_t (hls_types.h §T077).
//
// CpFixture reuse: identical to test_compute_pipeline.cpp — both the struct
// definition and the must-be-static rule (detach'd thread holds `this`).

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

size_t loadProgram(instr_word_t program[MAX_PROGRAM_LEN],
                    const std::vector<riscv_gpgpu::Instruction>& src) {
    size_t out_i = 0;
    for (const auto& gi : src) {
        raw_instr_t words[2];
        int n = encodeInstructionExpanded(static_cast<Opcode>(gi.opcode),
                                           gi.rd, gi.rs1, gi.rs2, gi.imm, words);
        for (int k = 0; k < n; ++k) program[out_i++] = words[k];
    }
    return out_i;
}

void initRegs(reg_t regs[MAX_THREADS_PER_WARP][NUM_REGS_PER_THREAD],
              uint32_t global_tid_offset = 0, uint32_t local_warp_id = 0) {
    for (int t = 0; t < MAX_THREADS_PER_WARP; ++t) {
        for (int r = 0; r < NUM_REGS_PER_THREAD; ++r) regs[t][r] = 0;
        regs[t][1] = global_tid_offset + t;
        regs[t][2] = 0x1000 + (global_tid_offset + t) * 4;
        regs[t][3] = local_warp_id;
    }
}

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

// intSaxpy has 5 real instructions (ADDI×3 + VMUL + VADD); HALT is not counted.
// fpUniformSaxpy may expand to 6+ words due to LUI+ADDI expansion (SS13.12)
// but still retires exactly 5 logical ops. Use intSaxpy for an exact count.
TEST(HlsCounters, IntSaxpyInstrCountEqualsProgram) {
    static reg_t regs[MAX_WARPS_PER_CU][MAX_THREADS_PER_WARP][NUM_REGS_PER_THREAD];
    initRegs(regs[0]);

    auto prog = riscv_gpgpu::kernels::intSaxpy(2, 10);
    static CpFixture cp;
    size_t program_len = loadProgram(cp.program, prog);
    cp.start(regs, program_len);

    warp_status_t st = cp.dispatchAndWait(0, 0, thread_mask_t(-1));
    EXPECT_EQ(st.code, WarpStatusCode::COMPLETE);
    // intSaxpy: ADDI r3,0,alpha  ADDI r4,r1,1  ADDI r5,0,y  VMUL r6,r4,r3  VADD r6,r6,r5
    EXPECT_EQ(static_cast<uint32_t>(st.instr_count), 5u);
}

// A kernel with a barrier must report instr_count > 0 on the pre-barrier
// dispatch (STALLED) and additional instructions on the post-barrier dispatch
// (COMPLETE). The total across both dispatches must equal the non-HALT/
// non-HALT program length.
TEST(HlsCounters, BarrierKernelAccumulatesAcrossStall) {
    // barrierRoundTrip: SW, BARRIER, LW, HALT — 3 real ops (SW + BARRIER + LW)
    static reg_t regs[MAX_WARPS_PER_CU][MAX_THREADS_PER_WARP][NUM_REGS_PER_THREAD];
    initRegs(regs[0]);

    auto prog = riscv_gpgpu::kernels::barrierRoundTrip(0);
    static CpFixture cp;
    // Provide a trivial memory service thread so SW/LW don't block the pipeline.
    static MemorySubsystem mem;
    static std::vector<ap_uint<32>> barrier_backing(65536, 0);
    std::thread mem_th([&]() {
        while (true) {
            mem_req_t req = cp.mem_req_out.read();
            mem_resp_t resp = mem.handleRequest(req, barrier_backing.data());
            cp.mem_resp_in.write(resp);
        }
    });
    mem_th.detach();

    size_t program_len = loadProgram(cp.program, prog);
    cp.start(regs, program_len);

    // Pre-barrier dispatch: executes SW then hits BARRIER
    warp_status_t pre = cp.dispatchAndWait(0, 0, thread_mask_t(-1));
    EXPECT_EQ(pre.code, WarpStatusCode::STALLED_AT_BARRIER);
    EXPECT_GT(static_cast<uint32_t>(pre.instr_count), 0u);  // SW was retired

    // Post-barrier dispatch: executes LW then HALT
    warp_status_t post = cp.dispatchAndWait(0, 0, thread_mask_t(-1), pre.resume_pc);
    EXPECT_EQ(post.code, WarpStatusCode::COMPLETE);
    EXPECT_GT(static_cast<uint32_t>(post.instr_count), 0u);  // LW was retired

    // Total across both dispatches == 3 (SW + BARRIER + LW)
    uint32_t total = static_cast<uint32_t>(pre.instr_count)
                   + static_cast<uint32_t>(post.instr_count);
    EXPECT_EQ(total, 3u);
}

// Monotonicity: a longer program produces a higher instr_count than a shorter one.
// intSaxpy (5 ops) > ADDI+HALT (1 op).
TEST(HlsCounters, LongerKernelHasHigherInstrCount) {
    static reg_t regs_long[MAX_WARPS_PER_CU][MAX_THREADS_PER_WARP][NUM_REGS_PER_THREAD];
    static reg_t regs_short[MAX_WARPS_PER_CU][MAX_THREADS_PER_WARP][NUM_REGS_PER_THREAD];
    initRegs(regs_long[0]);
    initRegs(regs_short[0]);

    static CpFixture cp_long;
    {
        auto prog = riscv_gpgpu::kernels::intSaxpy(2, 10);
        size_t n = loadProgram(cp_long.program, prog);
        cp_long.start(regs_long, n);
    }

    static CpFixture cp_short;
    {
        // Minimal 1-instruction kernel: ADDI r6, r0, 42
        raw_instr_t w0 = encodeInstruction(Opcode::ADDI, 6, 0, 0, 42);
        raw_instr_t w1 = encodeInstruction(Opcode::HALT);
        cp_short.program[0] = w0;
        cp_short.program[1] = w1;
        cp_short.start(regs_short, 2);
    }

    warp_status_t st_long  = cp_long.dispatchAndWait(0, 0, thread_mask_t(-1));
    warp_status_t st_short = cp_short.dispatchAndWait(0, 0, thread_mask_t(-1));

    EXPECT_EQ(st_long.code,  WarpStatusCode::COMPLETE);
    EXPECT_EQ(st_short.code, WarpStatusCode::COMPLETE);
    EXPECT_GT(static_cast<uint32_t>(st_long.instr_count),
              static_cast<uint32_t>(st_short.instr_count));
}

// MemorySubsystem counter monotonicity for a known kernel (memoryRoundTrip).
// Expected: after 1 SW + 2 LW per thread (L1 miss then hit), l1_hits > 0.
TEST(HlsCounters, MemorySubsystemCountersMonotonic) {
    MemorySubsystem mem;
    std::vector<ap_uint<32>> backing(65536, 0);

    auto mkReq = [](addr_t addr, bool is_write, reg_t data = 0) {
        mem_req_t r; r.cu_id = 0; r.warp_id = 0; r.lane_id = 0;
        r.address = addr; r.is_write = is_write; r.write_data = data;
        return r;
    };

    // Write some values to the backing store
    constexpr uint32_t base = 0x2000;
    for (int t = 0; t < 4; ++t) {
        uint32_t addr = base + t * 4;
        mem.handleRequest(mkReq(addr, true, reg_t(t + 1)), backing.data());
    }
    uint64_t l1h_before = mem.getL1CacheHits();
    for (int t = 0; t < 4; ++t) {
        uint32_t addr = base + t * 4;
        mem.handleRequest(mkReq(addr, false), backing.data());  // L1 miss → fills from L2
        mem.handleRequest(mkReq(addr, false), backing.data());  // L1 hit
    }
    EXPECT_GT(mem.getL1CacheHits(), l1h_before);
    EXPECT_GT(mem.getL2CacheHits() + mem.getL2CacheMisses(), uint64_t(0));
}

// conv2d3x3 runs more instructions than intSaxpy — both reported by instr_count.
TEST(HlsCounters, Conv2d3x3HasMoreInstrsThanIntSaxpy) {
    static reg_t regs_saxpy[MAX_WARPS_PER_CU][MAX_THREADS_PER_WARP][NUM_REGS_PER_THREAD];
    static reg_t regs_conv[MAX_WARPS_PER_CU][MAX_THREADS_PER_WARP][NUM_REGS_PER_THREAD];
    initRegs(regs_saxpy[0]);
    initRegs(regs_conv[0]);

    static CpFixture cp_saxpy;
    {
        auto prog = riscv_gpgpu::kernels::intSaxpy(2, 10);
        size_t n = loadProgram(cp_saxpy.program, prog);
        cp_saxpy.start(regs_saxpy, n);
    }

    // conv2d3x3 uses LW/SW; provide a trivial memory service
    static hls::stream<mem_req_t>  conv_req{"conv_req"};
    static hls::stream<mem_resp_t> conv_resp{"conv_resp"};
    static hls::stream<warp_dispatch_t> conv_dispatch{"conv_dispatch"};
    static hls::stream<warp_status_t>   conv_status{"conv_status"};
    static instr_word_t conv_program[MAX_PROGRAM_LEN];

    auto prog_conv = riscv_gpgpu::kernels::conv2d3x3();
    size_t n_conv = loadProgram(conv_program, prog_conv);

    static MemorySubsystem conv_mem;
    static std::vector<ap_uint<32>> conv_backing(65536, 0);
    std::thread mem_th([&]() {
        while (true) {
            mem_req_t req = conv_req.read();
            mem_resp_t resp = conv_mem.handleRequest(req, conv_backing.data());
            conv_resp.write(resp);
        }
    });
    mem_th.detach();

    std::thread conv_th([&]() {
        compute_pipeline(0, conv_dispatch, conv_program, static_cast<uint32_t>(n_conv),
                          regs_conv, nullptr, conv_req, conv_resp, conv_status);
    });
    conv_th.detach();

    warp_dispatch_t d;
    d.slot_id = 0; d.warp_id = 0;
    d.active_mask_init = thread_mask_t(-1); d.resume_pc = 0;
    conv_dispatch.write(d);
    warp_status_t st_conv = conv_status.read();

    warp_status_t st_saxpy = cp_saxpy.dispatchAndWait(0, 0, thread_mask_t(-1));

    EXPECT_EQ(st_conv.code,  WarpStatusCode::COMPLETE);
    EXPECT_EQ(st_saxpy.code, WarpStatusCode::COMPLETE);
    EXPECT_GT(static_cast<uint32_t>(st_conv.instr_count),
              static_cast<uint32_t>(st_saxpy.instr_count))
        << "conv2d3x3 (" << st_conv.instr_count
        << ") should exceed intSaxpy (" << st_saxpy.instr_count << ")";
}
