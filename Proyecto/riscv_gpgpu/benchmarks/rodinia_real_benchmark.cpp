// rodinia_real_benchmark.cpp — upstream Rodinia kernel smoke tests
//
// Validates real Rodinia CUDA kernels integrated through the selective
// dependency path:
//   - cuda/bfs/kernel.cu   -> Kernel
//   - cuda/bfs/kernel2.cu  -> Kernel2
//
// The upstream device code is wrapped with a CUDA-compat layer so it can run
// through the current RISC-V/SystemC path.

#include <gtest/gtest.h>

#include <cstdlib>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <limits>
#include <string>
#include <vector>

#include "kernel_bridge.h"
#include "elf_loader.h"
#include "../models/systemc/src/memory/memory_hierarchy.h"
#include "../../driver/src/loader.h"

namespace fs = std::filesystem;
using namespace riscv_gpgpu;

#ifndef RODINIA_BFS_KERNEL_ELF
#define RODINIA_BFS_KERNEL_ELF ""
#endif

#ifndef RODINIA_BFS_KERNEL2_ELF
#define RODINIA_BFS_KERNEL2_ELF ""
#endif

#ifndef RODINIA_GAUSSIAN_FAN1_ELF
#define RODINIA_GAUSSIAN_FAN1_ELF ""
#endif

#ifndef RODINIA_GAUSSIAN_FAN2_ELF
#define RODINIA_GAUSSIAN_FAN2_ELF ""
#endif

struct RodiniaBfsNode {
    int starting;
    int no_of_edges;
};

static uint32_t parseEnvU32(const char* name, uint32_t fallback) {
    if (const char* value = std::getenv(name)) {
        char* end = nullptr;
        unsigned long parsed = std::strtoul(value, &end, 10);
        if (end != value && *end == '\0' && parsed > 0
            && parsed <= std::numeric_limits<uint32_t>::max()) {
            return static_cast<uint32_t>(parsed);
        }
    }
    return fallback;
}

static bool envEnabled(const char* name) {
    if (const char* value = std::getenv(name)) {
        return std::string(value) == "1";
    }
    return false;
}

static void printBridgeStats(const char* tag, const KernelBridge& bridge) {
    std::cout << "[stress-debug] " << tag
              << " cycles=" << bridge.lastTotalCycles()
              << " instr=" << bridge.lastTotalInstructions()
              << " l1_hits=" << bridge.lastL1Hits()
              << " l1_misses=" << bridge.lastL1Misses()
              << " div=" << bridge.lastDivergenceEvents()
              << " grid=" << bridge.lastGridX() << "x" << bridge.lastGridY() << "x" << bridge.lastGridZ()
              << " block=" << bridge.lastBlockX() << "x" << bridge.lastBlockY() << "x" << bridge.lastBlockZ()
              << "\n";
}

static void printBfsState(const int* cost,
                          const uint8_t* visited,
                          const uint8_t* mask,
                          const uint8_t* updating,
                          int count,
                          bool over) {
    std::cout << "[stress-debug] state over=" << over << "\n";
    for (int i = 0; i < count; ++i) {
        std::cout << "[stress-debug] node=" << i
                  << " cost=" << cost[i]
                  << " visited=" << static_cast<unsigned>(visited[i])
                  << " mask=" << static_cast<unsigned>(mask[i])
                  << " updating=" << static_cast<unsigned>(updating[i])
                  << "\n";
    }
}

struct BfsFanoutGraph {
    std::vector<RodiniaBfsNode> nodes;
    std::vector<int> edges;
    std::vector<uint8_t> mask;
    std::vector<uint8_t> updating;
    std::vector<uint8_t> visited;
    std::vector<int> cost;
};

static BfsFanoutGraph buildBfsFanoutGraph(uint32_t nodes_count, uint32_t fanout_count) {
    BfsFanoutGraph graph;
    graph.nodes.resize(nodes_count);
    graph.edges.resize(fanout_count);
    graph.mask.resize(nodes_count, 0);
    graph.updating.resize(nodes_count, 0);
    graph.visited.resize(nodes_count, 0);
    graph.cost.resize(nodes_count, -1);

    if (nodes_count == 0) {
        return graph;
    }

    graph.nodes[0] = {0, static_cast<int>(fanout_count)};
    graph.mask[0] = 1;
    graph.visited[0] = 1;
    graph.cost[0] = 0;

    for (uint32_t i = 0; i < fanout_count; ++i) {
        graph.edges[i] = static_cast<int>(i + 1);
    }

    for (uint32_t node = 1; node < nodes_count; ++node) {
        graph.nodes[node] = {static_cast<int>(fanout_count), 0};
    }

    return graph;
}

