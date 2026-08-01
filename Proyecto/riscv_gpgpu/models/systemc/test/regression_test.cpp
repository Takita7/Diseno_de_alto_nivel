// regression_test.cpp – Full regression suite
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
#include "common/kernel_programs.h"

using namespace riscv_gpgpu;
using namespace riscv_gpgpu::kernels;

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
    mem_test.allocateSharedMemory(0, 16 * 1024);
    mem_test.storeSharedMemory(0, MemoryHierarchy::SHARED_MEM_BASE, 0x12345678);
    data = 0;
    mem_test.loadSharedMemory(0, MemoryHierarchy::SHARED_MEM_BASE, data);
    CHECK(data == 0x12345678, "Shared memory write→read");
    mem_test.allocateSharedMemory(1, 16 * 1024);
    uint32_t isolated = 0xFFFFFFFFu;
    mem_test.loadSharedMemory(1, MemoryHierarchy::SHARED_MEM_BASE, isolated);
    CHECK(isolated == 0, "Shared memory is isolated and zeroed per block");
    mem_test.releaseSharedMemory(0);
    mem_test.releaseSharedMemory(1);
    CHECK(!mem_test.hasSharedMemory(0), "Shared memory released at block completion");

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
    // Condition: AND r4, r1, r3 (r3=1) → r4 = global_tid & 1
    //   Even threads (r4=0) fall through; odd threads masked.
    //   Each warp has 16 even + 16 odd threads → 1 divergence event per GPU.
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

    // ═════════════════════════════════════════════════════════════════════════
    // Phase 8 – Floating-Point
    // ═════════════════════════════════════════════════════════════════════════
    Platform::printPhaseHeader(8, "Floating-Point");

    // ── 8a: Scalar FP – FADD and FMUL ────────────────────────────────────────
    // r1[t] = 2.0f, r2[t] = 3.0f (same for all 4 threads)
    // FMUL r3, r1, r2  → r3 = 2.0 * 3.0 = 6.0
    // FADD r4, r1, r2  → r4 = 2.0 + 3.0 = 5.0
    LOG_SEP("8a: Scalar FP – FMUL and FADD");
    {
        WarpContext ctx = makeContext(7, 4, {
            makeInstr(Opcode::FMUL, 3, 1, 2, 0),   // r3 = r1 * r2  (float)
            makeInstr(Opcode::FADD, 4, 1, 2, 0),   // r4 = r1 + r2  (float)
            makeInstr(Opcode::HALT)
        });
        for (uint32_t t = 0; t < 4; ++t) {
            ctx.regs[t][1] = floatAsReg(2.0f);
            ctx.regs[t][2] = floatAsReg(3.0f);
        }

        cu_test.executeWarp(ctx);

        CHECK(ctx.state == WarpState::COMPLETE, "Warp COMPLETE");
        // All 4 threads see the same values → check all
        for (uint32_t t = 0; t < 4; ++t) {
            CHECK(regAsFloat(ctx.regs[t][3]) == 6.0f,
                  "Thread " + std::to_string(t) + ": FMUL 2.0*3.0 = 6.0");
            CHECK(regAsFloat(ctx.regs[t][4]) == 5.0f,
                  "Thread " + std::to_string(t) + ": FADD 2.0+3.0 = 5.0");
        }
    }

    // ── 8b: Vector FP SAXPY – VFMUL + VFADD ──────────────────────────────────
    // r0[t]=alpha=2.0f, r1[t]=x={1.0,2.0,3.0,4.0}, r2[t]=y=10.0f
    // VFMUL r3, r1, r0  → r3 = x * alpha = {2.0, 4.0, 6.0, 8.0}
    // VFADD r3, r3, r2  → r3 = x*alpha + y = {12.0, 14.0, 16.0, 18.0}
    LOG_SEP("8b: Vector FP SAXPY – VFMUL + VFADD");
    {
        WarpContext ctx = makeContext(8, 4, {
            makeInstr(Opcode::VFMUL, 3, 1, 0, 0),  // r3 = r1 * r0
            makeInstr(Opcode::VFADD, 3, 3, 2, 0),  // r3 = r3 + r2
            makeInstr(Opcode::HALT)
        });
        float x[] = {1.0f, 2.0f, 3.0f, 4.0f};
        for (uint32_t t = 0; t < 4; ++t) {
            ctx.regs[t][0] = floatAsReg(2.0f);     // alpha
            ctx.regs[t][1] = floatAsReg(x[t]);     // x[t]
            ctx.regs[t][2] = floatAsReg(10.0f);    // y
        }

        cu_test.executeWarp(ctx);

        float expected[] = {12.0f, 14.0f, 16.0f, 18.0f};
        for (uint32_t t = 0; t < 4; ++t) {
            CHECK(regAsFloat(ctx.regs[t][3]) == expected[t],
                  "Thread " + std::to_string(t)
                  + ": 2.0*" + std::to_string(int(x[t]))
                  + "+10.0 = " + std::to_string(int(expected[t])) + ".0");
        }
    }

    // ── 8c: VFFMADD – fused multiply-add (float) ──────────────────────────────
    // r3[t]={1.5,2.5,3.5,4.5}, r4[t]=2.0f, r5[t]=1.0f
    // VFFMADD r5, r3, r4  → r5[t] = r3[t]*r4[t] + r5[t]
    //   = {1.5*2+1, 2.5*2+1, 3.5*2+1, 4.5*2+1} = {4.0, 6.0, 8.0, 10.0}
    LOG_SEP("8c: Vector FP VFFMADD – fused multiply-add");
    {
        WarpContext ctx = makeContext(9, 4, {
            makeInstr(Opcode::VFFMADD, 5, 3, 4, 0),  // r5 = r3*r4 + r5 (float)
            makeInstr(Opcode::HALT)
        });
        float a[] = {1.5f, 2.5f, 3.5f, 4.5f};
        for (uint32_t t = 0; t < 4; ++t) {
            ctx.regs[t][3] = floatAsReg(a[t]);
            ctx.regs[t][4] = floatAsReg(2.0f);
            ctx.regs[t][5] = floatAsReg(1.0f);
        }

        cu_test.executeWarp(ctx);

        float expected[] = {4.0f, 6.0f, 8.0f, 10.0f};
        for (uint32_t t = 0; t < 4; ++t) {
            CHECK(regAsFloat(ctx.regs[t][5]) == expected[t],
                  "Thread " + std::to_string(t)
                  + ": " + std::to_string(a[t])
                  + "*2.0+1.0 = " + std::to_string(int(expected[t])) + ".0");
        }
    }

    // ═════════════════════════════════════════════════════════════════════════
    // Phase 9 – kernel_programs.h verification
    // ═════════════════════════════════════════════════════════════════════════
    Platform::printPhaseHeader(9, "kernel_programs.h");

    // ── 9a: [TOP] intSaxpy() ──────────────────────────────────────────────────
    // Replaces the inline saxpy_prog from Phase 5 with a library call.
    // Result: r6[t] = 2*(global_tid+1)+10 per thread.
    LOG_SEP("9a: [TOP] intSaxpy(2, 10) via library");
    {
        uint64_t instr_before = top.getTotalInstructions();
        top.launchKernel(1, 1, intSaxpy(2, 10));   // 1 warp, 6 instructions
        sc_core::sc_start(sc_core::sc_time(100, sc_core::SC_NS));
        CHECK(top.isKernelComplete(), "Kernel complete");
        CHECK(top.getTotalInstructions() - instr_before == 6,
              "intSaxpy: 1 warp × 6 instructions");
    }

    // ── 9b: [TOP] fpUniformSaxpy() ───────────────────────────────────────────
    // All threads compute 2.0 * 3.0 + 1.0 = 7.0 (float) end-to-end via top.
    // Verifies FP opcodes work through the full launchKernel → simulationProcess
    // → executeWarp pipeline without manual register setup.
    LOG_SEP("9b: [TOP] fpUniformSaxpy(2.0, 3.0, 1.0) via library");
    {
        uint64_t instr_before = top.getTotalInstructions();
        // 6 instructions: ADDI×3 + VFMUL + VFADD + HALT
        top.launchKernel(1, 1, fpUniformSaxpy(2.0f, 3.0f, 1.0f));
        sc_core::sc_start(sc_core::sc_time(100, sc_core::SC_NS));
        CHECK(top.isKernelComplete(), "Kernel complete");
        CHECK(top.getTotalInstructions() - instr_before == 6,
              "fpUniformSaxpy: 1 warp × 6 instructions");
    }

    // ── 9c: [TOP] memoryRoundTrip() ──────────────────────────────────────────
    // Each thread writes its tid to its unique address, reads it back twice.
    // First LW → L1 miss; second LW → L1 hit.
    LOG_SEP("9c: [TOP] memoryRoundTrip() via library");
    {
        uint64_t miss_before = top.getL1CacheMisses();
        uint64_t hit_before  = top.getL1CacheHits();
        // 4 instructions: SW + LW + LW + HALT
        top.launchKernel(1, 1, memoryRoundTrip());
        sc_core::sc_start(sc_core::sc_time(100, sc_core::SC_NS));
        CHECK(top.isKernelComplete(), "Kernel complete");
        CHECK(top.getL1CacheMisses() - miss_before == 32,
              "memoryRoundTrip: 32 L1 misses (one per thread, first LW)");
        CHECK(top.getL1CacheHits() - hit_before == 32,
              "memoryRoundTrip: 32 L1 hits  (one per thread, second LW)");
    }

    // ── 9d: [TOP] divergentOddEven() across 2 GPUs ───────────────────────────
    // 2 warps split across 2 GPUs via SystemTop.
    // Each warp has 16 even + 16 odd threads → 1 divergence event each.
    LOG_SEP("9d: [TOP] divergentOddEven() via sys.launchKernel");
    {
        uint32_t div_before = sys.getDivergenceEvents();
        sys.launchKernel(2, 1, divergentOddEven());
        sc_core::sc_start(sc_core::sc_time(100, sc_core::SC_NS));
        CHECK(sys.isComplete(), "Kernel complete on both GPUs");
        CHECK(sys.getDivergenceEvents() - div_before == 2,
              "divergentOddEven: 2 divergence events (1 per GPU/warp)");
    }

    // ── 9e: [DIRECT] fpSaxpy() on cu_test ────────────────────────────────────
    LOG_SEP("9e: [DIRECT] fpSaxpy() via library");
    {
        WarpContext ctx = makeContext(10, 4, fpSaxpy());
        float xs[] = {1.0f, 2.0f, 3.0f, 4.0f};
        for (uint32_t t = 0; t < 4; ++t) {
            ctx.regs[t][3] = floatAsReg(2.0f);      // alpha
            ctx.regs[t][4] = floatAsReg(xs[t]);     // x[t]
            ctx.regs[t][5] = floatAsReg(10.0f);     // y
        }
        cu_test.executeWarp(ctx);
        CHECK(ctx.state == WarpState::COMPLETE, "Warp COMPLETE");
        float expected[] = {12.0f, 14.0f, 16.0f, 18.0f};
        for (uint32_t t = 0; t < 4; ++t) {
            CHECK(regAsFloat(ctx.regs[t][6]) == expected[t],
                  "fpSaxpy thread " + std::to_string(t)
                  + ": result = " + std::to_string(int(expected[t])) + ".0f");
        }
    }

    // ── [DIRECT] fpFmadd() on cu_test ────────────────────────────────────
    LOG_SEP("9f: [DIRECT] fpFmadd() via library");
    {
        WarpContext ctx = makeContext(11, 4, fpFmadd());
        float a[] = {1.5f, 2.5f, 3.5f, 4.5f};
        for (uint32_t t = 0; t < 4; ++t) {
            ctx.regs[t][3] = floatAsReg(a[t]);
            ctx.regs[t][4] = floatAsReg(2.0f);
            ctx.regs[t][5] = floatAsReg(1.0f);
        }
        cu_test.executeWarp(ctx);
        float expected[] = {4.0f, 6.0f, 8.0f, 10.0f};
        for (uint32_t t = 0; t < 4; ++t) {
            CHECK(regAsFloat(ctx.regs[t][5]) == expected[t],
                  "fpFmadd thread " + std::to_string(t)
                  + ": result = " + std::to_string(int(expected[t])) + ".0f");
        }
    }

    // ═════════════════════════════════════════════════════════════════════════
    // Phase 11 – Parallel Reduction (barrier + cross-warp communication)
    // ═════════════════════════════════════════════════════════════════════════
    Platform::printPhaseHeader(11, "Parallel Reduction");

    LOG_SEP("11a: 2-warp pairwise sum via BARRIER + cross-warp LW");
    {
        // Capture warp counter BEFORE launch to compute expected addresses
        uint32_t W = top.getNextWarpId();  // first warp ID of this kernel

        uint64_t instr_before = top.getTotalInstructions();
        uint64_t miss_before  = top.getL1CacheMisses();
        uint32_t div_before   = top.getDivergenceEvents();

        top.launchKernel(2, 1, kernels::parallelReduction());
        sc_core::sc_start(sc_core::sc_time(100, sc_core::SC_NS));

        CHECK(top.isKernelComplete(),
              "isKernelComplete() = true after barrier resolution");

        // 2 warps x 15 instructions = 30
        CHECK(top.getTotalInstructions() - instr_before == 30,
              "getTotalInstructions() delta = 30  (2 warps x 15 instr)");

        // 2 warps x 32 threads x 1 cross-warp LW = 64 misses
        CHECK(top.getL1CacheMisses() - miss_before == 64,
              "L1 misses delta = 64  (cross-warp LW, 32 per warp)");

        // Homogeneous warps: no intra-warp disagreement
        CHECK(top.getDivergenceEvents() - div_before == 0,
              "Divergence events = 0  (each warp is homogeneous)");

        // Verify computed results in memory.
        // Warp 0 (local_id=0): global_tid_base = W*32
        //   thread t: value = W*32+t+1, partner = (W+1)*32+t+1
        //   r6[t] = W*32+t+1 + (W+1)*32+t+1 = 2*W*32 + 2*t + 34
        //   stored at r2+8 = 0x10000 + W*32*4 + t*4 + 8
        uint32_t w0_base = 0x10000 + W * 32 * 4;
        uint32_t expected_t0 = 2 * W * 32 + 34;
        uint32_t expected_t1 = 2 * W * 32 + 36;

        CHECK(top.readWord(w0_base + 8)  == expected_t0,
              "Warp-0 thread-0: r6 = " + std::to_string(expected_t0));
        CHECK(top.readWord(w0_base + 12) == expected_t1,
              "Warp-0 thread-1: r6 = " + std::to_string(expected_t1));

        // Warp 1 (local_id=1): global_tid_base = (W+1)*32
        //   r6[t] = same formula (symmetric sum)
        uint32_t w1_base = 0x10000 + (W + 1) * 32 * 4;
        CHECK(top.readWord(w1_base + 8)  == expected_t0,
              "Warp-1 thread-0: r6 = " + std::to_string(expected_t0));
    }
    // ═════════════════════════════════════════════════════════════════════════
    // Phase 12 – GEMM  (2×2 tile, K=4, via fpGemm())
    // ═════════════════════════════════════════════════════════════════════════
    Platform::printPhaseHeader(12, "GEMM (2x2 tile, K=4)");

    LOG_SEP("12a: C = A x B, 4 threads compute one output element each");
    {
        // A = [[1,2,3,4],[5,6,7,8]]  B = [[1,2],[3,4],[5,6],[7,8]]
        // Thread assignments: 0→C[0][0]=50  1→C[0][1]=60
        //                     2→C[1][0]=114  3→C[1][1]=140
        float A[2][4] = {{1,2,3,4},{5,6,7,8}};
        float B[4][2] = {{1,2},{3,4},{5,6},{7,8}};
        int rows[] = {0, 0, 1, 1};
        int cols[] = {0, 1, 0, 1};

        WarpContext ctx = makeContext(12, 4, kernels::fpGemm());
        for (uint32_t t = 0; t < 4; ++t) {
            int row = rows[t], col = cols[t];
            ctx.regs[t][ 3] = floatAsReg(A[row][0]);
            ctx.regs[t][ 4] = floatAsReg(A[row][1]);
            ctx.regs[t][ 5] = floatAsReg(A[row][2]);
            ctx.regs[t][ 6] = floatAsReg(A[row][3]);
            ctx.regs[t][ 8] = floatAsReg(B[0][col]);
            ctx.regs[t][ 9] = floatAsReg(B[1][col]);
            ctx.regs[t][10] = floatAsReg(B[2][col]);
            ctx.regs[t][11] = floatAsReg(B[3][col]);
            ctx.regs[t][ 7] = floatAsReg(0.0f);   // accumulator init
        }

        InstructionCount instr_before = cu_test.getTotalInstructions();
        cu_test.executeWarp(ctx);

        CHECK(ctx.state == WarpState::COMPLETE, "Warp COMPLETE");
        CHECK(cu_test.getTotalInstructions() - instr_before == 5,
              "GEMM: 5 instructions (VFFMADD×4 + HALT)");

        CHECK(regAsFloat(ctx.regs[0][7]) ==  50.0f, "C[0][0] = 50.0  (thread 0)");
        CHECK(regAsFloat(ctx.regs[1][7]) ==  60.0f, "C[0][1] = 60.0  (thread 1)");
        CHECK(regAsFloat(ctx.regs[2][7]) == 114.0f, "C[1][0] = 114.0 (thread 2)");
        CHECK(regAsFloat(ctx.regs[3][7]) == 140.0f, "C[1][1] = 140.0 (thread 3)");

        LOG_SEP("12a: Verification");
        std::cout << "  A = [[1,2,3,4],[5,6,7,8]]  B = [[1,2],[3,4],[5,6],[7,8]]\n"
                  << "  C (computed) = [["
                  << regAsFloat(ctx.regs[0][7]) << ", "
                  << regAsFloat(ctx.regs[1][7]) << "], ["
                  << regAsFloat(ctx.regs[2][7]) << ", "
                  << regAsFloat(ctx.regs[3][7]) << "]]\n";
    }

    // ═════════════════════════════════════════════════════════════════════════
    // Phase 13 – 2D Convolution  (3×3 filter, 2×2 output tile)
    // ═════════════════════════════════════════════════════════════════════════
    Platform::printPhaseHeader(13, "2D Convolution (3x3 filter, 2x2 tile)");

    LOG_SEP("13a: Input 4x4, Filter Gaussian-like [1,2,1 / 2,4,2 / 1,2,1]");
    {
        // Input (4×4):          Filter (3×3):
        //  1  2  3  4           1 2 1
        //  5  6  7  8           2 4 2
        //  9 10 11 12           1 2 1
        // 13 14 15 16

        uint32_t neighborhoods[4][9] = {
            { 1, 2, 3,  5,  6,  7,  9, 10, 11},  // thread 0 → out[0][0]
            { 2, 3, 4,  6,  7,  8, 10, 11, 12},  // thread 1 → out[0][1]
            { 5, 6, 7,  9, 10, 11, 13, 14, 15},  // thread 2 → out[1][0]
            { 6, 7, 8, 10, 11, 12, 14, 15, 16}   // thread 3 → out[1][1]
        };
        uint32_t filter[9] = {1, 2, 1, 2, 4, 2, 1, 2, 1};

        WarpContext ctx = makeContext(13, 4, kernels::conv2d3x3());
        for (uint32_t t = 0; t < 4; ++t) {
            for (int k = 0; k < 9; ++k) {
                ctx.regs[t][3  + k] = neighborhoods[t][k];  // r3..r11
                ctx.regs[t][12 + k] = filter[k];            // r12..r20
            }
            ctx.regs[t][21] = 0;   // accumulator init
        }

        InstructionCount instr_before = cu_test.getTotalInstructions();
        cu_test.executeWarp(ctx);

        CHECK(ctx.state == WarpState::COMPLETE, "Warp COMPLETE");
        CHECK(cu_test.getTotalInstructions() - instr_before == 10,
              "Conv2D: 10 instructions (VFMADD×9 + HALT)");

        CHECK(ctx.regs[0][21] ==  96u, "out[0][0] =  96");
        CHECK(ctx.regs[1][21] == 112u, "out[0][1] = 112");
        CHECK(ctx.regs[2][21] == 160u, "out[1][0] = 160");
        CHECK(ctx.regs[3][21] == 176u, "out[1][1] = 176");

        LOG_SEP("13a: Convolution result");
        std::cout << "  Output C = [["
                  << ctx.regs[0][21] << ", " << ctx.regs[1][21] << "], ["
                  << ctx.regs[2][21] << ", " << ctx.regs[3][21] << "]]\n";
    }

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
        std::cout << "[PASS] All Phase 0 – Phase 13 checks passed.\n\n";
    else
        std::cout << "[FAIL] One or more checks failed – see above.\n\n";

    // Note: getTotalCycles() returns 0 in the functional/TLM model –
    // the clock-driven step() path is never activated. Cycle counts
    // come from the HLS synthesis step, not the SystemC model.
    Platform::printSimulationStats(top.getTotalCycles(),
                                   top.getTotalInstructions());
    return overall_pass ? 0 : 1;
}