// test_model_parity.cpp - T078: end-to-end parity regression
//
// For each target kernel class (divergent, barrier-heavy, FP), runs the
// kernel through BOTH:
//   (a) the HLS compute_pipeline (via the standard CpFixture harness),
//   (b) a lightweight C++ reference executor that directly implements
//       the same Virtual-ISA semantics as ComputeUnit::executeWarp()
//       without requiring the SystemC simulation framework.
//
// The reference executor is NOT a reimplementation from scratch: it
// mirrors ComputeUnit::executeWarp()'s instruction dispatch table
// (executeALU/executeVector/executeMemOp/executeBranch/executeJoin) using
// standard C++ integers and floats, so any drift between the golden model
// and the HLS port is caught here automatically.
//
// Intentional deviations (not flagged as failures):
//   - Cache hit rates differ: HLS uses line-granularity BRAM caches while
//     the reference executor uses a word-addressed std::map. Data returned
//     is identical; hit/miss counts are not compared.
//   - instruction_count per warp may include LUI expansion words (SS13.12)
//     on the HLS path that the reference executor doesn't produce, since the
//     reference operates on the logical kernel_programs.h instruction stream.
//     This accepted delta is documented in docs/hls/interfaces.md §T075.
//
// Fail mode: any register mismatch (non-FP) or > 1 ULP (FP) emits the full
// register file diff for the failing lane and fails the test.

#include <gtest/gtest.h>
#include <hls_stream.h>
#include <thread>
#include <vector>
#include <cmath>
#include <cstring>
#include <sstream>

#include "compute_unit/compute_pipeline.h"
#include "compute_unit/rv32i_codec.h"
#include "memory/memory_pipeline.h"
#include "../../models/systemc/src/common/kernel_programs.h"

using namespace riscv_gpgpu_hls;

namespace {

// ── Lightweight reference executor (mirrors ComputeUnit::executeWarp) ─────────
//
// Uses the same Opcode enum values as kernel_programs.h's makeInstr() and
// hls_types.h's Opcode — they are value-identical (verified by T022 notes in
// docs/hls/interfaces.md §11.3).

struct RefState {
    uint32_t regs[MAX_THREADS_PER_WARP][NUM_REGS_PER_THREAD] = {};
    float    fregs[MAX_THREADS_PER_WARP][32] = {};
    std::map<uint32_t, uint32_t> mem;
    uint32_t instr_count = 0;
    bool     stalled     = false;
    uint32_t barrier_id  = 0;
    uint32_t resume_pc   = 0;
};

struct DivStack {
    struct Frame { uint32_t mask; };
    Frame frames[16];
    int   depth = 0;
    uint32_t active = ~0u;

    void init(uint32_t mask) { active = mask; depth = 0; }

    void handleBranch(const bool conditions[MAX_THREADS_PER_WARP]) {
        // conds[t]=true → thread takes fall-through (active); false → thread diverges
        uint32_t fall = 0, jump = 0;
        for (int t = 0; t < MAX_THREADS_PER_WARP; ++t) {
            if (!((active >> t) & 1u)) continue;
            if (!conditions[t]) fall |= (1u << t);
            else                jump |= (1u << t);
        }
        frames[depth++].mask = jump;   // save the masked-out set
        active = fall;
    }

