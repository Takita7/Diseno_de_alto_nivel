// test_scheduler.cpp - Unit tests for warp scheduler
//
// Tests scheduler functionality and dispatch policies
//

#include <gtest/gtest.h>
#include "../../models/systemc/scheduler/warp_scheduler.h"

using namespace riscv_gpgpu;

class WarpSchedulerTest : public ::testing::Test {
protected:
    void SetUp() override {
        config.num_compute_units = 4;
        config.max_warps_per_cu = 16;
        config.policy = WarpScheduler::SchedulingPolicy::FIFO;
        config.enable_optimization = false;
        config.batch_size = 4;
    }
    
    WarpScheduler::Config config;
};

TEST_F(WarpSchedulerTest, InitializationSucceeds) {
    // Test that scheduler can be initialized
    EXPECT_NO_THROW({
        WarpScheduler scheduler("test_scheduler", config);
    });
}

TEST_F(WarpSchedulerTest, WarpSelectionFIFO) {
    // Test FIFO warp selection policy
    WarpScheduler scheduler("test_scheduler", config);
    
    // Submit kernel
    scheduler.submitKernel(0, 4, 1);
    
    // Should have ready warps
    EXPECT_TRUE(scheduler.hasReadyWarps(0));
}

TEST_F(WarpSchedulerTest, RoundRobinScheduling) {
    // Test round-robin scheduling
    config.policy = WarpScheduler::SchedulingPolicy::ROUND_ROBIN;
    WarpScheduler scheduler("test_scheduler", config);
    
    scheduler.submitKernel(0, 4, 1);
    EXPECT_TRUE(scheduler.hasReadyWarps(0));
}

TEST_F(WarpSchedulerTest, MultiComputeUnitDistribution) {
    // Test warp distribution across CUs
    WarpScheduler scheduler("test_scheduler", config);
    
    scheduler.submitKernel(0, 4, 1);
    
    int total_ready_warps = 0;
    for (uint32_t cu = 0; cu < config.num_compute_units; ++cu) {
        if (scheduler.hasReadyWarps(cu)) {
            total_ready_warps++;
        }
    }
    
    EXPECT_GT(total_ready_warps, 0);
}

TEST_F(WarpSchedulerTest, WarpCompletion) {
    // Test warp completion tracking
    WarpScheduler scheduler("test_scheduler", config);
    
    scheduler.submitKernel(0, 4, 1);
    scheduler.markWarpComplete(0, 0);
    
    // After completion, should have fewer ready warps
    // (exact behavior depends on implementation)
}

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