static KernelBridge::Config makeBridgeConfig(uint32_t num_compute_units = 1,
                                             uint32_t max_sim_cycles = 100000) {
    KernelBridge::Config cfg;
    cfg.num_compute_units = num_compute_units;
    cfg.threads_per_warp  = 1;
    cfg.max_warps_per_cu  = 1;
    cfg.shared_mem_size   = 32768;
    cfg.l1_cache_size     = 16384;
    cfg.l2_cache_size     = 262144;
    cfg.max_sim_cycles    = max_sim_cycles;
    cfg.print_stats       = false;
    return cfg;
}

static bool elfHasSymbol(const fs::path& elf_path, const std::string& symbol_name) {
    MemoryHierarchy::Config mem_cfg;
    mem_cfg.shared_mem_size = 4096;
    mem_cfg.global_mem_size = 0;
    mem_cfg.cache_line_size = 64;
    mem_cfg.l1_cache_size = 16384;
    mem_cfg.l2_cache_size = 262144;

    MemoryHierarchy mem("rodinia_real_symbol_mem", mem_cfg);
    ElfLoader loader;
    if (!loader.load(elf_path.string(), mem)) {
        return false;
    }

    SymbolEntry sym;
    return loader.findSymbol(symbol_name, sym);
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

TEST(RodiniaRealBenchmark, BfsKernelSymbolsPresent) {
    const fs::path bfs_kernel_elf  = RODINIA_BFS_KERNEL_ELF;
    const fs::path bfs_kernel2_elf = RODINIA_BFS_KERNEL2_ELF;

    if (bfs_kernel_elf.empty() || bfs_kernel2_elf.empty()) {
        GTEST_SKIP() << "Rodinia BFS kernels are not configured";
    }

    ASSERT_TRUE(fs::exists(bfs_kernel_elf)) << bfs_kernel_elf;
    ASSERT_TRUE(fs::exists(bfs_kernel2_elf)) << bfs_kernel2_elf;
    EXPECT_TRUE(elfHasSymbol(bfs_kernel_elf, "Kernel"));
    EXPECT_TRUE(elfHasSymbol(bfs_kernel2_elf, "Kernel2"));
}

TEST(RodiniaRealBenchmark, BfsFanoutStress) {
    if (!envEnabled("RODINIA_REAL_STRESS")) {
        GTEST_SKIP() << "Set RODINIA_REAL_STRESS=1 to run the BFS fanout stress test";
    }

    const uint32_t stress_max_cycles = parseEnvU32("RODINIA_REAL_STRESS_MAX_CYCLES", 500000);
    const uint32_t stress_cus = parseEnvU32("RODINIA_REAL_STRESS_CUS", 5);
    const uint32_t stress_threads = parseEnvU32("RODINIA_REAL_STRESS_THREADS", 5);
    const uint32_t stress_nodes = parseEnvU32("RODINIA_REAL_STRESS_NODES", 5);
    uint32_t stress_fanout = parseEnvU32("RODINIA_REAL_STRESS_FANOUT", stress_nodes > 0 ? stress_nodes - 1 : 0);
    const bool stress_debug = envEnabled("RODINIA_REAL_STRESS_DEBUG");

    if (stress_nodes < 2) {
        GTEST_SKIP() << "RODINIA_REAL_STRESS_NODES must be at least 2";
    }

    if (stress_fanout > stress_nodes - 1) {
        stress_fanout = stress_nodes - 1;
    }

    const fs::path bfs_kernel_elf  = RODINIA_BFS_KERNEL_ELF;
    const fs::path bfs_kernel2_elf = RODINIA_BFS_KERNEL2_ELF;

    if (bfs_kernel_elf.empty() || bfs_kernel2_elf.empty()) {
        GTEST_SKIP() << "Rodinia BFS kernels are not configured";
    }

    ASSERT_TRUE(fs::exists(bfs_kernel_elf)) << bfs_kernel_elf;
    ASSERT_TRUE(fs::exists(bfs_kernel2_elf)) << bfs_kernel2_elf;

    BfsFanoutGraph graph = buildBfsFanoutGraph(stress_nodes, stress_fanout);
    bool h_over = false;

    uint64_t d_nodes = 0;
    uint64_t d_edges = 0;
    uint64_t d_mask = 0;
    uint64_t d_updating = 0;
    uint64_t d_visited = 0;
    uint64_t d_cost = 0;
    uint64_t d_over = 0;

    ASSERT_TRUE(allocateDeviceBuffer(d_nodes, graph.nodes.size() * sizeof(RodiniaBfsNode)));
    ASSERT_TRUE(allocateDeviceBuffer(d_edges, graph.edges.size() * sizeof(int)));
    ASSERT_TRUE(allocateDeviceBuffer(d_mask, graph.mask.size() * sizeof(uint8_t)));
    ASSERT_TRUE(allocateDeviceBuffer(d_updating, graph.updating.size() * sizeof(uint8_t)));
    ASSERT_TRUE(allocateDeviceBuffer(d_visited, graph.visited.size() * sizeof(uint8_t)));
    ASSERT_TRUE(allocateDeviceBuffer(d_cost, graph.cost.size() * sizeof(int)));
    ASSERT_TRUE(allocateDeviceBuffer(d_over, sizeof(h_over)));

    ASSERT_TRUE(copyHostToDevice(d_nodes, graph.nodes.data(), graph.nodes.size() * sizeof(RodiniaBfsNode)));
    ASSERT_TRUE(copyHostToDevice(d_edges, graph.edges.data(), graph.edges.size() * sizeof(int)));
    ASSERT_TRUE(copyHostToDevice(d_mask, graph.mask.data(), graph.mask.size() * sizeof(uint8_t)));
    ASSERT_TRUE(copyHostToDevice(d_updating, graph.updating.data(), graph.updating.size() * sizeof(uint8_t)));
    ASSERT_TRUE(copyHostToDevice(d_visited, graph.visited.data(), graph.visited.size() * sizeof(uint8_t)));
    ASSERT_TRUE(copyHostToDevice(d_cost, graph.cost.data(), graph.cost.size() * sizeof(int)));
    ASSERT_TRUE(copyHostToDevice(d_over, &h_over, sizeof(h_over)));

    KernelLaunchArgs launch{};
    launch.kernel_name = "Kernel";
    launch.entry_symbol = "Kernel";
    launch.grid_x = 1;
    launch.grid_y = 1;
    launch.grid_z = 1;
    launch.block_x = stress_threads;
    launch.block_y = 1;
    launch.block_z = 1;
    ASSERT_TRUE(configureLaunch(launch));

    if (stress_debug) {
        std::cout << "[stress-debug] config cus=" << stress_cus
                  << " threads=" << stress_threads
                  << " max_cycles=" << stress_max_cycles << "\n";
    }

    KernelBridge bridge(makeBridgeConfig(stress_cus, stress_max_cycles));
    ASSERT_TRUE(bridge.runOnHardware(
        "Kernel",
        bfs_kernel_elf.string(),
        {d_nodes, d_edges, d_mask, d_updating, d_visited, d_cost, stress_nodes},
        {d_nodes, d_edges, d_mask, d_updating, d_visited, d_cost, d_over}));

    if (stress_debug) {
        printBridgeStats("Kernel", bridge);
    }

    launch.kernel_name = "Kernel2";
    launch.entry_symbol = "Kernel2";
    ASSERT_TRUE(configureLaunch(launch));

    ASSERT_TRUE(bridge.runOnHardware(
        "Kernel2",
        bfs_kernel2_elf.string(),
        {d_mask, d_updating, d_visited, d_over, stress_nodes},
        {d_mask, d_updating, d_visited, d_over}));

    if (stress_debug) {
        printBridgeStats("Kernel2", bridge);
    }

    ASSERT_TRUE(copyDeviceToHost(graph.mask.data(), d_mask, graph.mask.size() * sizeof(uint8_t)));
    ASSERT_TRUE(copyDeviceToHost(graph.updating.data(), d_updating, graph.updating.size() * sizeof(uint8_t)));
    ASSERT_TRUE(copyDeviceToHost(graph.visited.data(), d_visited, graph.visited.size() * sizeof(uint8_t)));
    ASSERT_TRUE(copyDeviceToHost(graph.cost.data(), d_cost, graph.cost.size() * sizeof(int)));
    ASSERT_TRUE(copyDeviceToHost(&h_over, d_over, sizeof(h_over)));

    if (stress_debug) {
        std::cout << "[stress-debug] graph nodes=" << stress_nodes
                  << " fanout=" << stress_fanout
                  << " cus=" << stress_cus
                  << " threads=" << stress_threads
                  << " max_cycles=" << stress_max_cycles << "\n";
        printBfsState(graph.cost.data(), graph.visited.data(), graph.mask.data(), graph.updating.data(),
                      static_cast<int>(stress_nodes), h_over);
    }

    for (uint32_t i = 1; i <= stress_fanout; ++i) {
        EXPECT_EQ(graph.cost[i], 1) << "node " << i;
        EXPECT_EQ(graph.visited[i], 1u) << "node " << i;
        EXPECT_EQ(graph.mask[i], 1u) << "node " << i;
        EXPECT_EQ(graph.updating[i], 0u) << "node " << i;
    }
    for (uint32_t i = stress_fanout + 1; i < stress_nodes; ++i) {
        EXPECT_EQ(graph.cost[i], -1) << "node " << i;
        EXPECT_EQ(graph.visited[i], 0u) << "node " << i;
        EXPECT_EQ(graph.mask[i], 0u) << "node " << i;
        EXPECT_EQ(graph.updating[i], 0u) << "node " << i;
    }
    EXPECT_TRUE(h_over);
}

TEST(RodiniaRealBenchmark, GaussianKernelSymbolsPresent) {
    const fs::path fan1_elf = RODINIA_GAUSSIAN_FAN1_ELF;
    const fs::path fan2_elf = RODINIA_GAUSSIAN_FAN2_ELF;

    if (fan1_elf.empty() || fan2_elf.empty()) {
        GTEST_SKIP() << "Rodinia Gaussian kernels are not configured";
    }

    ASSERT_TRUE(fs::exists(fan1_elf)) << fan1_elf;
    ASSERT_TRUE(fs::exists(fan2_elf)) << fan2_elf;
    EXPECT_TRUE(elfHasSymbol(fan1_elf, "Fan1"));
    EXPECT_TRUE(elfHasSymbol(fan2_elf, "Fan2"));
}

TEST(RodiniaRealBenchmark, GaussianFanSmoke) {
    const fs::path fan1_elf = RODINIA_GAUSSIAN_FAN1_ELF;
    const fs::path fan2_elf = RODINIA_GAUSSIAN_FAN2_ELF;

    if (fan1_elf.empty() || fan2_elf.empty()) {
        GTEST_SKIP() << "Rodinia Gaussian kernels are not configured";
    }

    ASSERT_TRUE(fs::exists(fan1_elf)) << fan1_elf;
    ASSERT_TRUE(fs::exists(fan2_elf)) << fan2_elf;

    constexpr int size = 2;
    float h_a[size * size] = {
        2.0f, 1.0f,
        3.0f, 4.0f,
    };
    float h_m[size * size] = {0.0f, 0.0f, 0.0f, 0.0f};
    float h_b[size] = {1.0f, 2.0f};

    uint64_t d_a = 0;
    uint64_t d_m = 0;
    uint64_t d_b = 0;

    ASSERT_TRUE(allocateDeviceBuffer(d_a, sizeof(h_a)));
    ASSERT_TRUE(allocateDeviceBuffer(d_m, sizeof(h_m)));
    ASSERT_TRUE(allocateDeviceBuffer(d_b, sizeof(h_b)));

    ASSERT_TRUE(copyHostToDevice(d_a, h_a, sizeof(h_a)));
    ASSERT_TRUE(copyHostToDevice(d_m, h_m, sizeof(h_m)));
    ASSERT_TRUE(copyHostToDevice(d_b, h_b, sizeof(h_b)));

    KernelLaunchArgs launch{};
    launch.kernel_name = "Fan1";
    launch.entry_symbol = "Fan1";
    launch.grid_x = 1;
    launch.grid_y = 1;
    launch.grid_z = 1;
    launch.block_x = 1;
    launch.block_y = 1;
    launch.block_z = 1;
    ASSERT_TRUE(configureLaunch(launch));

    KernelBridge bridge(makeBridgeConfig());
    ASSERT_TRUE(bridge.runOnHardware(
        "Fan1",
        fan1_elf.string(),
        {d_m, d_a, size, 0},
        {d_m}));

    launch.kernel_name = "Fan2";
    launch.entry_symbol = "Fan2";
    ASSERT_TRUE(configureLaunch(launch));

    ASSERT_TRUE(bridge.runOnHardware(
        "Fan2",
        fan2_elf.string(),
        {d_m, d_a, d_b, size, size, 0},
        {d_m, d_a, d_b}));

    ASSERT_TRUE(copyDeviceToHost(h_a, d_a, sizeof(h_a)));
    ASSERT_TRUE(copyDeviceToHost(h_m, d_m, sizeof(h_m)));
    ASSERT_TRUE(copyDeviceToHost(h_b, d_b, sizeof(h_b)));

    EXPECT_GT(bridge.lastTotalCycles(), 0u);
    EXPECT_GT(bridge.lastTotalInstructions(), 0u);
    EXPECT_FLOAT_EQ(h_a[0], 2.0f);
    EXPECT_FLOAT_EQ(h_b[0], 1.0f);
}