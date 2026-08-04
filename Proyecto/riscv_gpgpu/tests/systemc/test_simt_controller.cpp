// test_simt_controller.cpp - Unit tests for SIMTController

#include <gtest/gtest.h>
#include <array>
#include "../../models/systemc/src/simt_controller/simt_controller.h"

using namespace riscv_gpgpu;

class SIMTControllerTest : public ::testing::Test {
protected:
    SIMTController::Config config_{
        SIMTController::RecovergenceMode::IMMEDIATE,
        true,
        32
    };
};

TEST_F(SIMTControllerTest, InitializeBranchAndJoin) {
    SIMTController controller("simt_ctrl", config_);

    controller.initializeWarp(0, 8);
    EXPECT_EQ(controller.getActiveMask(0), 0xFFu);

    std::array<bool, 32> conditions{};
    for (size_t i = 0; i < conditions.size(); ++i) {
        conditions[i] = (i < 4);
    }

    controller.handleBranch(0, conditions.data());
    EXPECT_EQ(controller.getActiveMask(0), 0x0Fu);
    EXPECT_EQ(controller.getTotalDivergenceEvents(), 1u);
    EXPECT_GT(controller.getTotalWastedCycles(), 0u);

    controller.handleJoin(0);
    EXPECT_EQ(controller.getActiveMask(0), 0xFFu);
}

TEST_F(SIMTControllerTest, UniformBranchDoesNotDiverge) {
    SIMTController controller("simt_ctrl_uniform", config_);
    controller.initializeWarp(0, 8);

    std::array<bool, 32> conditions{};
    conditions.fill(true);

    controller.handleBranch(0, conditions.data());
    EXPECT_EQ(controller.getActiveMask(0), 0xFFu);
    EXPECT_EQ(controller.getTotalDivergenceEvents(), 0u);
}

// T047b: verify the reconvergence PC pushed by handleBranch is stored and
// returned by getReconvergencePC, then cleared after handleJoin.
TEST_F(SIMTControllerTest, ReconvergencePcTracked) {
    SIMTController controller("simt_ctrl_pc", config_);
    controller.initializeWarp(0, 8);

    // No divergence stack yet – should return 0.
    EXPECT_EQ(controller.getReconvergencePC(0), 0u);

    // Half threads fall-through (true), half branch (false) → real divergence.
    std::array<bool, 32> conditions{};
    for (size_t i = 0; i < 8; ++i) conditions[i] = (i < 4);

    const uint32_t reconv_pc = 0x00001000u;
    controller.handleBranch(0, conditions.data(), reconv_pc);

    EXPECT_EQ(controller.getTotalDivergenceEvents(), 1u);
    EXPECT_EQ(controller.getActiveMask(0), 0x0Fu);     // lower 4 threads active
    EXPECT_EQ(controller.getReconvergencePC(0), reconv_pc);

    // After join the stack is popped – reconvergence PC must be cleared.
    controller.handleJoin(0);
    EXPECT_EQ(controller.getActiveMask(0), 0xFFu);     // all threads restored
    EXPECT_EQ(controller.getReconvergencePC(0), 0u);   // stack is empty
}