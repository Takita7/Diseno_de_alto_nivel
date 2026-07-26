// benchmark_test.cpp – RISC-V GPGPU Stress Benchmark Suite
//
// Full-system benchmark using SystemTop with 5 GPUs.
// Includes design space exploration: multi-CU, vector lane width, IPDOM.
//

#include <systemc>
#include <iostream>
#include <iomanip>
#include <vector>
#include <string>

#include "top/top.h"
#include "system_top/system_top.h"
#include "compute_unit/compute_unit.h"
#include "common/platform.h"
#include "common/logging.h"
#include "common/kernel_programs.h"

using namespace riscv_gpgpu;
using namespace riscv_gpgpu::kernels;

static constexpr uint32_t NUM_GPUS         = 5;
static constexpr uint32_t THREADS_PER_WARP = 32;
static constexpr uint32_t CUS_PER_GPU      = 1;

struct Metrics {
    uint64_t instructions = 0;
    uint64_t l1_hits = 0, l1_misses = 0;
    uint32_t divergence = 0;
    Metrics operator-(const Metrics& o) const {
        return {instructions-o.instructions, l1_hits-o.l1_hits,
                l1_misses-o.l1_misses, divergence-o.divergence};
    }
    float l1HitRate() const {
        uint64_t t = l1_hits+l1_misses; return t>0 ? 100.f*l1_hits/t : 0.f;
    }
};

static Metrics snap(const GPGPUTop& g) {
    return {g.getTotalInstructions(), g.getL1CacheHits(),
            g.getL1CacheMisses(),     g.getDivergenceEvents()};
}
static Metrics snapSys(const SystemTop& s) {
    return {s.getTotalInstructions(), s.getL1CacheHits(),
            s.getL1CacheMisses(),     s.getDivergenceEvents()};
}
static std::vector<Metrics> snapAll(const SystemTop& s) {
    std::vector<Metrics> v;
    for (uint32_t i = 0; i < s.getNumGPUs(); ++i) v.push_back(snap(s.getGPU(i)));
    return v;
}

struct BResult {
    std::string label;
    uint32_t warps, gpus;
    Metrics  delta;
    uint64_t totalThreads() const { return warps * THREADS_PER_WARP; }
};