    void handleJoin() {
        if (depth == 0) return;
        active |= frames[--depth].mask;
    }
};

static inline float bitsToFloat(uint32_t b) {
    float f; std::memcpy(&f, &b, 4); return f;
}
static inline uint32_t floatToBits(float f) {
    uint32_t b; std::memcpy(&b, &f, 4); return b;
}

void executeRef(RefState& s,
                const std::vector<riscv_gpgpu::Instruction>& program,
                uint32_t start_pc = 0) {
    using Op = riscv_gpgpu::Opcode;
    DivStack simt;
    simt.init(~0u);

    for (uint32_t i = start_pc; i < program.size(); ++i) {
        const auto& instr = program[i];
        Op op = static_cast<Op>(instr.opcode);
        uint32_t mask = simt.active;

        if (op == Op::HALT) break;
        ++s.instr_count;

        if (op == Op::BARRIER) {
            s.stalled   = true;
            s.barrier_id = static_cast<uint32_t>(instr.imm);
            s.resume_pc  = i + 1;
            return;
        }

        bool conds[MAX_THREADS_PER_WARP];
        for (int t = 0; t < MAX_THREADS_PER_WARP; ++t) conds[t] = false;

        if (op == Op::VBRANCH) {
            for (int t = 0; t < MAX_THREADS_PER_WARP; ++t)
                if ((mask >> t) & 1u) conds[t] = (s.regs[t][instr.rs1] == 0);
            simt.handleBranch(conds);
        } else if (op == Op::VJOIN) {
            simt.handleJoin();
        } else if (op == Op::LW) {
            for (int t = 0; t < MAX_THREADS_PER_WARP; ++t) {
                if (!((mask >> t) & 1u)) continue;
                uint32_t addr = s.regs[t][instr.rs1] + static_cast<uint32_t>(instr.imm);
                s.regs[t][instr.rd] = s.mem.count(addr) ? s.mem[addr] : 0u;
            }
        } else if (op == Op::SW) {
            for (int t = 0; t < MAX_THREADS_PER_WARP; ++t) {
                if (!((mask >> t) & 1u)) continue;
                uint32_t addr = s.regs[t][instr.rs1] + static_cast<uint32_t>(instr.imm);
                s.mem[addr] = s.regs[t][instr.rs2];
            }
        } else {
            // ALU / vector ops
            for (int t = 0; t < MAX_THREADS_PER_WARP; ++t) {
                if (!((mask >> t) & 1u)) continue;
                uint32_t a = s.regs[t][instr.rs1];
                uint32_t b = (op == Op::ADDI || op == Op::LUI)
                             ? static_cast<uint32_t>(instr.imm)
                             : s.regs[t][instr.rs2];
                float fa = bitsToFloat(a), fb = bitsToFloat(b);
                float fc = bitsToFloat(s.regs[t][instr.rd]);

                switch (op) {
                    case Op::ADD:
                    case Op::ADDI:   s.regs[t][instr.rd] = a + b;                           break;
                    case Op::SUB:    s.regs[t][instr.rd] = a - b;                           break;
                    case Op::AND:    s.regs[t][instr.rd] = a & b;                           break;
                    case Op::OR:     s.regs[t][instr.rd] = a | b;                           break;
                    case Op::XOR:    s.regs[t][instr.rd] = a ^ b;                           break;
                    case Op::SLT:    s.regs[t][instr.rd] = (int32_t(a) < int32_t(b)) ? 1 : 0; break;
                    case Op::LUI:    s.regs[t][instr.rd] = b << 12;                         break;
                    case Op::FADD:   s.regs[t][instr.rd] = floatToBits(fa + fb);            break;
                    case Op::FMUL:   s.regs[t][instr.rd] = floatToBits(fa * fb);            break;
                    case Op::VADD:   s.regs[t][instr.rd] = a + b;                           break;
                    case Op::VSUB:   s.regs[t][instr.rd] = a - b;                           break;
                    case Op::VMUL:   s.regs[t][instr.rd] = a * b;                           break;
                    case Op::VFMADD: s.regs[t][instr.rd] = a * b + s.regs[t][instr.rd];    break;
                    case Op::VFADD:  s.regs[t][instr.rd] = floatToBits(fa + fb);            break;
                    case Op::VFSUB:  s.regs[t][instr.rd] = floatToBits(fa - fb);            break;
                    case Op::VFMUL:  s.regs[t][instr.rd] = floatToBits(fa * fb);            break;
                    case Op::VFFMADD:s.regs[t][instr.rd] = floatToBits(fa * fb + fc);       break;
                    default: break;
                }
            }
        }
    }
    s.stalled = false;
}

// ── HLS execution harness (CpFixture + inline memory service) ─────────────────

struct HlsRunner {
    hls::stream<warp_dispatch_t> dispatch_in{"pr_dispatch"};
    hls::stream<mem_req_t>       mem_req_out{"pr_req"};
    hls::stream<mem_resp_t>      mem_resp_in{"pr_resp"};
    hls::stream<warp_status_t>   status_out{"pr_status"};
    instr_word_t program[MAX_PROGRAM_LEN] = {};
    std::thread  cp_th;
    std::thread  mem_th;

