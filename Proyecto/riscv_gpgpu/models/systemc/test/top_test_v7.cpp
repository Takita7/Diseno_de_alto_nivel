// top_test.cpp – Phase 0 through Phase 7
//

#include <systemc>
#include <iostream>
#include <set>

#include "top/top.h"
#include "system_top/system_top.h"
#include "memory/memory_hierarchy.h"
#include "scheduler/warp_scheduler.h"
#include "simt_controller/simt_controller.h"
#include "compute_unit/compute_unit.h"
#include "common/platform.h"
#include "common/logging.h"

using namespace riscv_gpgpu;

#define CHECK(cond, msg) \
    do { \
        if (cond) { std::cout << "[PASS] " << (msg) << "\n"; } \
        else      { std::cout << "[FAIL] " << (msg) << "\n"; overall_pass = false; } \
    } while (0)

static WarpContext makeContext(WarpID warp_id, uint32_t threads,
                               std::vector<Instruction> prog) {
    WarpContext ctx;
    ctx.warp_id = warp_id;
    ctx.regs.resize(threads, std::vector<uint32_t>(32, 0));
    ctx.program = std::move(prog);
    return ctx;
}

int sc_main(int /*argc*/, char* /*argv*/[]) {
    bool overall_pass = true;
    Platform::printSimulationBanner();

    // ── Instantiate all modules before sc_start ───────────────────────────────

    GPGPUTop::Config top_config;
    top_config.num_compute_units = 1;
    top_config.max_warps_per_cu  = 4;
    top_config.threads_per_warp  = 32;
    top_config.shared_mem_size   = 16 * 1024;
    top_config.l1_cache_size     = 32 * 1024;
    top_config.l2_cache_size     = 512 * 1024;
    GPGPUTop top("gpgpu_top", top_config);

    sc_core::sc_clock test_clock("test_clock",
        sc_core::sc_time(GPGPU_CLOCK_PERIOD_NS, sc_core::SC_NS));

    MemoryHierarchy::Config mem_config;
    mem_config.shared_mem_size = 16 * 1024;
    mem_config.l1_cache_size   = 32 * 1024;
    mem_config.l2_cache_size   = 512 * 1024;
    mem_config.cache_line_size = 128;
    MemoryHierarchy mem_test("mem_test", mem_config);
    mem_test.clk(test_clock);

    WarpScheduler::Config sched1_config;
    sched1_config.num_compute_units = 1;
    sched1_config.max_warps_per_cu  = 8;
    sched1_config.policy = WarpScheduler::SchedulingPolicy::ROUND_ROBIN;
    WarpScheduler sched1("sched1", sched1_config);
    sched1.clk(test_clock);

    WarpScheduler::Config sched2_config;
    sched2_config.num_compute_units = 2;
    sched2_config.max_warps_per_cu  = 8;
    sched2_config.policy = WarpScheduler::SchedulingPolicy::ROUND_ROBIN;
    WarpScheduler sched2("sched2", sched2_config);
    sched2.clk(test_clock);

    SIMTController::Config simt_config;
    simt_config.mode = SIMTController::RecovergenceMode::IMMEDIATE;
    SIMTController simt("simt_test", simt_config);
    simt.clk(test_clock);

    ComputeUnit::Config cu_config;
    cu_config.unit_id          = 99;
    cu_config.threads_per_warp = 4;
    cu_config.num_threads      = 4;
    cu_config.max_warps        = 4;
    cu_config.shared_mem_size  = 4 * 1024;
    ComputeUnit cu_test("cu_test", cu_config);
    cu_test.clk(test_clock);

    // Phase 7 – multi-GPU system (2 GPUs, same config as top)
    SystemTop::Config sys_config;
    sys_config.num_gpus   = 2;
    sys_config.gpu_config = top_config;
    SystemTop sys("system_top", sys_config);

    sc_core::sc_start(sc_core::sc_time(0, sc_core::SC_NS));

    // ═════════════════════════════════════════════════════════════════════════
    // Phase 0
    // ═════════════════════════════════════════════════════════════════════════
    Platform::printPhaseHeader(0, "Build & Initialization");
    LOG_SEP("Phase 0 Results");
    CHECK(top.isKernelComplete(), "GPGPUTop: isKernelComplete() before launch");
    CHECK(sys.isComplete(),       "SystemTop: isComplete() before launch");

    // ═════════════════════════════════════════════════════════════════════════
    // Phase 1 – Memory Hierarchy
    // ═════════════════════════════════════════════════════════════════════════
    Platform::printPhaseHeader(1, "Memory Hierarchy");
    uint32_t data = 0, latency = 0;

    LOG_SEP("1a: Global memory");
    mem_test.storeWord(0x10000, 0xDEADBEEF, latency);
    mem_test.loadWord (0x10000, data, latency);
    CHECK(data == 0xDEADBEEF, "Write→read: 0xDEADBEEF");

    LOG_SEP("1b: L1 hit");
    uint64_t hits_before = mem_test.getL1CacheHits();
    mem_test.loadWord(0x10000, data, latency);
    CHECK(mem_test.getL1CacheHits() == hits_before + 1, "L1 hit counter incremented");
    CHECK(latency == 1, "L1 hit latency == 1");

    LOG_SEP("1c: Cache invalidation");
    mem_test.invalidateCache();
    uint64_t misses_before = mem_test.getL1CacheMisses();
    mem_test.loadWord(0x10000, data, latency);
    CHECK(mem_test.getL1CacheMisses() == misses_before + 1, "L1 miss after invalidation");
    CHECK(data == 0xDEADBEEF, "Data correct after re-fetch");

    LOG_SEP("1d: Shared memory");
    mem_test.storeSharedMemory(0x0, 0x12345678);
    data = 0;
    mem_test.loadSharedMemory(0x0, data);
    CHECK(data == 0x12345678, "Shared memory write→read");

    LOG_SEP("1e: No aliasing");
    mem_test.storeWord(0x20000, 0xCAFEBABE, latency);
    mem_test.storeWord(0x30000, 0xDEADC0DE, latency);
    uint32_t d1 = 0, d2 = 0;
    mem_test.loadWord(0x20000, d1, latency);
    mem_test.loadWord(0x30000, d2, latency);
    CHECK(d1 == 0xCAFEBABE, "0x20000 correct");
    CHECK(d2 == 0xDEADC0DE, "0x30000 correct");

    // ═════════════════════════════════════════════════════════════════════════
    // Phase 2 – Warp Scheduler
    // ═════════════════════════════════════════════════════════════════════════
    Platform::printPhaseHeader(2, "Warp Scheduler");

    LOG_SEP("2a: Basic dispatch");
    sched1.submitKernel(0, 2, 2);
    CHECK(sched1.hasReadyWarps(0), "hasReadyWarps after submit");
    std::set<WarpID> seen;
    bool all_valid = true;
    for (int i = 0; i < 4; ++i) {
        WarpID w = sched1.selectWarp(0);
        if (w == WarpScheduler::INVALID_WARP_ID) { all_valid = false; break; }
        seen.insert(w);
    }
    CHECK(all_valid, "All IDs valid");
    CHECK(seen.size() == 4, "All IDs distinct");
    for (WarpID w : seen) sched1.markWarpComplete(0, w);
    CHECK(sched1.isComplete(), "isComplete after all warps done");

    LOG_SEP("2b: Stall");
    sched1.submitKernel(0, 1, 2);
    WarpID wa = sched1.selectWarp(0), wb = sched1.selectWarp(0);
    sched1.markWarpStalled(0, wa);
    sched1.markWarpComplete(0, wb);
    CHECK(!sched1.isComplete(), "isComplete=false with stalled warp");

    LOG_SEP("2c: Load balance");
    sched2.submitKernel(0, 2, 2);
    uint32_t cu0 = 0, cu1 = 0;
    while (sched2.hasReadyWarps(0)) { sched2.selectWarp(0); ++cu0; }
    while (sched2.hasReadyWarps(1)) { sched2.selectWarp(1); ++cu1; }
    CHECK(cu0 == 2, "CU0 = 2 warps");
    CHECK(cu1 == 2, "CU1 = 2 warps");

    // ═════════════════════════════════════════════════════════════════════════
    // Phase 3 – SIMT Controller
    // ═════════════════════════════════════════════════════════════════════════
    Platform::printPhaseHeader(3, "SIMT Controller");

    LOG_SEP("3a: Init");
    simt.initializeWarp(0, 8);
    CHECK(simt.getActiveMask(0) == 0xFF, "Initial mask 0xFF");

    LOG_SEP("3b: Divergent branch");
    bool cond_b[8] = {true,true,true,true,false,false,false,false};
    simt.handleBranch(0, cond_b);
    CHECK(simt.getActiveMask(0) == 0x0F, "Taken mask 0x0F");
    CHECK(simt.getTotalDivergenceEvents() == 1, "1 divergence event");

    LOG_SEP("3c: Reconvergence");
    simt.handleJoin(0);
    CHECK(simt.getActiveMask(0) == 0xFF, "Mask restored 0xFF");

    LOG_SEP("3d: No-divergence");
    simt.initializeWarp(1, 4);
    bool cond_d[4] = {true,true,true,true};
    simt.handleBranch(1, cond_d);
    CHECK(simt.getTotalDivergenceEvents() == 1, "Still 1 event");

    LOG_SEP("3e: Nested divergence");
    simt.initializeWarp(2, 8);
    bool co[8] = {true,true,true,true,false,false,false,false};
    bool ci[8] = {true,true,false,false,false,false,false,false};
    simt.handleBranch(2, co);
    simt.handleBranch(2, ci);
    CHECK(simt.getActiveMask(2) == 0x03, "Inner mask 0x03");
    simt.handleJoin(2);
    simt.handleJoin(2);
    CHECK(simt.getActiveMask(2) == 0xFF, "Fully reconverged 0xFF");

    // ═════════════════════════════════════════════════════════════════════════
    // Phase 4 – Compute Unit
    // ═════════════════════════════════════════════════════════════════════════
    Platform::printPhaseHeader(4, "Compute Unit");

    LOG_SEP("4a: Integer ALU");
    {
        WarpContext ctx = makeContext(0, 4, {
            makeInstr(Opcode::ADDI, 1, 0, 0,  5),
            makeInstr(Opcode::ADD,  2, 1, 1,  0),
            makeInstr(Opcode::HALT)
        });
        for (uint32_t t = 0; t < 4; ++t) ctx.regs[t][0] = t;
        cu_test.executeWarp(ctx);
        CHECK(ctx.state == WarpState::COMPLETE, "Warp COMPLETE");
        CHECK(ctx.regs[0][2] == 10, "Thread 0: r2=10");
        CHECK(ctx.regs[1][2] == 12, "Thread 1: r2=12");
        CHECK(ctx.regs[2][2] == 14, "Thread 2: r2=14");
        CHECK(ctx.regs[3][2] == 16, "Thread 3: r2=16");
    }

    LOG_SEP("4b: Vector SAXPY");
    {
        WarpContext ctx = makeContext(1, 4, {
            makeInstr(Opcode::VMUL, 3, 1, 0, 0),
            makeInstr(Opcode::VADD, 3, 3, 2, 0),
            makeInstr(Opcode::HALT)
        });
        for (uint32_t t = 0; t < 4; ++t) {
            ctx.regs[t][0] = 2; ctx.regs[t][1] = t+1; ctx.regs[t][2] = 10;
        }
        cu_test.executeWarp(ctx);
        CHECK(ctx.regs[0][3] == 12, "Thread 0: 2*1+10=12");
        CHECK(ctx.regs[1][3] == 14, "Thread 1: 2*2+10=14");
        CHECK(ctx.regs[2][3] == 16, "Thread 2: 2*3+10=16");
        CHECK(ctx.regs[3][3] == 18, "Thread 3: 2*4+10=18");
    }

    LOG_SEP("4c: Memory SW + LW");
    {
        WarpContext ctx = makeContext(2, 4, {
            makeInstr(Opcode::SW,  0, 0, 1, 0),
            makeInstr(Opcode::LW,  2, 0, 0, 0),
            makeInstr(Opcode::HALT)
        });
        for (uint32_t t = 0; t < 4; ++t) {
            ctx.regs[t][0] = 0x1000 + t * 4;
            ctx.regs[t][1] = t * 100;
        }
        cu_test.executeWarp(ctx);
        CHECK(ctx.regs[0][2] ==   0, "Thread 0: r2=0");
        CHECK(ctx.regs[1][2] == 100, "Thread 1: r2=100");
        CHECK(ctx.regs[2][2] == 200, "Thread 2: r2=200");
        CHECK(ctx.regs[3][2] == 300, "Thread 3: r2=300");
    }

    // ═════════════════════════════════════════════════════════════════════════
    // Phase 5 – Top Integration
    // ═════════════════════════════════════════════════════════════════════════
    Platform::printPhaseHeader(5, "Top Integration");

    std::vector<Instruction> saxpy_prog = {
        makeInstr(Opcode::ADDI, 3, 0, 0,  2),
        makeInstr(Opcode::ADDI, 4, 1, 0,  1),
        makeInstr(Opcode::ADDI, 5, 0, 0, 10),
        makeInstr(Opcode::VMUL, 6, 4, 3,  0),
        makeInstr(Opcode::VADD, 6, 6, 5,  0),
        makeInstr(Opcode::HALT)
    };

    LOG_SEP("5a: 2x1 kernel (2 warps)");
    CHECK(top.isKernelComplete(), "isKernelComplete() = true before launch");
    top.launchKernel(2, 1, saxpy_prog);
    CHECK(!top.isKernelComplete(), "isKernelComplete() = false after launch");
    sc_core::sc_start(sc_core::sc_time(100, sc_core::SC_NS));
    CHECK(top.isKernelComplete(), "isKernelComplete() = true after sc_start");
    CHECK(top.getTotalInstructions() == 12, "getTotalInstructions() = 12  (2×6)");

    LOG_SEP("5b: Second kernel (1 warp)");
    top.launchKernel(1, 1, saxpy_prog);
    sc_core::sc_start(sc_core::sc_time(100, sc_core::SC_NS));
    CHECK(top.isKernelComplete(), "isKernelComplete() after second kernel");
    CHECK(top.getTotalInstructions() == 18, "getTotalInstructions() = 18 cumulative");

    // ═════════════════════════════════════════════════════════════════════════
    // Phase 6 – Benchmarks
    // ═════════════════════════════════════════════════════════════════════════
    Platform::printPhaseHeader(6, "Benchmarks");

    LOG_SEP("6a: SAXPY with real memory");
    {
        std::vector<Instruction> mem_saxpy = {
            makeInstr(Opcode::ADDI, 3, 0, 0,  2),
            makeInstr(Opcode::ADDI, 4, 1, 0,  1),
            makeInstr(Opcode::ADDI, 5, 0, 0, 10),
            makeInstr(Opcode::VMUL, 6, 4, 3,  0),
            makeInstr(Opcode::VADD, 6, 6, 5,  0),
            makeInstr(Opcode::SW,   0, 2, 6,  0),
            makeInstr(Opcode::LW,   7, 2, 0,  0),
            makeInstr(Opcode::LW,   8, 2, 0,  0),
            makeInstr(Opcode::HALT)
        };
        uint64_t m_before = top.getL1CacheMisses();
        uint64_t h_before = top.getL1CacheHits();
        top.launchKernel(1, 1, mem_saxpy);
        sc_core::sc_start(sc_core::sc_time(100, sc_core::SC_NS));
        CHECK(top.isKernelComplete(),                          "Kernel complete");
        CHECK(top.getL1CacheMisses() == m_before + 32,        "L1 misses += 32");
        CHECK(top.getL1CacheHits()   == h_before + 32,        "L1 hits   += 32");
        CHECK(top.getDivergenceEvents() == 0,                  "No divergence");
        CHECK(top.getTotalInstructions() == 27,                "getTotalInstructions() = 27");
    }

    LOG_SEP("6b: Divergent kernel");
    {
        WarpContext ctx = makeContext(3, 4, {
            makeInstr(Opcode::VBRANCH, 0, 0, 0, 3),
            makeInstr(Opcode::ADDI,    1, 0, 0, 100),
            makeInstr(Opcode::ADD,     2, 1, 0, 0),
            makeInstr(Opcode::VJOIN,   0, 0, 0, 0),
            makeInstr(Opcode::HALT)
        });
        for (uint32_t t = 0; t < 4; ++t) ctx.regs[t][0] = t;
        uint32_t div_before = cu_test.getDivergenceEvents();
        cu_test.executeWarp(ctx);
        CHECK(ctx.regs[0][1] == 100, "Thread 0: r1=100");
        CHECK(ctx.regs[1][1] ==   0, "Thread 1: r1=0 (masked)");
        CHECK(cu_test.getDivergenceEvents() == div_before + 1, "Divergence event");
    }

    LOG_SEP("6c: VFMADD");
    {
        WarpContext ctx = makeContext(4, 4, {
            makeInstr(Opcode::VFMADD, 5, 3, 4, 0),
            makeInstr(Opcode::HALT)
        });
        uint32_t a[] = {1,2,3,4}; uint32_t b[] = {5,6,7,8};
        for (uint32_t t = 0; t < 4; ++t) {
            ctx.regs[t][3] = a[t]; ctx.regs[t][4] = b[t]; ctx.regs[t][5] = 10;
        }
        cu_test.executeWarp(ctx);
        CHECK(ctx.regs[0][5] == 15, "Thread 0: 1*5+10=15");
        CHECK(ctx.regs[1][5] == 22, "Thread 1: 2*6+10=22");
        CHECK(ctx.regs[2][5] == 31, "Thread 2: 3*7+10=31");
        CHECK(ctx.regs[3][5] == 42, "Thread 3: 4*8+10=42");
    }

    // ═════════════════════════════════════════════════════════════════════════
    // Phase 7 – Multi-GPU (SystemTop)
    // ═════════════════════════════════════════════════════════════════════════
    Platform::printPhaseHeader(7, "Multi-GPU (SystemTop)");

    // ── 7a: Even split – 4×1 kernel across 2 GPUs ────────────────────────────
    // 4 total warps → GPU 0: 2 warps (offset 0), GPU 1: 2 warps (offset 2)
    LOG_SEP("7a: Even split – 4 warps across 2 GPUs");
    {
        CHECK(sys.isComplete(), "sys.isComplete() = true before launch");

        sys.launchKernel(4, 1, saxpy_prog);
        CHECK(!sys.isComplete(), "sys.isComplete() = false after launch");

        sc_core::sc_start(sc_core::sc_time(100, sc_core::SC_NS));

        CHECK(sys.isComplete(), "sys.isComplete() = true after sc_start");
        // 4 warps × 6 instructions = 24 total
        CHECK(sys.getTotalInstructions() == 24,
              "getTotalInstructions() = 24  (4 warps × 6)");
        // Each GPU gets 2 warps × 6 = 12
        CHECK(sys.getGPU(0).getTotalInstructions() == 12,
              "GPU 0: 2 warps × 6 = 12 instructions");
        CHECK(sys.getGPU(1).getTotalInstructions() == 12,
              "GPU 1: 2 warps × 6 = 12 instructions");
        CHECK(sys.getNumGPUs() == 2, "getNumGPUs() = 2");
    }

    // ── 7b: Uneven split – 3×1 kernel across 2 GPUs ──────────────────────────
    // 3 total warps → GPU 0: 2 warps (offset 2), GPU 1: 1 warp (offset 4)
    LOG_SEP("7b: Uneven split – 3 warps across 2 GPUs");
    {
        uint64_t gpu0_before = sys.getGPU(0).getTotalInstructions();
        uint64_t gpu1_before = sys.getGPU(1).getTotalInstructions();
        uint64_t total_before = sys.getTotalInstructions();

        sys.launchKernel(3, 1, saxpy_prog);
        sc_core::sc_start(sc_core::sc_time(100, sc_core::SC_NS));

        CHECK(sys.isComplete(), "sys.isComplete() = true");
        // GPU 0: 2 warps × 6 = 12 new
        CHECK(sys.getGPU(0).getTotalInstructions() - gpu0_before == 12,
              "GPU 0: 2 warps × 6 = 12 new instructions");
        // GPU 1: 1 warp × 6 = 6 new
        CHECK(sys.getGPU(1).getTotalInstructions() - gpu1_before == 6,
              "GPU 1: 1 warp × 6 = 6 new instructions");
        // Total: 3 warps × 6 = 18 new
        CHECK(sys.getTotalInstructions() - total_before == 18,
              "Total: 3 warps × 6 = 18 new instructions");
    }

    // ── 7c: Divergent kernel across 2 GPUs ────────────────────────────────────
    // Condition: AND r4, r1, r3 (r3=1) -> r4 = global_tid & 1
    //   Even threads (r4=0) fall through; odd threads masked.
    //   Each warp has 16 even + 16 odd threads -> 1 divergence event per GPU.
    LOG_SEP("7c: Divergent kernel – 1 warp per GPU (2 total)");
    {
        std::vector<Instruction> div_prog = {
            makeInstr(Opcode::ADDI,    3, 0, 0, 1),   // r3 = 1
            makeInstr(Opcode::AND,     4, 1, 3, 0),   // r4 = r1 & 1  (0=even, 1=odd)
            makeInstr(Opcode::VBRANCH, 0, 4, 0, 2),   // odd threads masked, even fall through
            makeInstr(Opcode::ADDI,    5, 0, 0, 100), // even threads only: r5=100
            makeInstr(Opcode::VJOIN,   0, 0, 0, 0),   // reconverge
            makeInstr(Opcode::HALT)
        };

        uint32_t div_before   = sys.getDivergenceEvents();
        uint64_t total_before = sys.getTotalInstructions();

        sys.launchKernel(2, 1, div_prog);
        sc_core::sc_start(sc_core::sc_time(100, sc_core::SC_NS));

        CHECK(sys.isComplete(), "sys.isComplete() = true");
        // 2 warps × 6 instructions = 12 new
        CHECK(sys.getTotalInstructions() - total_before == 12,
              "Total: 2 warps × 6 = 12 new instructions");
        // 1 divergence event per GPU = 2 total
        CHECK(sys.getDivergenceEvents() - div_before == 2,
              "2 divergence events total (1 per GPU)");
        CHECK(sys.getGPU(0).getDivergenceEvents() == 1,
              "GPU 0: 1 divergence event");
        CHECK(sys.getGPU(1).getDivergenceEvents() == 1,
              "GPU 1: 1 divergence event");
    }

    // ── Final statistics ──────────────────────────────────────────────────────
    LOG_SEP("Final Statistics");
    std::cout << "  single GPU (top)\n"
              << "    instructions : " << top.getTotalInstructions() << "\n"
              << "    L1 hits      : " << top.getL1CacheHits()       << "\n"
              << "    L1 misses    : " << top.getL1CacheMisses()     << "\n"
              << "    divergence   : " << top.getDivergenceEvents()  << "\n"
              << "  multi-GPU (sys)\n"
              << "    instructions : " << sys.getTotalInstructions() << "\n"
              << "    L1 hits      : " << sys.getL1CacheHits()       << "\n"
              << "    L1 misses    : " << sys.getL1CacheMisses()     << "\n"
              << "    divergence   : " << sys.getDivergenceEvents()  << "\n"
              << "    GPU 0 instr  : " << sys.getGPU(0).getTotalInstructions() << "\n"
              << "    GPU 1 instr  : " << sys.getGPU(1).getTotalInstructions() << "\n";

    LOG_SEP("");
    if (overall_pass)
        std::cout << "[PASS] All Phase 0 – Phase 7 checks passed.\n\n";
    else
        std::cout << "[FAIL] One or more checks failed – see above.\n\n";

    Platform::printSimulationStats(sys.getTotalInstructions(),
                                   sys.getTotalInstructions());
    return overall_pass ? 0 : 1;
}