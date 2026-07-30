// test_hls_data_structures.cpp - regression tests for the HLS-safe
// data-structure layer (hls/src/common/), which precedes the T022/T023
// compute_pipeline.cpp/memory_pipeline.cpp implementations proper.
//
// Compiled against the real Vitis HLS ap_int.h/hls_stream.h headers (csim
// style, via plain g++ - vitis_hls itself is not required to run these).

#include <gtest/gtest.h>
#include <hls_stream.h>

#include "common/hls_config.h"
#include "common/hls_types.h"
#include "compute_unit/rv32i_codec.h"
#include "simt_controller/divergence_stack.h"
#include "memory/cache_bank.h"

using namespace riscv_gpgpu_hls;

// docs/hls/interfaces.md SS13: instr_word_t is now raw_instr_t (a real
// RV32I-encoded ap_uint<32>), not a decoded Instruction struct directly -
// this now round-trips through the codec instead of streaming a struct.
// `pc` is no longer checked: it isn't carried in the raw word
// (decodeInstruction() always returns pc=0 - see rv32i_codec.h).
TEST(HlsTypes, InstrWordStreamRoundTrip) {
    hls::stream<instr_word_t> s;
    instr_word_t w = encodeInstruction(Opcode::ADDI, /*rd=*/3, /*rs1=*/0, /*rs2=*/0, /*imm=*/42);
    s.write(w);

    Instruction o = decodeInstruction(s.read());
    EXPECT_EQ(o.opcode, Opcode::ADDI);
    EXPECT_EQ(o.rd, 3);
    EXPECT_EQ(o.imm, 42);
}

TEST(HlsTypes, FloatRegisterRoundTrip) {
    reg_t r = floatAsReg(3.5f);
    EXPECT_FLOAT_EQ(regAsFloat(r), 3.5f);
}

TEST(DivergenceStackTest, UniformBranchCausesNoDivergence) {
    DivergenceStack ds;
    ds.initializeWarpFromThreadCount(32);
    ASSERT_EQ(ds.getActiveMask(), thread_mask_t(-1));

    bool cond[MAX_THREADS_PER_WARP];
    for (int t = 0; t < MAX_THREADS_PER_WARP; ++t) cond[t] = true; // all take same path
    ds.handleBranch(cond);

    EXPECT_EQ(ds.getDivergenceEvents(), 0);
    EXPECT_FALSE(ds.hasPendingDivergence());
}

TEST(DivergenceStackTest, DivergeThenJoinReconverges) {
    DivergenceStack ds;
    ds.initializeWarpFromThreadCount(32);

    bool cond[MAX_THREADS_PER_WARP];
    for (int t = 0; t < MAX_THREADS_PER_WARP; ++t) cond[t] = (t % 2 == 0);
    ds.handleBranch(cond); // even threads taken, odd threads not_taken -> diverge

    ASSERT_EQ(ds.getDivergenceEvents(), 1);
    ASSERT_TRUE(ds.hasPendingDivergence());
    for (int t = 0; t < MAX_THREADS_PER_WARP; ++t) {
        EXPECT_EQ(ds.isThreadActive(t), (t % 2 == 0)) << "lane " << t;
    }

    ds.handleJoin();
    EXPECT_FALSE(ds.hasPendingDivergence());
    for (int t = 0; t < MAX_THREADS_PER_WARP; ++t) {
        EXPECT_TRUE(ds.isThreadActive(t)) << "lane " << t << " should be active after join";
    }
}

// Pulled in from an upstream golden-model bug fix (models/systemc/src/
// simt_controller/simt_controller.cpp, commit 9c4dfea "GPGPU READY"):
// when EVERY active lane takes the masked path (none fall through), the
// mask must become 0 and push onto the stack for handleJoin() to restore -
// NOT be treated as "no divergence" and left fully active. The old logic
// (mask = (taken!=0) ? taken : not_taken) computed mask = not_taken here,
// which equals the entire current mask when taken==0 - i.e. every lane
// stayed active despite none of them satisfying the fall-through condition.
// This is exactly the pattern golden's new parallelReduction() kernel relies
// on (a homogeneous warp where every thread takes the same masked branch).
TEST(DivergenceStackTest, AllLanesMaskedIsNotTreatedAsNoDivergence) {
    DivergenceStack ds;
    ds.initializeWarpFromThreadCount(32);

    bool cond[MAX_THREADS_PER_WARP];
    for (int t = 0; t < MAX_THREADS_PER_WARP; ++t) cond[t] = false; // every lane takes the masked path

    ds.handleBranch(cond);

    EXPECT_EQ(ds.getActiveMask(), thread_mask_t(0)) << "all-masked case must zero the mask, not leave it full";
    EXPECT_EQ(ds.getDivergenceEvents(), 0) << "every lane agreed - not a divergence event";
    EXPECT_TRUE(ds.hasPendingDivergence()) << "must still push so handleJoin() can restore the lanes";

    ds.handleJoin();
    EXPECT_FALSE(ds.hasPendingDivergence());
    for (int t = 0; t < MAX_THREADS_PER_WARP; ++t) {
        EXPECT_TRUE(ds.isThreadActive(t)) << "lane " << t << " should be restored after join";
    }
}

