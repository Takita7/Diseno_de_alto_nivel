// test_hls_binary_execution.cpp - T075: binary execution parity
//
// Verifies that the HLS compute_pipeline correctly decodes and executes
// programs built from raw RV32I binary words (via encodeInstruction())
// — not from kernel_programs.h's vector<Instruction> helpers.
//
// Gap addressed: the golden SystemC model executes real RV32I binary
// via riscv_isa.h + PC-driven fetch/execute (docs/hls/interfaces.md §T075).
// The HLS path uses custom-opcode encoding for GPGPU-specific ops
// (rv32i_codec.h §13.5/13.6), but standard RV32I integer ops (ADD/SUB/
// ADDI/LW/SW/BEQ/BNE) share the SAME bit encoding in both paths. This
// test constructs programs entirely from standard RV32I words and confirms
// the HLS decode+execute path produces the same result as hand-traced
// SystemC semantics for those ops.
//
// Intentional deviation documented: ops outside the RV32I∩custom-opcode
// intersection (VADD/VMUL/VFMUL etc.) have different bit encodings — they
// are custom-0/custom-1 in HLS, unimplemented in riscv_isa.h. Kernels using
// those ops cannot be run on both paths with identical programs. See
// docs/hls/interfaces.md §T075 parity matrix row "vector/float ops".

#include <gtest/gtest.h>
#include <hls_stream.h>
#include <thread>
#include <vector>

#include "compute_unit/compute_pipeline.h"
#include "compute_unit/rv32i_codec.h"
#include "memory/memory_pipeline.h"

using namespace riscv_gpgpu_hls;