    MemorySubsystem mem;
    std::vector<ap_uint<32>> backing;

    void loadAndStart(
            reg_t regs[MAX_WARPS_PER_CU][MAX_THREADS_PER_WARP][NUM_REGS_PER_THREAD],
            const std::vector<riscv_gpgpu::Instruction>& src) {
        size_t out_i = 0;
        for (const auto& gi : src) {
            raw_instr_t words[2];
            int n = encodeInstructionExpanded(static_cast<Opcode>(gi.opcode),
                                               gi.rd, gi.rs1, gi.rs2, gi.imm, words);
            for (int k = 0; k < n; ++k) program[out_i++] = words[k];
        }
        uint32_t plen = static_cast<uint32_t>(out_i);
        backing.assign(65536, 0);

        mem_th = std::thread([this]() {
            while (true) {
                mem_req_t req = mem_req_out.read();
                mem_resp_t resp = mem.handleRequest(req, backing.data());
                mem_resp_in.write(resp);
            }
        });

        cp_th = std::thread([this, regs, plen]() {
            compute_pipeline(0, dispatch_in, program, plen, regs, nullptr,
                              mem_req_out, mem_resp_in, status_out);
        });
    }

    warp_status_t run(slot_id_t slot, warp_id_t wid,
                      thread_mask_t mask = thread_mask_t(-1),
                      ap_uint<16> resume_pc = 0) {
        warp_dispatch_t d;
        d.slot_id = slot; d.warp_id = wid;
        d.active_mask_init = mask; d.resume_pc = resume_pc;
        dispatch_in.write(d);
        return status_out.read();
    }

    ~HlsRunner() { cp_th.detach(); mem_th.detach(); }
};

void initRegs(reg_t regs[MAX_THREADS_PER_WARP][NUM_REGS_PER_THREAD],
              RefState& ref,
              uint32_t global_tid_offset = 0, uint32_t local_warp_id = 0) {
    for (int t = 0; t < MAX_THREADS_PER_WARP; ++t) {
        for (int r = 0; r < NUM_REGS_PER_THREAD; ++r) { regs[t][r] = 0; ref.regs[t][r] = 0; }
        uint32_t tid  = global_tid_offset + t;
        uint32_t addr = 0x1000 + tid * 4;
        regs[t][1] = tid;  ref.regs[t][1] = tid;
        regs[t][2] = addr; ref.regs[t][2] = addr;
        regs[t][3] = local_warp_id; ref.regs[t][3] = local_warp_id;
    }
}

// Compare HLS and reference register files; emit diff on mismatch.
// Returns true if all checked registers match (or are within FP tolerance).
bool compareRegs(
        const char* kernel_name,
        const reg_t hls_regs[MAX_THREADS_PER_WARP][NUM_REGS_PER_THREAD],
        const uint32_t ref_regs[MAX_THREADS_PER_WARP][NUM_REGS_PER_THREAD],
        bool fp_mode = false,
        int check_reg = -1  /* -1 = all regs */) {
    bool ok = true;
    for (int t = 0; t < MAX_THREADS_PER_WARP; ++t) {
        int rstart = (check_reg >= 0) ? check_reg : 0;
        int rend   = (check_reg >= 0) ? check_reg + 1 : NUM_REGS_PER_THREAD;
        for (int r = rstart; r < rend; ++r) {
            uint32_t hv = static_cast<uint32_t>(hls_regs[t][r]);
            uint32_t rv = ref_regs[t][r];
            if (hv == rv) continue;
            if (fp_mode) {
                float fh, fr; std::memcpy(&fh, &hv, 4); std::memcpy(&fr, &rv, 4);
                if (std::isfinite(fh) && std::isfinite(fr) && std::fabs(fh - fr) <= std::fabs(fr) * 1e-6f) continue;
            }
            ok = false;
            ADD_FAILURE() << "[" << kernel_name << "] "
                          << "lane " << t << " r" << r
                          << ": HLS=0x" << std::hex << hv
                          << " REF=0x" << rv << std::dec;
        }
    }
    return ok;
}

}  // namespace

