// rodinia_real_benchmark.cpp — upstream Rodinia kernel smoke tests
//
// Validates the first real Rodinia CUDA kernel pair integrated through the
// selective dependency path:
//   - cuda/bfs/kernel.cu   -> Kernel
//   - cuda/bfs/kernel2.cu  -> Kernel2
//
// The upstream device code is wrapped with a CUDA-compat layer so it can run
// through the current RISC-V/SystemC path.

#include <gtest/gtest.h>

#include <cstdint>
#include <filesystem>

#include "kernel_bridge.h"
#include "../../driver/src/loader.h"

namespace fs = std::filesystem;
using namespace riscv_gpgpu;

#ifndef RODINIA_BFS_KERNEL_ELF
#define RODINIA_BFS_KERNEL_ELF ""
#endif

#ifndef RODINIA_BFS_KERNEL2_ELF
#define RODINIA_BFS_KERNEL2_ELF ""
#endif

struct RodiniaBfsNode {
    int starting;
    int no_of_edges;
};

static KernelBridge::Config makeBridgeConfig() {
    KernelBridge::Config cfg;
    cfg.num_compute_units = 1;
    cfg.threads_per_warp  = 1;
    cfg.max_warps_per_cu  = 1;
    cfg.shared_mem_size   = 32768;
    cfg.l1_cache_size     = 16384;
    cfg.l2_cache_size     = 262144;
    cfg.max_sim_cycles    = 100000;
    cfg.print_stats       = false;
    return cfg;
}

TEST(RodiniaRealBenchmark, BfsSingleNodeRoundTrip) {
    const fs::path bfs_kernel_elf  = RODINIA_BFS_KERNEL_ELF;
    const fs::path bfs_kernel2_elf = RODINIA_BFS_KERNEL2_ELF;

    if (bfs_kernel_elf.empty() || bfs_kernel2_elf.empty()) {
        GTEST_SKIP() << "Rodinia BFS kernels are not configured";
    }

    ASSERT_TRUE(fs::exists(bfs_kernel_elf)) << bfs_kernel_elf;
    ASSERT_TRUE(fs::exists(bfs_kernel2_elf)) << bfs_kernel2_elf;

    RodiniaBfsNode h_nodes[1] = {{0, 0}};
    int  h_edges[1]     = {0};
    bool h_mask[1]      = {true};
    bool h_updating[1]  = {false};
    bool h_visited[1]   = {true};
    int  h_cost[1]      = {0};
    bool h_over         = false;

    uint64_t d_nodes = 0;
    uint64_t d_edges = 0;
    uint64_t d_mask = 0;
    uint64_t d_updating = 0;
    uint64_t d_visited = 0;
    uint64_t d_cost = 0;
    uint64_t d_over = 0;

    ASSERT_TRUE(allocateDeviceBuffer(d_nodes, sizeof(h_nodes)));
    ASSERT_TRUE(allocateDeviceBuffer(d_edges, sizeof(h_edges)));
    ASSERT_TRUE(allocateDeviceBuffer(d_mask, sizeof(h_mask)));
    ASSERT_TRUE(allocateDeviceBuffer(d_updating, sizeof(h_updating)));
    ASSERT_TRUE(allocateDeviceBuffer(d_visited, sizeof(h_visited)));
    ASSERT_TRUE(allocateDeviceBuffer(d_cost, sizeof(h_cost)));
    ASSERT_TRUE(allocateDeviceBuffer(d_over, sizeof(h_over)));

    ASSERT_TRUE(copyHostToDevice(d_nodes, h_nodes, sizeof(h_nodes)));
    ASSERT_TRUE(copyHostToDevice(d_edges, h_edges, sizeof(h_edges)));
    ASSERT_TRUE(copyHostToDevice(d_mask, h_mask, sizeof(h_mask)));
    ASSERT_TRUE(copyHostToDevice(d_updating, h_updating, sizeof(h_updating)));
    ASSERT_TRUE(copyHostToDevice(d_visited, h_visited, sizeof(h_visited)));
    ASSERT_TRUE(copyHostToDevice(d_cost, h_cost, sizeof(h_cost)));
    ASSERT_TRUE(copyHostToDevice(d_over, &h_over, sizeof(h_over)));

    KernelLaunchArgs launch{};
    launch.kernel_name = "Kernel";
    launch.entry_symbol = "Kernel";
    launch.grid_x = 1;
    launch.grid_y = 1;
    launch.grid_z = 1;
    launch.block_x = 1;
    launch.block_y = 1;
    launch.block_z = 1;
    ASSERT_TRUE(configureLaunch(launch));

    KernelBridge bridge(makeBridgeConfig());
    ASSERT_TRUE(bridge.runOnHardware(
        "Kernel",
        bfs_kernel_elf.string(),
        {d_nodes, d_edges, d_mask, d_updating, d_visited, d_cost, 1},
        {d_nodes, d_edges, d_mask, d_updating, d_visited, d_cost, d_over}));

    launch.kernel_name = "Kernel2";
    launch.entry_symbol = "Kernel2";
    ASSERT_TRUE(configureLaunch(launch));

    ASSERT_TRUE(bridge.runOnHardware(
        "Kernel2",
        bfs_kernel2_elf.string(),
        {d_mask, d_updating, d_visited, d_over, 1},
        {d_mask, d_updating, d_visited, d_over}));

    ASSERT_TRUE(copyDeviceToHost(h_nodes, d_nodes, sizeof(h_nodes)));
    ASSERT_TRUE(copyDeviceToHost(h_edges, d_edges, sizeof(h_edges)));
    ASSERT_TRUE(copyDeviceToHost(h_mask, d_mask, sizeof(h_mask)));
    ASSERT_TRUE(copyDeviceToHost(h_updating, d_updating, sizeof(h_updating)));
    ASSERT_TRUE(copyDeviceToHost(h_visited, d_visited, sizeof(h_visited)));
    ASSERT_TRUE(copyDeviceToHost(h_cost, d_cost, sizeof(h_cost)));
    ASSERT_TRUE(copyDeviceToHost(&h_over, d_over, sizeof(h_over)));

    EXPECT_EQ(h_cost[0], 0);
    EXPECT_TRUE(h_visited[0]);
    EXPECT_FALSE(h_mask[0]);
    EXPECT_FALSE(h_updating[0]);
    EXPECT_FALSE(h_over);
}