namespace {

void initRegs(reg_t regs[MAX_THREADS_PER_WARP][NUM_REGS_PER_THREAD],
              uint32_t global_tid_offset = 0) {
    for (int t = 0; t < MAX_THREADS_PER_WARP; ++t) {
        for (int r = 0; r < NUM_REGS_PER_THREAD; ++r) regs[t][r] = 0;
        regs[t][1] = global_tid_offset + t;   // r1 = global_tid (standard convention)
        regs[t][2] = 0x2000 + (global_tid_offset + t) * 4;
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

    warp_status_t dispatchAndWait(slot_id_t slot = 0, warp_id_t wid = 0,
                                   thread_mask_t mask = thread_mask_t(-1),
                                   ap_uint<16> resume_pc = 0) {
        warp_dispatch_t d;
        d.slot_id = slot; d.warp_id = wid;
        d.active_mask_init = mask; d.resume_pc = resume_pc;
        dispatch_in.write(d);
        return status_out.read();
    }

    ~CpFixture() { th.detach(); }
};

}  // namespace

// ADD x4, x1, x1  →  r4[t] = 2 * r1[t] = 2*t
// Encodes the canonical RV32I ADD R-type (opcode=0x33, funct3=0x0, funct7=0x00).
TEST(HlsBinaryExecution, AddR4EqualsTwiceR1) {
    static reg_t regs[MAX_WARPS_PER_CU][MAX_THREADS_PER_WARP][NUM_REGS_PER_THREAD];
    initRegs(regs[0], /*global_tid_offset=*/0);

    static CpFixture cp;
    cp.program[0] = encodeInstruction(Opcode::ADD,  4, 1, 1, 0);  // ADD x4, x1, x1
    cp.program[1] = encodeInstruction(Opcode::HALT);
    cp.start(regs, 2);

    warp_status_t st = cp.dispatchAndWait();
    EXPECT_EQ(st.code, WarpStatusCode::COMPLETE);
    EXPECT_EQ(static_cast<uint32_t>(st.instr_count), 1u);  // exactly one ADD retired

    for (int t = 0; t < MAX_THREADS_PER_WARP; ++t) {
        uint32_t expect = 2u * t;
        EXPECT_EQ(static_cast<uint32_t>(regs[0][t][4]), expect) << "lane " << t;
    }
}

// ADDI x5, x1, 7  →  r5[t] = r1[t] + 7 = t + 7
// Tests the I-type encoding with a small positive immediate.
TEST(HlsBinaryExecution, AddiR5EqualsTidPlusSeven) {
    static reg_t regs[MAX_WARPS_PER_CU][MAX_THREADS_PER_WARP][NUM_REGS_PER_THREAD];
    initRegs(regs[0], 0);

    static CpFixture cp;
    cp.program[0] = encodeInstruction(Opcode::ADDI, 5, 1, 0, 7);  // ADDI x5, x1, 7
    cp.program[1] = encodeInstruction(Opcode::HALT);
    cp.start(regs, 2);

    warp_status_t st = cp.dispatchAndWait();
    EXPECT_EQ(st.code, WarpStatusCode::COMPLETE);

    for (int t = 0; t < MAX_THREADS_PER_WARP; ++t) {
        EXPECT_EQ(static_cast<uint32_t>(regs[0][t][5]), uint32_t(t + 7)) << "lane " << t;
    }
}

// SUB x6, x1, x1  →  r6[t] = r1[t] - r1[t] = 0
// Confirms R-type SUB encoding (opcode=0x33, funct7=0x20) is decoded correctly.
TEST(HlsBinaryExecution, SubResultIsZero) {
    static reg_t regs[MAX_WARPS_PER_CU][MAX_THREADS_PER_WARP][NUM_REGS_PER_THREAD];
    initRegs(regs[0]);
    // Pre-set r6 to non-zero to confirm it gets overwritten
    for (int t = 0; t < MAX_THREADS_PER_WARP; ++t) regs[0][t][6] = 0xDEAD;

    static CpFixture cp;
    cp.program[0] = encodeInstruction(Opcode::SUB, 6, 1, 1, 0);  // SUB x6, x1, x1
    cp.program[1] = encodeInstruction(Opcode::HALT);
    cp.start(regs, 2);

    warp_status_t st = cp.dispatchAndWait();
    EXPECT_EQ(st.code, WarpStatusCode::COMPLETE);

    for (int t = 0; t < MAX_THREADS_PER_WARP; ++t)
        EXPECT_EQ(static_cast<uint32_t>(regs[0][t][6]), 0u) << "lane " << t;
}

// SLT x7, x1, x1  →  r7[t] = (r1[t] < r1[t]) = 0
// Signed comparison when operands are equal — result always 0.
TEST(HlsBinaryExecution, SltSameOperandIsZero) {
    static reg_t regs[MAX_WARPS_PER_CU][MAX_THREADS_PER_WARP][NUM_REGS_PER_THREAD];
    initRegs(regs[0]);

    static CpFixture cp;
    cp.program[0] = encodeInstruction(Opcode::SLT, 7, 1, 1, 0);  // SLT x7, x1, x1
    cp.program[1] = encodeInstruction(Opcode::HALT);
    cp.start(regs, 2);

    warp_status_t st = cp.dispatchAndWait();
    EXPECT_EQ(st.code, WarpStatusCode::COMPLETE);

    for (int t = 0; t < MAX_THREADS_PER_WARP; ++t)
        EXPECT_EQ(static_cast<uint32_t>(regs[0][t][7]), 0u) << "lane " << t;
}

// Chain: ADDI x3, x0, 100  then  ADD x4, x1, x3  → r4[t] = t + 100
// Verifies that a two-instruction sequence propagates register state correctly.
TEST(HlsBinaryExecution, TwoInstructionChain) {
    static reg_t regs[MAX_WARPS_PER_CU][MAX_THREADS_PER_WARP][NUM_REGS_PER_THREAD];
    initRegs(regs[0]);

    static CpFixture cp;
    cp.program[0] = encodeInstruction(Opcode::ADDI, 3, 0, 0, 100);  // ADDI x3, x0, 100
    cp.program[1] = encodeInstruction(Opcode::ADD,  4, 1, 3, 0);   // ADD  x4, x1, x3
    cp.program[2] = encodeInstruction(Opcode::HALT);
    cp.start(regs, 3);

    warp_status_t st = cp.dispatchAndWait();
    EXPECT_EQ(st.code, WarpStatusCode::COMPLETE);
    EXPECT_EQ(static_cast<uint32_t>(st.instr_count), 2u);

    for (int t = 0; t < MAX_THREADS_PER_WARP; ++t) {
        EXPECT_EQ(static_cast<uint32_t>(regs[0][t][3]), 100u) << "r3 lane " << t;
        EXPECT_EQ(static_cast<uint32_t>(regs[0][t][4]), uint32_t(t + 100)) << "r4 lane " << t;
    }
}

// LW/SW round-trip using standard I-type/S-type binary encoding.
// Each thread stores r1 to mem[r2], then loads it back into r8.
// Proves the memory-path decode (opcode 0x03 LW, 0x23 SW) is correct.
TEST(HlsBinaryExecution, LwSwRoundTripBinaryEncoded) {
    static reg_t regs[MAX_WARPS_PER_CU][MAX_THREADS_PER_WARP][NUM_REGS_PER_THREAD];
    initRegs(regs[0]);

    static CpFixture cp;
    cp.program[0] = encodeInstruction(Opcode::SW, 0, 2, 1, 0);   // SW x1, 0(x2)
    cp.program[1] = encodeInstruction(Opcode::LW, 8, 2, 0, 0);   // LW x8, 0(x2)
    cp.program[2] = encodeInstruction(Opcode::HALT);

    static hls::stream<mem_req_t>  mem_req{"mem_req_bin"};
    static hls::stream<mem_resp_t> mem_resp{"mem_resp_bin"};
    static hls::stream<warp_dispatch_t> disp{"disp_bin"};
    static hls::stream<warp_status_t>   stat{"stat_bin"};
    static MemorySubsystem bin_mem;
    static std::vector<ap_uint<32>> bin_backing(65536, 0);

    std::thread mem_th([&]() {
        while (true) {
            mem_req_t req = mem_req.read();
            mem_resp_t resp = bin_mem.handleRequest(req, bin_backing.data());
            mem_resp.write(resp);
        }
    });
    mem_th.detach();

    std::thread cp_th([&]() {
        compute_pipeline(0, disp, cp.program, 3, regs, nullptr, mem_req, mem_resp, stat);
    });
    cp_th.detach();

    warp_dispatch_t d;
    d.slot_id = 0; d.warp_id = 0; d.active_mask_init = thread_mask_t(-1); d.resume_pc = 0;
    disp.write(d);
    warp_status_t st = stat.read();

    EXPECT_EQ(st.code, WarpStatusCode::COMPLETE);
    EXPECT_EQ(static_cast<uint32_t>(st.instr_count), 2u);  // SW + LW
    for (int t = 0; t < MAX_THREADS_PER_WARP; ++t)
        EXPECT_EQ(static_cast<uint32_t>(regs[0][t][8]),
                  static_cast<uint32_t>(regs[0][t][1])) << "lane " << t;
}