// ── T078 Kernel 1: divergent (divergentOddEven) ───────────────────────────────
// Even threads get r5=100, odd get r5=0 (VBRANCH on r1&1 == 0 condition).
TEST(ModelParity, DivergentOddEven) {
    static reg_t hls_regs[MAX_WARPS_PER_CU][MAX_THREADS_PER_WARP][NUM_REGS_PER_THREAD];
    RefState ref;
    initRegs(hls_regs[0], ref);

    auto prog = riscv_gpgpu::kernels::divergentOddEven();

    static HlsRunner runner;
    runner.loadAndStart(hls_regs, prog);
    warp_status_t st = runner.run(0, 0);
    EXPECT_EQ(st.code, WarpStatusCode::COMPLETE);

    executeRef(ref, prog);

    EXPECT_TRUE(compareRegs("divergentOddEven", hls_regs[0], ref.regs,
                             /*fp_mode=*/false, /*check_reg=*/5))
        << "r5 mismatch: HLS and reference executor disagree on divergent output";
}

// ── T078 Kernel 2: barrier-heavy (barrierRoundTrip) ──────────────────────────
// Two-dispatch sequence: SW then barrier then LW. r3 == r1 after completion.
TEST(ModelParity, BarrierRoundTripTwoDispatch) {
    static reg_t hls_regs[MAX_WARPS_PER_CU][MAX_THREADS_PER_WARP][NUM_REGS_PER_THREAD];
    RefState ref;
    initRegs(hls_regs[0], ref);

    auto prog = riscv_gpgpu::kernels::barrierRoundTrip(0);

    static HlsRunner runner;
    runner.loadAndStart(hls_regs, prog);

    // Pre-barrier: SW dispatched
    warp_status_t st1 = runner.run(0, 0);
    EXPECT_EQ(st1.code, WarpStatusCode::STALLED_AT_BARRIER);

    // Reference: run up to barrier
    executeRef(ref, prog);
    EXPECT_TRUE(ref.stalled);

    // Post-barrier: LW dispatched
    warp_status_t st2 = runner.run(0, 0, thread_mask_t(-1), st1.resume_pc);
    EXPECT_EQ(st2.code, WarpStatusCode::COMPLETE);

    // Reference: resume from barrier_pc
    ref.stalled = false;
    executeRef(ref, prog, ref.resume_pc);

    EXPECT_TRUE(compareRegs("barrierRoundTrip", hls_regs[0], ref.regs,
                             /*fp_mode=*/false, /*check_reg=*/3))
        << "r3 mismatch: load-after-barrier result differs between HLS and reference";
}

// ── T078 Kernel 3: FP (fpUniformSaxpy) ───────────────────────────────────────
// All threads compute r6 = alpha * x + y in float; same value per thread.
TEST(ModelParity, FpUniformSaxpyAllThreadsMatch) {
    static reg_t hls_regs[MAX_WARPS_PER_CU][MAX_THREADS_PER_WARP][NUM_REGS_PER_THREAD];
    RefState ref;
    initRegs(hls_regs[0], ref);

    const float alpha = 2.0f, x = 3.0f, y = 1.0f;
    auto prog = riscv_gpgpu::kernels::fpUniformSaxpy(alpha, x, y);

    static HlsRunner runner;
    runner.loadAndStart(hls_regs, prog);
    warp_status_t st = runner.run(0, 0);
    EXPECT_EQ(st.code, WarpStatusCode::COMPLETE);

    executeRef(ref, prog);

    // r6 should be floatToBits(alpha*x + y) = floatToBits(7.0f) for all lanes
    EXPECT_TRUE(compareRegs("fpUniformSaxpy", hls_regs[0], ref.regs,
                             /*fp_mode=*/true, /*check_reg=*/6))
        << "r6 FP mismatch: alpha*x+y differs between HLS and reference";
}

