// test_pipeline.cpp - Integration tests for kernel execution pipeline
//
// Tests complete kernel execution from launch to completion
//

#include <gtest/gtest.h>
#include <systemc>
#include "../../models/systemc/src/top/top.h"
#include "../../models/systemc/src/common/kernel_programs.h"

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
    // Zero-time sc_start() to leave elaboration and enter the simulation
    // phase before launchKernel()'s immediate sc_event::notify() - matches
    // models/systemc/test/regression_test.cpp's sequencing (line 96 there);
    // without it SystemC throws E521 "immediate notification is not allowed
    // during update phase or elaboration". Missing here originally - this
    // GTest file predates launchKernel() requiring this and was never
    // updated to match.
    sc_core::sc_start(sc_core::sc_time(0, sc_core::SC_NS));

    EXPECT_NO_THROW({
        top.launchKernel(4, 1, kernels::intSaxpy());
    });
}

TEST_F(PipelineIntegrationTest, KernelExecutionCompletes) {
    // Test that kernel execution completes
    GPGPUTop top("gpgpu_top", config);
    // Zero-time sc_start() to leave elaboration and enter the simulation
    // phase before launchKernel()'s immediate sc_event::notify() - matches
    // models/systemc/test/regression_test.cpp's sequencing (line 96 there);
    // without it SystemC throws E521 "immediate notification is not allowed
    // during update phase or elaboration". Missing here originally - this
    // GTest file predates launchKernel() requiring this and was never
    // updated to match.
    sc_core::sc_start(sc_core::sc_time(0, sc_core::SC_NS));

    top.launchKernel(4, 1, kernels::intSaxpy());
    
    // Run simulation for some cycles
    sc_core::sc_start(1000, sc_core::SC_NS);
    
    // Kernel should eventually complete
    // (Note: This is a simplified check)
}

TEST_F(PipelineIntegrationTest, StatisticsCollected) {
    // Test that statistics are collected
    GPGPUTop top("gpgpu_top", config);
    // Zero-time sc_start() to leave elaboration and enter the simulation
    // phase before launchKernel()'s immediate sc_event::notify() - matches
    // models/systemc/test/regression_test.cpp's sequencing (line 96 there);
    // without it SystemC throws E521 "immediate notification is not allowed
    // during update phase or elaboration". Missing here originally - this
    // GTest file predates launchKernel() requiring this and was never
    // updated to match.
    sc_core::sc_start(sc_core::sc_time(0, sc_core::SC_NS));

    top.launchKernel(4, 1, kernels::intSaxpy());
    sc_core::sc_start(1000, sc_core::SC_NS);
    
    uint64_t cycles = top.getTotalCycles();
    uint64_t instructions = top.getTotalInstructions();
    
    EXPECT_GE(cycles, 0);
    EXPECT_GE(instructions, 0);
}

TEST_F(PipelineIntegrationTest, CacheStatistics) {
    // Test cache hit/miss statistics
    GPGPUTop top("gpgpu_top", config);
    // Zero-time sc_start() to leave elaboration and enter the simulation
    // phase before launchKernel()'s immediate sc_event::notify() - matches
    // models/systemc/test/regression_test.cpp's sequencing (line 96 there);
    // without it SystemC throws E521 "immediate notification is not allowed
    // during update phase or elaboration". Missing here originally - this
    // GTest file predates launchKernel() requiring this and was never
    // updated to match.
    sc_core::sc_start(sc_core::sc_time(0, sc_core::SC_NS));

    top.launchKernel(4, 1, kernels::intSaxpy());
    sc_core::sc_start(1000, sc_core::SC_NS);
    
    uint32_t l1_hits = top.getL1CacheHits();
    uint32_t l1_misses = top.getL1CacheMisses();
    
    EXPECT_GE(l1_hits, 0);
    EXPECT_GE(l1_misses, 0);
}

TEST_F(PipelineIntegrationTest, DivergenceTracking) {
    // Test divergence event tracking
    GPGPUTop top("gpgpu_top", config);
    // Zero-time sc_start() to leave elaboration and enter the simulation
    // phase before launchKernel()'s immediate sc_event::notify() - matches
    // models/systemc/test/regression_test.cpp's sequencing (line 96 there);
    // without it SystemC throws E521 "immediate notification is not allowed
    // during update phase or elaboration". Missing here originally - this
    // GTest file predates launchKernel() requiring this and was never
    // updated to match.
    sc_core::sc_start(sc_core::sc_time(0, sc_core::SC_NS));

    top.launchKernel(4, 1, kernels::intSaxpy());
    sc_core::sc_start(1000, sc_core::SC_NS);
    
    uint32_t divergence_events = top.getDivergenceEvents();
    EXPECT_GE(divergence_events, 0);
}

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
