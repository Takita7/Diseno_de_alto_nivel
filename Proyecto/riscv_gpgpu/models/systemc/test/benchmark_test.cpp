// benchmark_test.cpp – RISC-V GPGPU Stress Benchmark Suite
//
// Full-system benchmark using SystemTop with 5 GPUs.
// All metrics are from a functional/TLM model — not cycle-accurate.
//

#include <systemc>
#include <iostream>
#include <iomanip>
#include <sstream>
#include <vector>
#include <string>

#include "top/top.h"
#include "system_top/system_top.h"
#include "common/platform.h"
#include "common/logging.h"
#include "common/kernel_programs.h"

using namespace riscv_gpgpu;
using namespace riscv_gpgpu::kernels;

static constexpr uint32_t NUM_GPUS         = 5;
static constexpr uint32_t THREADS_PER_WARP = 32;
static constexpr uint32_t CUS_PER_GPU      = 1;

// ── Metrics ───────────────────────────────────────────────────────────────────
struct Metrics {
    uint64_t instructions = 0;
    uint64_t l1_hits      = 0;
    uint64_t l1_misses    = 0;
    uint32_t divergence   = 0;
    Metrics operator-(const Metrics& o) const {
        return { instructions-o.instructions, l1_hits-o.l1_hits,
                 l1_misses-o.l1_misses,       divergence-o.divergence };
    }
    float l1HitRate() const {
        uint64_t t = l1_hits + l1_misses;
        return t > 0 ? 100.0f * l1_hits / t : 0.0f;
    }
};

static Metrics snap(const GPGPUTop& g) {
    return { g.getTotalInstructions(), g.getL1CacheHits(),
             g.getL1CacheMisses(),     g.getDivergenceEvents() };
}
static Metrics snapSys(const SystemTop& s) {
    return { s.getTotalInstructions(), s.getL1CacheHits(),
             s.getL1CacheMisses(),     s.getDivergenceEvents() };
}
static std::vector<Metrics> snapAll(const SystemTop& s) {
    std::vector<Metrics> v;
    for (uint32_t i = 0; i < s.getNumGPUs(); ++i) v.push_back(snap(s.getGPU(i)));
    return v;
}

// ── Benchmark result ──────────────────────────────────────────────────────────
struct BResult {
    std::string label;
    uint32_t warps, gpus;
    Metrics  delta;
    uint64_t totalThreads() const { return warps * THREADS_PER_WARP; }
};

// ── Print helpers ─────────────────────────────────────────────────────────────
static void sep(char c = '-', int w = 78) { std::cout << std::string(w,c) << "\n"; }

static void printSection(const std::string& name) {
    std::cout << "\n";
    sep('=');
    std::cout << "  " << name << "\n";
    sep('=');
}

static void printPerGPU(const SystemTop& sys,
                        const std::vector<Metrics>& before) {
    for (uint32_t i = 0; i < sys.getNumGPUs(); ++i) {
        Metrics d = snap(sys.getGPU(i)) - before[i];
        std::cout
            << "    GPU " << i << " │ "
            << std::setw(5) << d.instructions << " instr │ "
            << "L1 hits: "   << std::setw(4) << d.l1_hits   << " │ "
            << "L1 misses: " << std::setw(4) << d.l1_misses << " │ "
            << "div: "       << d.divergence  << "\n";
    }
}

static void printBenchTable(const std::vector<BResult>& r) {
    sep('=');
    std::cout
        << std::left  << "  " << std::setw(38) << "Benchmark"
        << std::right << std::setw(5)  << "GPUs"
                      << std::setw(7)  << "Warps"
                      << std::setw(8)  << "Threads"
                      << std::setw(8)  << "Instrs"
                      << std::setw(10) << "L1 Hit%"
                      << std::setw(6)  << "Div\n";
    sep();
    for (const auto& b : r) {
        std::cout
            << std::left  << "  " << std::setw(38) << b.label
            << std::right << std::setw(5)  << b.gpus
                          << std::setw(7)  << b.warps
                          << std::setw(8)  << b.totalThreads()
                          << std::setw(8)  << b.delta.instructions
                          << std::setw(9)  << std::fixed << std::setprecision(1)
                                           << b.delta.l1HitRate() << "%"
                          << std::setw(6)  << b.delta.divergence << "\n";
    }
    sep('=');
}

