// top_test.cpp – Phase 0 through Phase 5 tests
//

#include <systemc>
#include <iostream>
#include <set>

#include "top/top.h"
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

    // Elaboration
    sc_core::sc_start(sc_core::sc_time(0, sc_core::SC_NS));

    // ═════════════════════════════════════════════════════════════════════════
    // Phase 0
    // ═════════════════════════════════════════════════════════════════════════
    Platform::printPhaseHeader(0, "Build & Initialization");
    LOG_SEP("Phase 0 Results");
    CHECK(top.isKernelComplete(),
          "GPGPUTop: isKernelComplete() before any kernel launch");

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
    CHECK(all_valid,        "All IDs valid");
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
            makeInstr(Opcode::VMUL, 3, 1, 0,  0),
            makeInstr(Opcode::VADD, 3, 3, 2,  0),
            makeInstr(Opcode::HALT)
        });
        for (uint32_t t = 0; t < 4; ++t) {
            ctx.regs[t][0] = 2; ctx.regs[t][1] = t + 1; ctx.regs[t][2] = 10;
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
    // Phase 5 – Top Integration (end-to-end SAXPY kernel)
    // ═════════════════════════════════════════════════════════════════════════
    Platform::printPhaseHeader(5, "Top Integration");
    LOG_SEP("5a: End-to-end SAXPY kernel – 2x1 grid (2 warps), 32 threads/warp");

    // Sanity check: scheduler empty before launch
    CHECK(top.isKernelComplete(), "isKernelComplete() = true before launch");

    // Launch: submits 2 warps, fires kernel_launch_event_
    top.launchKernel(2, 1);
    CHECK(!top.isKernelComplete(), "isKernelComplete() = false immediately after launch");

    // Advance simulation: simulationProcess runs both warps to completion
    sc_core::sc_start(sc_core::sc_time(100, sc_core::SC_NS));

    LOG_SEP("5a: Results");
    CHECK(top.isKernelComplete(), "isKernelComplete() = true after sc_start");

    // 2 warps × 3 instructions (VMUL + VADD + HALT) = 6
    CHECK(top.getTotalInstructions() == 6,
          "getTotalInstructions() = 6  (2 warps × 3 instructions)");

    LOG_SEP("5b: Second kernel launch (stateful reset check)");
    // Launch again: scheduler re-used, new warps queued
    top.launchKernel(1, 1);   // 1 warp
    sc_core::sc_start(sc_core::sc_time(100, sc_core::SC_NS));
    CHECK(top.isKernelComplete(), "isKernelComplete() = true after second kernel");
    // 6 from first launch + 3 from second = 9
    CHECK(top.getTotalInstructions() == 9,
          "getTotalInstructions() = 9 cumulative (6 + 3)");

    LOG_SEP("Phase 5 Statistics");
    std::cout << "  Total instructions : " << top.getTotalInstructions() << "\n"
              << "  Total cycles       : " << top.getTotalCycles()
              << "  (0 – clock-driven path not active in Phase 5)\n"
              << "  L1 cache hits      : " << top.getL1CacheHits()
              << "  (0 – memory not yet wired to compute unit)\n";

    // ── Final result ──────────────────────────────────────────────────────────
    LOG_SEP("");
    if (overall_pass)
        std::cout << "[PASS] All Phase 0 – Phase 5 checks passed.\n\n";
    else
        std::cout << "[FAIL] One or more checks failed – see above.\n\n";

    Platform::printSimulationStats(top.getTotalCycles(),
                                   top.getTotalInstructions());
    return overall_pass ? 0 : 1;
}