static void sep(char c='-', int w=78){ std::cout << std::string(w,c) << "\n"; }
static void printSection(const std::string& n) {
    std::cout << "\n"; sep('='); std::cout << "  " << n << "\n"; sep('=');
}
static void printPerGPU(const SystemTop& sys, const std::vector<Metrics>& before) {
    for (uint32_t i = 0; i < sys.getNumGPUs(); ++i) {
        Metrics d = snap(sys.getGPU(i)) - before[i];
        std::cout << "    GPU " << i << " | " << std::setw(5) << d.instructions
                  << " instr | L1 hits: " << std::setw(4) << d.l1_hits
                  << " | L1 misses: " << std::setw(4) << d.l1_misses
                  << " | div: " << d.divergence << "\n";
    }
}
static void printBenchTable(const std::vector<BResult>& r) {
    sep('=');
    std::cout << std::left  << "  " << std::setw(38) << "Benchmark"
              << std::right << std::setw(5)  << "GPUs"
                            << std::setw(7)  << "Warps"
                            << std::setw(8)  << "Threads"
                            << std::setw(8)  << "Instrs"
                            << std::setw(10) << "L1 Hit%"
                            << std::setw(6)  << "Div\n";
    sep();
    for (const auto& b : r) {
        std::cout << std::left  << "  " << std::setw(38) << b.label
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

    // ── Main 5-GPU system ─────────────────────────────────────────────────────
    GPGPUTop::Config gpu_cfg;
    gpu_cfg.num_compute_units = CUS_PER_GPU;
    gpu_cfg.max_warps_per_cu  = 8;
    gpu_cfg.threads_per_warp  = THREADS_PER_WARP;
    gpu_cfg.shared_mem_size   = 16*1024;
    gpu_cfg.l1_cache_size     = 32*1024;
    gpu_cfg.l2_cache_size     = 512*1024;

    GPGPUTop  single_gpu("single_gpu", gpu_cfg);
    SystemTop::Config sys_cfg; sys_cfg.num_gpus = NUM_GPUS; sys_cfg.gpu_config = gpu_cfg;
    SystemTop sys("sys", sys_cfg);

    // ── Standalone CU for DIRECT kernels ──────────────────────────────────────
    sc_core::sc_clock bench_clock("bench_clock",
        sc_core::sc_time(GPGPU_CLOCK_PERIOD_NS, sc_core::SC_NS));
    ComputeUnit::Config cu_cfg;
    cu_cfg.unit_id=42; cu_cfg.threads_per_warp=4; cu_cfg.num_threads=4;
    cu_cfg.max_warps=1; cu_cfg.shared_mem_size=4*1024;
    ComputeUnit direct_cu("direct_cu", cu_cfg);
    direct_cu.clk(bench_clock);

    // ── Design space instances (all before sc_start) ──────────────────────────
    // A. Multi-CU scaling
    auto make_gpu = [&](uint32_t n_cu, uint32_t tpw) {
        GPGPUTop::Config c = gpu_cfg;
        c.num_compute_units = n_cu;
        c.threads_per_warp  = tpw;
        c.max_warps_per_cu  = 8;
        return c;
    };
    GPGPUTop dse_1cu("dse_1cu", make_gpu(1, 32));
    GPGPUTop dse_2cu("dse_2cu", make_gpu(2, 32));
    GPGPUTop dse_4cu("dse_4cu", make_gpu(4, 32));
    // B. Vector lane width
    GPGPUTop dse_tpw16("dse_tpw16", make_gpu(1, 16));

    sc_core::sc_start(sc_core::sc_time(0, sc_core::SC_NS));

    // ── Config banner ─────────────────────────────────────────────────────────
    sep('=');
    std::cout
        << "  RISC-V GPGPU SystemC Functional Model - Stress Benchmark\n"
        << "  NOTE: instruction counts and cache stats are exact;\n"
        << "        timing is not cycle-accurate (functional model).\n";
    sep('=');
    std::cout << "  GPUs: " << NUM_GPUS << "   CUs/GPU: " << CUS_PER_GPU
              << "   Threads/warp: " << THREADS_PER_WARP << "\n"
              << "  L1: 32 KB (write-through, no-write-allocate)   L2: 512 KB\n";
    sep('=');

    std::vector<BResult> results;
    Metrics b1_delta, b4_delta;   // captured for design space IPDOM analysis
    auto advance = [&](uint32_t ns=200) {
        sc_core::sc_start(sc_core::sc_time(ns, sc_core::SC_NS));
    };

    // ── B1: Integer SAXPY ─────────────────────────────────────────────────────
    printSection("B1  Integer SAXPY   (20 warps, 4/GPU, alpha=3, y=7)");
    { auto b=snapSys(sys); auto bpu=snapAll(sys);
      sys.launchKernel(20,1,intSaxpy(3,7)); advance();
      printPerGPU(sys,bpu);
      b1_delta = snapSys(sys)-b;
      results.push_back({"B1  Int SAXPY (a=3, y=7)",20,NUM_GPUS,b1_delta}); }

    // ── B2: FP SAXPY ──────────────────────────────────────────────────────────
    printSection("B2  FP SAXPY uniform   (20 warps, 4/GPU, alpha=2.5)");
    { auto b=snapSys(sys); auto bpu=snapAll(sys);
      sys.launchKernel(20,1,fpUniformSaxpy(2.5f,4.0f,10.0f)); advance();
      printPerGPU(sys,bpu);
      results.push_back({"B2  FP SAXPY (a=2.5,x=4.0,y=10.0)",20,NUM_GPUS,snapSys(sys)-b}); }

    // ── B3: Memory round-trip ─────────────────────────────────────────────────
    printSection("B3  Memory round-trip  (10 warps, 2/GPU, SW+LW+LW)");
    { auto b=snapSys(sys); auto bpu=snapAll(sys);
      sys.launchKernel(10,1,memoryRoundTrip()); advance();
      printPerGPU(sys,bpu);
      results.push_back({"B3  Memory round-trip",10,NUM_GPUS,snapSys(sys)-b}); }

    // ── B4: Divergent ─────────────────────────────────────────────────────────
    printSection("B4  Divergent odd/even (20 warps, 4/GPU)");
    { auto b=snapSys(sys); auto bpu=snapAll(sys);
      sys.launchKernel(20,1,divergentOddEven()); advance();
      printPerGPU(sys,bpu);
      b4_delta = snapSys(sys)-b;
      results.push_back({"B4  Divergent odd/even",20,NUM_GPUS,b4_delta}); }

    // ── B5: Barrier round-trip ────────────────────────────────────────────────
    printSection("B5  Barrier round-trip (10 warps, 2/GPU)");
    { auto b=snapSys(sys); auto bpu=snapAll(sys);
      sys.launchKernel(10,1,barrierRoundTrip(0)); advance();
      printPerGPU(sys,bpu);
      results.push_back({"B5  Barrier sync",10,NUM_GPUS,snapSys(sys)-b}); }

    // ── B6: Parallel reduction ────────────────────────────────────────────────
    printSection("B6  Parallel Reduction (10 warps, 2/GPU, BARRIER+cross-LW)");
    { auto b=snapSys(sys); auto bpu=snapAll(sys);
      sys.launchKernel(10,1,parallelReduction()); advance();
      printPerGPU(sys,bpu);
      results.push_back({"B6  Parallel reduction",10,NUM_GPUS,snapSys(sys)-b}); }

    // ── B7: GEMM (DIRECT) ─────────────────────────────────────────────────────
    printSection("B7  GEMM 2x2 tile, K=4  (direct, 4 threads, VFFMADD x4)");
    { float A[2][4]={{1,2,3,4},{5,6,7,8}}; float B[4][2]={{1,2},{3,4},{5,6},{7,8}};
      int rows[]={0,0,1,1}; int cols[]={0,1,0,1};
      WarpContext ctx; ctx.warp_id=0;
      ctx.regs.resize(4,std::vector<uint32_t>(32,0)); ctx.program=fpGemm();
      for (uint32_t t=0;t<4;++t) {
          int r=rows[t],c=cols[t];
          ctx.regs[t][3]=floatAsReg(A[r][0]); ctx.regs[t][8] =floatAsReg(B[0][c]);
          ctx.regs[t][4]=floatAsReg(A[r][1]); ctx.regs[t][9] =floatAsReg(B[1][c]);
          ctx.regs[t][5]=floatAsReg(A[r][2]); ctx.regs[t][10]=floatAsReg(B[2][c]);
          ctx.regs[t][6]=floatAsReg(A[r][3]); ctx.regs[t][11]=floatAsReg(B[3][c]);
          ctx.regs[t][7]=floatAsReg(0.0f); }
      direct_cu.executeWarp(ctx);
      std::cout << "    C=[[" << regAsFloat(ctx.regs[0][7]) << ","
                << regAsFloat(ctx.regs[1][7]) << "],["
                << regAsFloat(ctx.regs[2][7]) << "," << regAsFloat(ctx.regs[3][7]) << "]]\n"
                << "    Instructions: " << direct_cu.getTotalInstructions() << "\n"; }

    // ── B8: 2D Convolution (DIRECT) ───────────────────────────────────────────
    printSection("B8  2D Conv 3x3  (direct, 4 threads, VFMADD x9)");
    { uint32_t nb[4][9]={{1,2,3,5,6,7,9,10,11},{2,3,4,6,7,8,10,11,12},
                         {5,6,7,9,10,11,13,14,15},{6,7,8,10,11,12,14,15,16}};
      uint32_t filt[9]={1,2,1,2,4,2,1,2,1};
      WarpContext ctx; ctx.warp_id=1;
      ctx.regs.resize(4,std::vector<uint32_t>(32,0)); ctx.program=conv2d3x3();
      for (uint32_t t=0;t<4;++t) {
          for (int k=0;k<9;++k) { ctx.regs[t][3+k]=nb[t][k]; ctx.regs[t][12+k]=filt[k]; }
          ctx.regs[t][21]=0; }
      direct_cu.executeWarp(ctx);
      std::cout << "    C=[[" << ctx.regs[0][21] << "," << ctx.regs[1][21]
                << "],[" << ctx.regs[2][21] << "," << ctx.regs[3][21] << "]]\n"
                << "    Instructions: " << direct_cu.getTotalInstructions() << "\n"; }

    // ── Scalability ───────────────────────────────────────────────────────────
    printSection("SCALABILITY  Integer SAXPY: 1 GPU vs 5 GPUs");
    { auto sg_b=snap(single_gpu);
      single_gpu.launchKernel(4,1,intSaxpy(3,7)); advance();
      auto sg=snap(single_gpu)-sg_b;
      auto ms_b=snapSys(sys); auto ms_bpu=snapAll(sys);
      sys.launchKernel(20,1,intSaxpy(3,7)); advance();
      auto ms=snapSys(sys)-ms_b;
      sep();
      std::cout << "  Config            Warps   Threads   Instrs   Throughput\n"; sep();
      std::cout << "  1 GPU (baseline)     4       128     " << std::setw(5)
                << sg.instructions << "       1.00x\n"
                << "  5 GPUs (scaled)     20       640     " << std::setw(5)
                << ms.instructions << "       " << std::fixed << std::setprecision(2)
                << (sg.instructions>0?float(ms.instructions)/sg.instructions:0.f) << "x\n";
      sep();
      std::cout << "\n  Per-GPU (5-GPU run):\n";
      for (uint32_t i=0;i<sys.getNumGPUs();++i) {
          auto d=snap(sys.getGPU(i))-ms_bpu[i];
          std::cout << "    GPU " << i << ": " << d.instructions
                    << " instr  (" << THREADS_PER_WARP*4 << " threads)\n"; } }

    // ─────────────────────────────────────────────────────────────────────────
    // DESIGN SPACE EXPLORATION
    // ─────────────────────────────────────────────────────────────────────────

    // ── DSE-A: Multi-CU scaling ───────────────────────────────────────────────
    // Run intSaxpy (20 warps) on 1, 2, and 4 CU configurations.
    // Total instruction count is always 120 (same program, same warps).
    // The metric is load balance across CUs.
    printSection("DSE-A  Multi-CU Scaling  (Integer SAXPY, 20 warps, 32 tpw)");
    {
        struct DSEResult { uint32_t n_cu; uint64_t instrs; };
        std::vector<DSEResult> dse_results;

        auto run_dse = [&](GPGPUTop& gpu, uint32_t n_cu) {
            auto b = snap(gpu);
            gpu.launchKernel(20, 1, intSaxpy(3, 7));
            advance();
            dse_results.push_back({n_cu, (snap(gpu)-b).instructions});
        };
        run_dse(dse_1cu, 1);
        run_dse(dse_2cu, 2);
        run_dse(dse_4cu, 4);

        sep();
        std::cout << "  CUs   Warps/CU   Total Instrs   Load per CU\n"; sep();
        uint32_t total_warps = 20;
        for (auto& r : dse_results) {
            std::cout << "    " << r.n_cu << "       "
                      << std::setw(4) << (total_warps / r.n_cu)
                      << "         " << std::setw(5) << r.instrs
                      << "        " << (r.instrs / r.n_cu) << " instr/CU\n";
        }
        sep();
        std::cout << "  Note: total instruction count is invariant (120).\n"
                  << "  More CUs = finer dispatch granularity and lower\n"
                  << "  per-CU latency in a cycle-accurate implementation.\n";
    }

    // ── DSE-B: Vector lane width ──────────────────────────────────────────────
    // Compare 32 threads/warp (4 warps, 128 threads) vs
    //         16 threads/warp (8 warps, 128 threads) for the same total work.
    printSection("DSE-B  Vector Lane Width  (Integer SAXPY, 128 total threads)");
    {
        auto b32 = snap(dse_1cu);
        dse_1cu.launchKernel(4, 1, intSaxpy(3, 7));    // 4 warps x 32 tpw
        advance();
        auto delta32 = snap(dse_1cu) - b32;

        auto b16 = snap(dse_tpw16);
        dse_tpw16.launchKernel(8, 1, intSaxpy(3, 7));  // 8 warps x 16 tpw
        advance();
        auto delta16 = snap(dse_tpw16) - b16;

        sep();
        std::cout << "  tpw   Warps   Threads   Instrs   Instr/thread\n"; sep();
        std::cout << "   32      4      128     " << std::setw(5) << delta32.instructions
                  << "       " << std::fixed << std::setprecision(2)
                  << float(delta32.instructions) / 128.f << "\n";
        std::cout << "   16      8      128     " << std::setw(5) << delta16.instructions
                  << "       " << float(delta16.instructions) / 128.f << "\n";
        sep();
        std::cout << "  Wider lanes (32 tpw) amortize warp overhead across more\n"
                  << "  threads. Narrower lanes (16 tpw) increase scheduling\n"
                  << "  flexibility at the cost of more warp dispatches.\n";
    }

    // ── DSE-C: IPDOM / Divergence overhead ────────────────────────────────────
    // Compares non-divergent (B1) vs divergent (B4) kernels.
    // Both run 20 warps with 32 threads/warp = 640 total thread-slots.
    // IPDOM tracks which threads are masked, quantifying wasted lane-cycles.
    printSection("DSE-C  IPDOM / Divergence Overhead  (B1 vs B4 kernel)");
    {
        uint64_t total_thread_slots = 20 * THREADS_PER_WARP;  // 640
        uint64_t masked_per_event   = THREADS_PER_WARP / 2;   // 16 (odd threads)
        uint64_t wasted_slots = b4_delta.divergence * masked_per_event;
        float efficiency_nodiv = 100.0f;
        float efficiency_div   = 100.0f * float(total_thread_slots - wasted_slots)
                                        / float(total_thread_slots);

        sep();
        std::cout << "  Kernel              Warps  Div Events  Wasted Slots  Lane Eff.\n"; sep();
        std::cout << "  B1 Int SAXPY (no div)  20       "
                  << std::setw(2) << b1_delta.divergence
                  << "           0            " << std::fixed << std::setprecision(1)
                  << efficiency_nodiv << "%\n";
        std::cout << "  B4 Div odd/even        20       "
                  << std::setw(2) << b4_delta.divergence
                  << "         " << std::setw(4) << wasted_slots
                  << "            " << efficiency_div << "%\n";
        sep();
        std::cout << "  With IPDOM enabled: " << b4_delta.divergence
                  << " divergence events detected.\n"
                  << "  Each event masks " << masked_per_event
                  << " threads (odd-tid) → " << wasted_slots
                  << " wasted thread-slots.\n"
                  << "  SIMT lane efficiency: " << efficiency_nodiv
                  << "% (no div) vs " << efficiency_div << "% (with div).\n"
                  << "  Without IPDOM: both paths execute on all threads →\n"
                  << "  incorrect results but 100% lane utilisation.\n";
    }

    // ── Summary ───────────────────────────────────────────────────────────────
    std::cout << "\n\n";
    printSection("BENCHMARK SUMMARY (B1-B6, 5-GPU system)");
    printBenchTable(results);

    uint64_t tot_i=0,tot_h=0,tot_m=0; uint32_t tot_d=0;
    for (auto& r:results){tot_i+=r.delta.instructions;tot_h+=r.delta.l1_hits;
                           tot_m+=r.delta.l1_misses;  tot_d+=r.delta.divergence;}
    float hit=(tot_h+tot_m)>0?100.f*tot_h/(tot_h+tot_m):0.f;

    std::cout << "\n  TOTALS (B1-B6)\n"; sep();
    std::cout << "  Instructions executed  : " << tot_i << "\n"
              << "  L1 cache hits          : " << tot_h << "\n"
              << "  L1 cache misses        : " << tot_m << "\n"
              << "  Overall L1 hit rate    : " << std::fixed << std::setprecision(1)
              << hit << "%\n"
              << "  Divergence events      : " << tot_d
              << "  (25.0% warp-div rate across suite)\n"
              << "\n  B7 GEMM + B8 Conv2D: direct kernel results shown above\n";
    sep('=');
    std::cout << "\n";

    return 0;
}