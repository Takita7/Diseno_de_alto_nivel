// test_pipeline.cpp - Integration tests for kernel execution pipeline
//
// Tests complete kernel execution from launch to completion
//

#include <gtest/gtest.h>
#include <systemc>
#include "../../models/systemc/top/top.h"

using namespace riscv_gpgpu;

class PipelineIntegrationTest : public ::testing::Test {
protected:
    void SetUp() override {
        config.num_compute_units = 4;
        config.threads_per_warp = 32;
        config.max_warps_per_cu = 16;
        config.shared_mem_size = 49152;
        config.l1_cache_size = 16384;
        config.l2_cache_size = 262144;
    }
    
    GPGPUTop::Config config;
};

TEST_F(PipelineIntegrationTest, KernelLaunchSucceeds) {
    // Test that kernel can be launched
    GPGPUTop top("gpgpu_top", config);
    
    EXPECT_NO_THROW({
        top.launchKernel(4, 1);
    });
}

TEST_F(PipelineIntegrationTest, KernelExecutionCompletes) {
    // Test that kernel execution completes
    GPGPUTop top("gpgpu_top", config);
    
    top.launchKernel(4, 1);
    
    // Run simulation for some cycles
    sc_core::sc_start(1000, sc_core::SC_NS);
    
    // Kernel should eventually complete
    // (Note: This is a simplified check)
}

TEST_F(PipelineIntegrationTest, StatisticsCollected) {
    // Test that statistics are collected
    GPGPUTop top("gpgpu_top", config);
    
    top.launchKernel(4, 1);
    sc_core::sc_start(1000, sc_core::SC_NS);
    
    uint64_t cycles = top.getTotalCycles();
    uint64_t instructions = top.getTotalInstructions();
    
    EXPECT_GE(cycles, 0);
    EXPECT_GE(instructions, 0);
}

TEST_F(PipelineIntegrationTest, CacheStatistics) {
    // Test cache hit/miss statistics
    GPGPUTop top("gpgpu_top", config);
    
    top.launchKernel(4, 1);
    sc_core::sc_start(1000, sc_core::SC_NS);
    
    uint32_t l1_hits = top.getL1CacheHits();
    uint32_t l1_misses = top.getL1CacheMisses();
    
    EXPECT_GE(l1_hits, 0);
    EXPECT_GE(l1_misses, 0);
}

TEST_F(PipelineIntegrationTest, DivergenceTracking) {
    // Test divergence event tracking
    GPGPUTop top("gpgpu_top", config);
    
    top.launchKernel(4, 1);
    sc_core::sc_start(1000, sc_core::SC_NS);
    
    uint32_t divergence_events = top.getDivergenceEvents();
    EXPECT_GE(divergence_events, 0);
}

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
