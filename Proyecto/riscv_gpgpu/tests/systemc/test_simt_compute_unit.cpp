// test_simt_compute_unit.cpp - ComputeUnit functional SIMT tests

#include <gtest/gtest.h>

#include "../../models/systemc/src/compute_unit/compute_unit.h"

using namespace riscv_gpgpu;

TEST(SIMTComputeUnit, ExecuteWarpRunsScalarOpsAcrossActiveLanes) {
    ComputeUnit::Config cfg;
    cfg.unit_id = 0;
    cfg.threads_per_warp = 2;
    cfg.num_threads = 2;
    cfg.max_warps = 1;

    ComputeUnit cu("simt_cu_basic", cfg);

    WarpContext ctx;
    ctx.warp_id = 0;
    ctx.pc = 0;
    ctx.regs.assign(2, std::vector<uint32_t>(32, 0));
    ctx.program = {
        makeInstr(Opcode::ADDI, 5, 0, 0, 7),
        makeInstr(Opcode::HALT)
    };

    cu.executeWarp(ctx);

    EXPECT_EQ(ctx.state, WarpState::COMPLETE);
    EXPECT_EQ(ctx.regs[0][5], 7u);
    EXPECT_EQ(ctx.regs[1][5], 7u);
}

TEST(SIMTComputeUnit, DivergentBranchMasksLanesUntilJoin) {
    ComputeUnit::Config cfg;
    cfg.unit_id = 0;
    cfg.threads_per_warp = 2;
    cfg.num_threads = 2;
    cfg.max_warps = 1;

    ComputeUnit cu("simt_cu_divergence", cfg);

    WarpContext ctx;
    ctx.warp_id = 0;
    ctx.pc = 0;
    ctx.regs.assign(2, std::vector<uint32_t>(32, 0));

    // Thread 0 takes branch path (rs1 == 0), thread 1 is masked out until join.
    ctx.regs[0][1] = 0;
    ctx.regs[1][1] = 1;

    ctx.program = {
        makeInstr(Opcode::VBRANCH, 0, 1, 0, 0),
        makeInstr(Opcode::ADDI, 2, 0, 0, 11),
        makeInstr(Opcode::VJOIN),
        makeInstr(Opcode::ADDI, 3, 0, 0, 22),
        makeInstr(Opcode::HALT)
    };

    cu.executeWarp(ctx);

    EXPECT_EQ(ctx.state, WarpState::COMPLETE);
    EXPECT_EQ(ctx.regs[0][2], 11u);
    EXPECT_EQ(ctx.regs[1][2], 0u);
    EXPECT_EQ(ctx.regs[0][3], 22u);
    EXPECT_EQ(ctx.regs[1][3], 22u);
    EXPECT_GT(cu.getDivergenceEvents(), 0u);
}