// ── sc_main ───────────────────────────────────────────────────────────────────
int sc_main(int /*argc*/, char* /*argv*/[]) {
    Logger::instance().setLogLevel(LogLevel::WARNING);

    GPGPUTop::Config gpu_config;
    gpu_config.num_compute_units = CUS_PER_GPU;
    gpu_config.max_warps_per_cu  = 4;
    gpu_config.threads_per_warp  = THREADS_PER_WARP;
    gpu_config.shared_mem_size   = 16 * 1024;
    gpu_config.l1_cache_size     = 32 * 1024;
    gpu_config.l2_cache_size     = 512 * 1024;

    GPGPUTop  single_gpu("single_gpu", gpu_config);

    SystemTop::Config sys_config;
    sys_config.num_gpus   = NUM_GPUS;
    sys_config.gpu_config = gpu_config;
    SystemTop sys("sys", sys_config);

    sc_core::sc_start(sc_core::sc_time(0, sc_core::SC_NS));

    // ── Config banner ─────────────────────────────────────────────────────────
    sep('=');
    std::cout
        << "  RISC-V GPGPU SystemC Functional Model - Stress Benchmark\n"
        << "  NOTE: instruction counts and cache stats are exact;\n"
        << "        timing is not cycle-accurate (functional model).\n";
    sep('=');
    std::cout
        << "  GPUs:             " << NUM_GPUS << "\n"
        << "  CUs per GPU:      " << CUS_PER_GPU << "\n"
        << "  Threads per warp: " << THREADS_PER_WARP << "\n"
        << "  Threads per GPU:  " << (CUS_PER_GPU * THREADS_PER_WARP * 4) << "\n"
        << "  L1 cache:         32 KB (write-through, no-write-allocate)\n"
        << "  L2 cache:         512 KB\n"
        << "  Shared memory:    16 KB\n";
    sep('=');

    std::vector<BResult> results;
    uint32_t t_ns = 0;
    auto advance = [&](uint32_t ns = 200) {
        sc_core::sc_start(sc_core::sc_time(ns, sc_core::SC_NS));
        t_ns += ns;
    };

    // ─────────────────────────────────────────────────────────────────────────
    printSection("B1  Integer SAXPY   (20 warps, 4 per GPU, alpha=3, y=7)");
    {
        auto before    = snapSys(sys);
        auto before_pu = snapAll(sys);
        sys.launchKernel(20, 1, intSaxpy(3, 7));
        advance();
        auto delta = snapSys(sys) - before;
        printPerGPU(sys, before_pu);
        results.push_back({"B1  Int SAXPY (a=3, y=7)", 20, NUM_GPUS, delta});
    }

    // ─────────────────────────────────────────────────────────────────────────
    printSection("B2  FP SAXPY uniform   (20 warps, 4 per GPU, alpha=2.5)");
    {
        auto before    = snapSys(sys);
        auto before_pu = snapAll(sys);
        sys.launchKernel(20, 1, fpUniformSaxpy(2.5f, 4.0f, 10.0f));
        advance();
        auto delta = snapSys(sys) - before;
        printPerGPU(sys, before_pu);
        results.push_back({"B2  FP SAXPY (a=2.5, x=4.0, y=10.0)", 20, NUM_GPUS, delta});
    }

    // ─────────────────────────────────────────────────────────────────────────
    printSection("B3  Memory round-trip  (10 warps, 2 per GPU, SW+LW+LW)");
    {
        auto before    = snapSys(sys);
        auto before_pu = snapAll(sys);
        sys.launchKernel(10, 1, memoryRoundTrip());
        advance();
        auto delta = snapSys(sys) - before;
        printPerGPU(sys, before_pu);
        results.push_back({"B3  Memory round-trip (SW+LW+LW)", 10, NUM_GPUS, delta});
    }

    // ─────────────────────────────────────────────────────────────────────────
    printSection("B4  Divergent odd/even (20 warps, 4 per GPU)");
    {
        auto before    = snapSys(sys);
        auto before_pu = snapAll(sys);
        sys.launchKernel(20, 1, divergentOddEven());
        advance();
        auto delta = snapSys(sys) - before;
        printPerGPU(sys, before_pu);
        results.push_back({"B4  Divergent odd/even", 20, NUM_GPUS, delta});
    }

    // ─────────────────────────────────────────────────────────────────────────
    printSection("B5  Barrier round-trip (10 warps, 2 per GPU, BARRIER+SW+LW)");
    {
        auto before    = snapSys(sys);
        auto before_pu = snapAll(sys);
        sys.launchKernel(10, 1, barrierRoundTrip(0));
        advance();
        auto delta = snapSys(sys) - before;
        printPerGPU(sys, before_pu);
        results.push_back({"B5  Barrier sync (BARRIER+SW+LW)", 10, NUM_GPUS, delta});
    }

    // ─────────────────────────────────────────────────────────────────────────
    printSection("SCALABILITY  Integer SAXPY: 1 GPU vs 5 GPUs");
    {
        auto sg_before = snap(single_gpu);
        single_gpu.launchKernel(4, 1, intSaxpy(3, 7));
        advance();
        auto sg = snap(single_gpu) - sg_before;

        auto ms_before    = snapSys(sys);
        auto ms_before_pu = snapAll(sys);
        sys.launchKernel(20, 1, intSaxpy(3, 7));
        advance();
        auto ms = snapSys(sys) - ms_before;

        sep();
        std::cout
            << "  Config            Warps   Threads   Instrs   Throughput\n";
        sep();
        std::cout
            << "  1 GPU (baseline)     4       128     " << std::setw(5) << sg.instructions
            << "       1.00x\n"
            << "  5 GPUs (scaled)     20       640     " << std::setw(5) << ms.instructions
            << "       " << std::fixed << std::setprecision(2)
            << (sg.instructions > 0 ? float(ms.instructions)/sg.instructions : 0.f) << "x\n";
        sep();
        std::cout << "\n  Per-GPU (5-GPU run):\n";
        for (uint32_t i = 0; i < sys.getNumGPUs(); ++i) {
            auto d = snap(sys.getGPU(i)) - ms_before_pu[i];
            std::cout << "    GPU " << i << ": " << d.instructions
                      << " instr  (" << THREADS_PER_WARP*4 << " threads)\n";
        }
    }

    // ─────────────────────────────────────────────────────────────────────────
    std::cout << "\n\n";
    printSection("BENCHMARK SUMMARY (B1-B5, 5-GPU system)");
    printBenchTable(results);

    uint64_t tot_i=0, tot_h=0, tot_m=0; uint32_t tot_d=0;
    for (auto& r : results) {
        tot_i+=r.delta.instructions; tot_h+=r.delta.l1_hits;
        tot_m+=r.delta.l1_misses;   tot_d+=r.delta.divergence;
    }
    float hit = (tot_h+tot_m)>0 ? 100.f*tot_h/(tot_h+tot_m) : 0.f;

    std::cout
        << "\n  TOTALS\n";
    sep();
    std::cout
        << "  Instructions executed  : " << tot_i << "\n"
        << "  L1 cache hits          : " << tot_h << "\n"
        << "  L1 cache misses        : " << tot_m << "\n"
        << "  Overall L1 hit rate    : " << std::fixed << std::setprecision(1) << hit << "%\n"
        << "  Divergence events      : " << tot_d << "  (out of 80 total warps, "
        << std::setprecision(1) << (80>0?100.f*tot_d/80:0.f) << "% warp-div rate)\n";
    sep('=');
    std::cout << "\n";

    return 0;
}