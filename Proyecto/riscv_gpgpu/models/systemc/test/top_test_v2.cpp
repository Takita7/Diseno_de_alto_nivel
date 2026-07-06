// top_test.cpp – Phase 0 + Phase 1 + Phase 2 tests

#include <systemc>
#include <iostream>
#include <set>

#include "top/top.h"
#include "memory/memory_hierarchy.h"
#include "scheduler/warp_scheduler.h"
#include "common/platform.h"
#include "common/logging.h"

using namespace riscv_gpgpu;

#define CHECK(cond, msg) \
    do { \
        if (cond) { std::cout << "[PASS] " << (msg) << "\n"; } \
        else      { std::cout << "[FAIL] " << (msg) << "\n"; overall_pass = false; } \
    } while (0)

int sc_main(int /*argc*/, char* /*argv*/[]) {
    bool overall_pass = true;

    Platform::printSimulationBanner();

    // ── Instantiate all modules before sc_start ───────────────────────────────

    // Phase 0
    GPGPUTop::Config top_config;
    top_config.num_compute_units = 1;
    top_config.max_warps_per_cu  = 4;
    top_config.threads_per_warp  = 32;
    top_config.shared_mem_size   = 16 * 1024;
    top_config.l1_cache_size     = 32 * 1024;
    top_config.l2_cache_size     = 512 * 1024;
    GPGPUTop top("gpgpu_top", top_config);

    // Shared test clock (bound to all standalone test modules)
    sc_core::sc_clock test_clock("test_clock",
        sc_core::sc_time(GPGPU_CLOCK_PERIOD_NS, sc_core::SC_NS));

    // Phase 1 – standalone memory
    MemoryHierarchy::Config mem_config;
    mem_config.shared_mem_size = 16 * 1024;
    mem_config.l1_cache_size   = 32 * 1024;
    mem_config.l2_cache_size   = 512 * 1024;
    mem_config.cache_line_size = 128;
    MemoryHierarchy mem_test("mem_test", mem_config);
    mem_test.clk(test_clock);

    // Phase 2a/2b – single-CU scheduler
    WarpScheduler::Config sched1_config;
    sched1_config.num_compute_units   = 1;
    sched1_config.max_warps_per_cu    = 8;
    sched1_config.policy              = WarpScheduler::SchedulingPolicy::ROUND_ROBIN;
    sched1_config.enable_optimization = false;
    sched1_config.batch_size          = 1;
    WarpScheduler sched1("sched1", sched1_config);
    sched1.clk(test_clock);

    // Phase 2c – two-CU scheduler (load distribution test)
    WarpScheduler::Config sched2_config;
    sched2_config.num_compute_units   = 2;
    sched2_config.max_warps_per_cu    = 8;
    sched2_config.policy              = WarpScheduler::SchedulingPolicy::ROUND_ROBIN;
    sched2_config.enable_optimization = false;
    sched2_config.batch_size          = 1;
    WarpScheduler sched2("sched2", sched2_config);
    sched2.clk(test_clock);

    sc_core::sc_start(sc_core::sc_time(0, sc_core::SC_NS));

    // ═════════════════════════════════════════════════════════════════════════
    // Phase 0 – Build & Initialization
    // ═════════════════════════════════════════════════════════════════════════
    Platform::printPhaseHeader(0, "Build & Initialization");
    LOG_SEP("Phase 0 Results");
    CHECK(top.isKernelComplete(),
          "GPGPUTop: isKernelComplete() true before any kernel launch");

    // ═════════════════════════════════════════════════════════════════════════
    // Phase 1 – Memory Hierarchy
    // ═════════════════════════════════════════════════════════════════════════
    Platform::printPhaseHeader(1, "Memory Hierarchy");

    uint32_t data = 0, latency = 0;

    LOG_SEP("1a: Global memory write / read");
    mem_test.storeWord(0x10000, 0xDEADBEEF, latency);
    mem_test.loadWord (0x10000, data, latency);
    CHECK(data == 0xDEADBEEF, "Global write→read: data matches 0xDEADBEEF");

    LOG_SEP("1b: L1 hit on second read");
    uint64_t hits_before = mem_test.getL1CacheHits();
    mem_test.loadWord(0x10000, data, latency);
    CHECK(mem_test.getL1CacheHits() == hits_before + 1,
          "L1 hit counter incremented on second read");
    CHECK(latency == 1, "L1 hit latency == 1 cycle");

    LOG_SEP("1c: Cache invalidation forces miss");
    mem_test.invalidateCache();
    uint64_t misses_before = mem_test.getL1CacheMisses();
    mem_test.loadWord(0x10000, data, latency);
    CHECK(mem_test.getL1CacheMisses() == misses_before + 1,
          "L1 miss after invalidation");
    CHECK(data == 0xDEADBEEF,
          "Data still correct after re-fetch from global memory");

    LOG_SEP("1d: Shared memory");
    mem_test.storeSharedMemory(0x0, 0x12345678);
    data = 0;
    mem_test.loadSharedMemory(0x0, data);
    CHECK(data == 0x12345678, "Shared memory write→read: data matches");

    LOG_SEP("1e: Multiple addresses - no aliasing");
    mem_test.storeWord(0x20000, 0xCAFEBABE, latency);
    mem_test.storeWord(0x30000, 0xDEADC0DE, latency);
    uint32_t d1 = 0, d2 = 0;
    mem_test.loadWord(0x20000, d1, latency);
    mem_test.loadWord(0x30000, d2, latency);
    CHECK(d1 == 0xCAFEBABE, "Address 0x20000 holds correct value");
    CHECK(d2 == 0xDEADC0DE, "Address 0x30000 holds correct value");

    LOG_SEP("Phase 1 Statistics");
    std::cout << "  L1 hits   : " << mem_test.getL1CacheHits()   << "\n"
              << "  L1 misses : " << mem_test.getL1CacheMisses() << "\n"
              << "  L2 hits   : " << mem_test.getL2CacheHits()   << "\n"
              << "  L2 misses : " << mem_test.getL2CacheMisses() << "\n";

    // ═════════════════════════════════════════════════════════════════════════
    // Phase 2 – Warp Scheduler
    // ═════════════════════════════════════════════════════════════════════════
    Platform::printPhaseHeader(2, "Warp Scheduler");

    // ── 2a. Basic dispatch ────────────────────────────────────────────────────
    LOG_SEP("2a: Basic dispatch – 2x2 kernel, 1 CU");
    sched1.submitKernel(0, 2, 2);   // 4 warps → CU 0

    CHECK(sched1.hasReadyWarps(0), "hasReadyWarps(0) = true after submitKernel");

    std::set<WarpID> seen;
    bool all_valid = true;
    for (int i = 0; i < 4; ++i) {
        WarpID w = sched1.selectWarp(0);
        if (w == WarpScheduler::INVALID_WARP_ID) { all_valid = false; break; }
        seen.insert(w);
    }
    CHECK(all_valid,         "selectWarp returned valid IDs for all 4 warps");
    CHECK(seen.size() == 4,  "All 4 warp IDs are distinct");
    CHECK(!sched1.hasReadyWarps(0), "hasReadyWarps(0) = false after draining queue");

    for (WarpID w : seen) sched1.markWarpComplete(0, w);
    CHECK(sched1.isComplete(),                    "isComplete() = true after all warps complete");
    CHECK(sched1.getTotalWarpsDispatched() == 4,  "getTotalWarpsDispatched() = 4");
    CHECK(sched1.getTotalKernelsCompleted() == 4, "getTotalKernelsCompleted() = 4");

    // ── 2b. Stall flow ────────────────────────────────────────────────────────
    LOG_SEP("2b: Stall flow");
    sched1.submitKernel(0, 1, 2);   // 2 more warps
    WarpID wa = sched1.selectWarp(0);
    WarpID wb = sched1.selectWarp(0);
    CHECK(wa != WarpScheduler::INVALID_WARP_ID, "First warp selected successfully");
    CHECK(wb != WarpScheduler::INVALID_WARP_ID, "Second warp selected successfully");

    sched1.markWarpStalled(0, wa);
    sched1.markWarpComplete(0, wb);
    CHECK(!sched1.isComplete(),
          "isComplete() = false while one warp is stalled");

    // ── 2c. Load distribution – 2 CUs ─────────────────────────────────────────
    LOG_SEP("2c: Load distribution – 2x2 kernel, 2 CUs");
    sched2.submitKernel(0, 2, 2);   // 4 warps → 2 CUs

    CHECK(sched2.hasReadyWarps(0), "CU 0 has ready warps");
    CHECK(sched2.hasReadyWarps(1), "CU 1 has ready warps");

    // After balancing: each CU should have exactly 2 warps
    uint32_t cu0_count = 0, cu1_count = 0;
    while (sched2.hasReadyWarps(0)) { sched2.selectWarp(0); ++cu0_count; }
    while (sched2.hasReadyWarps(1)) { sched2.selectWarp(1); ++cu1_count; }
    CHECK(cu0_count == 2, "CU 0 received 2 warps (balanced)");
    CHECK(cu1_count == 2, "CU 1 received 2 warps (balanced)");

    LOG_SEP("Phase 2 Statistics");
    std::cout << "  sched1 dispatched  : " << sched1.getTotalWarpsDispatched()  << "\n"
              << "  sched1 completed   : " << sched1.getTotalKernelsCompleted() << "\n"
              << "  sched2 dispatched  : " << sched2.getTotalWarpsDispatched()  << "\n";

    // ── Final result ──────────────────────────────────────────────────────────
    LOG_SEP("");
    if (overall_pass)
        std::cout << "[PASS] All Phase 0 + Phase 1 + Phase 2 checks passed.\n\n";
    else
        std::cout << "[FAIL] One or more checks failed – see above.\n\n";

    Platform::printSimulationStats(top.getTotalCycles(),
                                   top.getTotalInstructions());
    return overall_pass ? 0 : 1;
}