// benchmark_test.cpp – RISC-V GPGPU Benchmark Suite
//
// Clean, focused benchmark runner using kernel_programs.h.
// Run with: make benchmark
//
// Benchmarks map to the paper's planned evaluation (Section IV):
//   B1  Integer SAXPY          – baseline, no memory, no divergence
//   B2  FP SAXPY (uniform)     – floating-point pipeline
//   B3  Memory round-trip      – L1/L2 cache hierarchy
//   B4  Divergent odd/even     – branch divergence overhead
//   B5  Multi-GPU integer SAXPY – SystemTop with 2 GPUs
//
// Register convention (buildWarpContext):
//   r0[t] = 0,  r1[t] = global_tid,  r2[t] = 0x10000 + tid*4
//

#include <systemc>
#include <iostream>
#include <iomanip>
#include <string>
#include <vector>

#include "top/top.h"
#include "system_top/system_top.h"
#include "common/platform.h"
#include "common/logging.h"
#include "common/kernel_programs.h"

using namespace riscv_gpgpu;
using namespace riscv_gpgpu::kernels;

// ── Per-benchmark metrics snapshot ───────────────────────────────────────────
struct Metrics {
    uint64_t instructions = 0;
    uint64_t l1_hits      = 0;
    uint64_t l1_misses    = 0;
    uint32_t divergence   = 0;

    float l1HitRate() const {
        uint64_t total = l1_hits + l1_misses;
        return total > 0 ? 100.0f * l1_hits / total : 0.0f;
    }

    Metrics operator-(const Metrics& o) const {
        return { instructions - o.instructions,
                 l1_hits      - o.l1_hits,
                 l1_misses    - o.l1_misses,
                 divergence   - o.divergence };
    }
};

static Metrics snapshot(const GPGPUTop& gpu) {
    return { gpu.getTotalInstructions(), gpu.getL1CacheHits(),
             gpu.getL1CacheMisses(),     gpu.getDivergenceEvents() };
}

static Metrics snapshotSys(const SystemTop& sys) {
    return { sys.getTotalInstructions(), sys.getL1CacheHits(),
             sys.getL1CacheMisses(),     sys.getDivergenceEvents() };
}

// ── Result table ──────────────────────────────────────────────────────────────
struct BenchmarkResult {
    std::string name;
    uint32_t    warps;
    uint32_t    threads_per_warp;
    Metrics     delta;
};

static void printTable(const std::vector<BenchmarkResult>& results) {
    std::cout << "\n"
        << "╔══════════════════════════════════════════════════════════════════╗\n"
        << "║  RISC-V GPGPU – Benchmark Report                               ║\n"
        << "╠═══════════════════════════╦═══════╦════════╦══════════╦════════╣\n"
        << "║  Benchmark                ║ Warps ║ Instrs ║ L1 Hit%  ║  Div  ║\n"
        << "╠═══════════════════════════╬═══════╬════════╬══════════╬════════╣\n";

    for (const auto& r : results) {
        std::cout << "║  " << std::left  << std::setw(25) << r.name
                  << "║  " << std::right << std::setw(4)  << r.warps
                  << " ║  " << std::setw(5)  << r.delta.instructions
                  << " ║   "  << std::fixed << std::setprecision(1)
                              << std::setw(5) << r.delta.l1HitRate() << "%"
                  << "  ║  " << std::setw(4)  << r.delta.divergence
                  << " ║\n";
    }
    std::cout
        << "╚═══════════════════════════╩═══════╩════════╩══════════╩════════╝\n\n";
}

// ── sc_main ───────────────────────────────────────────────────────────────────
int sc_main(int /*argc*/, char* /*argv*/[]) {
    // Suppress INFO logs for a cleaner benchmark output
    Logger::instance().setLogLevel(LogLevel::WARNING);

    Platform::printSimulationBanner();
    std::cout << "  Running benchmark suite...\n\n";

    // ── Module instantiation (all before sc_start) ────────────────────────────
    GPGPUTop::Config gpu_config;
    gpu_config.num_compute_units = 1;
    gpu_config.max_warps_per_cu  = 8;
    gpu_config.threads_per_warp  = 32;
    gpu_config.shared_mem_size   = 16 * 1024;
    gpu_config.l1_cache_size     = 32 * 1024;
    gpu_config.l2_cache_size     = 512 * 1024;
    GPGPUTop gpu("gpu", gpu_config);

    SystemTop::Config sys_config;
    sys_config.num_gpus   = 2;
    sys_config.gpu_config = gpu_config;
    SystemTop sys("sys", sys_config);

    sc_core::sc_start(sc_core::sc_time(0, sc_core::SC_NS));

    std::vector<BenchmarkResult> results;

    // ── B1: Integer SAXPY ─────────────────────────────────────────────────────
    {
        auto before = snapshot(gpu);
        gpu.launchKernel(4, 1, intSaxpy(2, 10));
        sc_core::sc_start(sc_core::sc_time(100, sc_core::SC_NS));
        results.push_back({"B1 Int SAXPY", 4, 32, snapshot(gpu) - before});
    }

    // ── B2: FP SAXPY (uniform) ────────────────────────────────────────────────
    {
        auto before = snapshot(gpu);
        gpu.launchKernel(4, 1, fpUniformSaxpy(2.0f, 3.0f, 1.0f));
        sc_core::sc_start(sc_core::sc_time(100, sc_core::SC_NS));
        results.push_back({"B2 FP SAXPY (uniform)", 4, 32, snapshot(gpu) - before});
    }

    // ── B3: Memory round-trip (L1/L2 cache) ──────────────────────────────────
    {
        auto before = snapshot(gpu);
        gpu.launchKernel(2, 1, memoryRoundTrip());
        sc_core::sc_start(sc_core::sc_time(100, sc_core::SC_NS));
        results.push_back({"B3 Memory round-trip", 2, 32, snapshot(gpu) - before});
    }

    // ── B4: Divergent odd/even ────────────────────────────────────────────────
    {
        auto before = snapshot(gpu);
        gpu.launchKernel(4, 1, divergentOddEven());
        sc_core::sc_start(sc_core::sc_time(100, sc_core::SC_NS));
        results.push_back({"B4 Divergent odd/even", 4, 32, snapshot(gpu) - before});
    }

    // ── B5: Multi-GPU integer SAXPY ───────────────────────────────────────────
    {
        auto before = snapshotSys(sys);
        sys.launchKernel(8, 1, intSaxpy(2, 10));   // 4 warps per GPU
        sc_core::sc_start(sc_core::sc_time(100, sc_core::SC_NS));
        results.push_back({"B5 Multi-GPU SAXPY (2x)", 8, 32, snapshotSys(sys) - before});
    }

    // ── Report ────────────────────────────────────────────────────────────────
    printTable(results);

    // Per-GPU breakdown for B5
    std::cout << "  B5 per-GPU breakdown:\n"
              << "    GPU 0: " << sys.getGPU(0).getTotalInstructions() << " total instructions\n"
              << "    GPU 1: " << sys.getGPU(1).getTotalInstructions() << " total instructions\n\n";

    return 0;
}