// Worst case for a 32-lane warp is NOT log2(32)=5: handleBranch only ever
// looks at currently-active lanes (matches SIMTController::handleBranch's
// `if ((current >> t) & 1u)`), so a pathological "peel off one lane at a
// time" pattern (e.g. a chain of `if (tid==0) ... else if (tid==1) ... else`)
// can push once per peeled lane - up to 31 pushes for 32 lanes, without an
// intervening join. That comfortably exceeds MAX_DIVERGENCE_DEPTH=8. This is
// a real sizing question for the team (docs/hls/interfaces.md SS6), not a
// theoretical edge case - flagged here so the guard's behavior (sticky
// overflow flag, no corruption) is pinned down by a test rather than assumed.
TEST(DivergenceStackTest, OverflowIsFlaggedNotCorrupting) {
    DivergenceStack ds;
    ds.initializeWarpFromThreadCount(32);
    for (int peel = 0; peel < MAX_DIVERGENCE_DEPTH + 2; ++peel) {
        bool cond[MAX_THREADS_PER_WARP];
        for (int t = 0; t < MAX_THREADS_PER_WARP; ++t) cond[t] = (t != peel); // peel off lane `peel`
        ds.handleBranch(cond);
    }
    EXPECT_TRUE(ds.overflowed());
}

TEST(CacheBankTest, MissThenHitReturnsFilledLineData) {
    static L1Cache l1; // static: avoid large stack allocation of BRAM-sized arrays
    l1.reset();

    L1Cache::addr_t addr = 0x1000; // word-aligned
    auto miss = l1.lookup(addr);
    EXPECT_FALSE(miss.hit);

    L1Cache::word_t line[WORDS_PER_LINE];
    for (int i = 0; i < WORDS_PER_LINE; ++i) line[i] = 0xA0000000u + i;
    l1.fillLine(L1Cache::lineBaseAddr(addr), line);

    auto hit = l1.lookup(addr);
    ASSERT_TRUE(hit.hit);
    int widx = static_cast<int>(L1Cache::lineWordIndex(addr));
    EXPECT_EQ(hit.data, line[widx]);
}

TEST(CacheBankTest, SameSetDifferentTagLinesCoexistAcrossWays) {
    static L1Cache l1;
    l1.reset();

    // Two addresses mapping to the same set but different tags (differ by
    // exactly one full cache's worth of sets*line-size) - both must be able
    // to reside simultaneously across the L1_WAYS parallel banks.
    L1Cache::addr_t addr_a = 0x0000;
    L1Cache::addr_t addr_b = addr_a
        + (L1Cache::addr_t(1) << (L1Cache::LINE_OFFSET_BITS + L1Cache::SET_BITS));

    L1Cache::word_t line_a[WORDS_PER_LINE];
    L1Cache::word_t line_b[WORDS_PER_LINE];
    for (int i = 0; i < WORDS_PER_LINE; ++i) { line_a[i] = 0x1111; line_b[i] = 0x2222; }
    l1.fillLine(L1Cache::lineBaseAddr(addr_a), line_a);
    l1.fillLine(L1Cache::lineBaseAddr(addr_b), line_b);

    auto ra = l1.lookup(addr_a);
    auto rb = l1.lookup(addr_b);
    ASSERT_TRUE(ra.hit);
    ASSERT_TRUE(rb.hit);
    EXPECT_EQ(ra.data, 0x1111);
    EXPECT_EQ(rb.data, 0x2222);
    EXPECT_NE(ra.way, rb.way);
}

TEST(CacheBankTest, L2CacheSizingMatchesArchConfigDefaults) {
    // arch_config.yaml: l2_cache_size: 262144 (256KB), cache_line_size: 128
    EXPECT_EQ(L2_WAYS * L2_SETS_PER_WAY * WORDS_PER_LINE * 4, L2_SIZE_BYTES);
    EXPECT_EQ(L1_WAYS * L1_SETS_PER_WAY * WORDS_PER_LINE * 4, L1_SIZE_BYTES);
}