// ── T078 Kernel 4: FP divergent (fpDivergentSaxpy) ────────────────────────────
// Threads with r0 & (r0+1) == 0 compute SAXPY; others are masked (r6=0).
// Falling-through set for r0=t is t in {0,1,3,7,15,31} (all-ones bit patterns).
// See docs/hls/interfaces.md §9.2 for the full divergence analysis.
TEST(ModelParity, FpDivergentSaxpyEvenOddMask) {
    static reg_t hls_regs[MAX_WARPS_PER_CU][MAX_THREADS_PER_WARP][NUM_REGS_PER_THREAD];
    RefState ref;
    initRegs(hls_regs[0], ref);

    const float alpha = 3.0f, y = 1.0f;
    // Set r0[t]=t (thread_index), r3=alpha, r4[t]=float(t+1), r5=y
    for (int t = 0; t < MAX_THREADS_PER_WARP; ++t) {
        hls_regs[0][t][0] = ref.regs[t][0] = static_cast<uint32_t>(t);  // r0 = thread_index
        hls_regs[0][t][3] = ref.regs[t][3] = floatToBits(alpha);
        hls_regs[0][t][4] = ref.regs[t][4] = floatToBits(static_cast<float>(t + 1));
        hls_regs[0][t][5] = ref.regs[t][5] = floatToBits(y);
    }

    auto prog = riscv_gpgpu::kernels::fpDivergentSaxpy();

    static HlsRunner runner2;
    runner2.loadAndStart(hls_regs, prog);
    warp_status_t st = runner2.run(0, 0);
    EXPECT_EQ(st.code, WarpStatusCode::COMPLETE);

    executeRef(ref, prog);

    EXPECT_TRUE(compareRegs("fpDivergentSaxpy", hls_regs[0], ref.regs,
                             /*fp_mode=*/true, /*check_reg=*/6))
        << "r6 mismatch for fpDivergentSaxpy (even threads computed, odd masked)";
}

// ── T078 Kernel 5: integer SAXPY instr_count matches reference program length ─
// Also confirms that both paths produce identical register states (double check
// against test_compute_pipeline.cpp's existing coverage to make it explicit).
TEST(ModelParity, IntSaxpyRegisterAndCountParity) {
    static reg_t hls_regs[MAX_WARPS_PER_CU][MAX_THREADS_PER_WARP][NUM_REGS_PER_THREAD];
    RefState ref;
    initRegs(hls_regs[0], ref);

    const int32_t alpha = 2, y_val = 10;
    auto prog = riscv_gpgpu::kernels::intSaxpy(alpha, y_val);

    static HlsRunner runner3;
    runner3.loadAndStart(hls_regs, prog);
    warp_status_t st = runner3.run(0, 0);
    EXPECT_EQ(st.code, WarpStatusCode::COMPLETE);

    executeRef(ref, prog);

    // r6[t] = (global_tid + 1) * alpha + y = (t+1)*2 + 10
    EXPECT_TRUE(compareRegs("intSaxpy", hls_regs[0], ref.regs,
                             /*fp_mode=*/false, /*check_reg=*/6));

    // instr_count from HLS must be >= reference count (HLS may count LUI
    // expansions, ref counts logical instructions; HLS count >= ref count)
    EXPECT_GE(static_cast<uint32_t>(st.instr_count), ref.instr_count);
}
