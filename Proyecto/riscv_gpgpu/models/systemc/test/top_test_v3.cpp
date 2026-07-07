// top_test.cpp – Phase 0 + Phase 1 + Phase 2 + Phase 3 tests
//

#include <systemc>
#include <iostream>
#include <set>

#include "top/top.h"
#include "memory/memory_hierarchy.h"
#include "scheduler/warp_scheduler.h"
#include "simt_controller/simt_controller.h"
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

    // Phase 1
    MemoryHierarchy::Config mem_config;
    mem_config.shared_mem_size = 16 * 1024;
    mem_config.l1_cache_size   = 32 * 1024;
    mem_config.l2_cache_size   = 512 * 1024;
    mem_config.cache_line_size = 128;
    MemoryHierarchy mem_test("mem_test", mem_config);
    mem_test.clk(test_clock);

    // Phase 2
    WarpScheduler::Config sched1_config;
    sched1_config.num_compute_units = 1;
    sched1_config.max_warps_per_cu  = 8;
    sched1_config.policy            = WarpScheduler::SchedulingPolicy::ROUND_ROBIN;
    WarpScheduler sched1("sched1", sched1_config);
    sched1.clk(test_clock);

    WarpScheduler::Config sched2_config;
    sched2_config.num_compute_units = 2;
    sched2_config.max_warps_per_cu  = 8;
    sched2_config.policy            = WarpScheduler::SchedulingPolicy::ROUND_ROBIN;
    WarpScheduler sched2("sched2", sched2_config);
    sched2.clk(test_clock);

    // Phase 3
    SIMTController::Config simt_config;
    simt_config.mode                      = SIMTController::RecovergenceMode::IMMEDIATE;
    simt_config.enable_divergence_tracking = true;
    simt_config.max_history_depth         = 8;
    SIMTController simt("simt_test", simt_config);
    simt.clk(test_clock);

    sc_core::sc_start(sc_core::sc_time(0, sc_core::SC_NS));

    // ═════════════════════════════════════════════════════════════════════════
    // Phase 0
    // ═════════════════════════════════════════════════════════════════════════
    Platform::printPhaseHeader(0, "Build & Initialization");
    LOG_SEP("Phase 0 Results");
    CHECK(top.isKernelComplete(),
          "GPGPUTop: isKernelComplete() true before any kernel launch");

    // ═════════════════════════════════════════════════════════════════════════
    // Phase 1
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
    CHECK(mem_test.getL1CacheHits() == hits_before + 1, "L1 hit counter incremented");
    CHECK(latency == 1, "L1 hit latency == 1 cycle");

    LOG_SEP("1c: Cache invalidation forces miss");
    mem_test.invalidateCache();
    uint64_t misses_before = mem_test.getL1CacheMisses();
    mem_test.loadWord(0x10000, data, latency);
    CHECK(mem_test.getL1CacheMisses() == misses_before + 1, "L1 miss after invalidation");
    CHECK(data == 0xDEADBEEF, "Data correct after re-fetch");

    LOG_SEP("1d: Shared memory");
    mem_test.storeSharedMemory(0x0, 0x12345678);
    data = 0;
    mem_test.loadSharedMemory(0x0, data);
    CHECK(data == 0x12345678, "Shared memory write→read matches");

    LOG_SEP("1e: Multiple addresses – no aliasing");
    mem_test.storeWord(0x20000, 0xCAFEBABE, latency);
    mem_test.storeWord(0x30000, 0xDEADC0DE, latency);
    uint32_t d1 = 0, d2 = 0;
    mem_test.loadWord(0x20000, d1, latency);
    mem_test.loadWord(0x30000, d2, latency);
    CHECK(d1 == 0xCAFEBABE, "0x20000 holds correct value");
    CHECK(d2 == 0xDEADC0DE, "0x30000 holds correct value");

    // ═════════════════════════════════════════════════════════════════════════
    // Phase 2
    // ═════════════════════════════════════════════════════════════════════════
    Platform::printPhaseHeader(2, "Warp Scheduler");

    LOG_SEP("2a: Basic dispatch – 2x2 kernel, 1 CU");
    sched1.submitKernel(0, 2, 2);
    CHECK(sched1.hasReadyWarps(0), "hasReadyWarps(0) = true");
    std::set<WarpID> seen;
    bool all_valid = true;
    for (int i = 0; i < 4; ++i) {
        WarpID w = sched1.selectWarp(0);
        if (w == WarpScheduler::INVALID_WARP_ID) { all_valid = false; break; }
        seen.insert(w);
    }
    CHECK(all_valid,        "All 4 warp IDs are valid");
    CHECK(seen.size() == 4, "All 4 warp IDs are distinct");
    CHECK(!sched1.hasReadyWarps(0), "Queue empty after draining");
    for (WarpID w : seen) sched1.markWarpComplete(0, w);
    CHECK(sched1.isComplete(),                    "isComplete() after all complete");
    CHECK(sched1.getTotalWarpsDispatched() == 4,  "getTotalWarpsDispatched() = 4");

    LOG_SEP("2b: Stall flow");
    sched1.submitKernel(0, 1, 2);
    WarpID wa = sched1.selectWarp(0);
    WarpID wb = sched1.selectWarp(0);
    sched1.markWarpStalled(0, wa);
    sched1.markWarpComplete(0, wb);
    CHECK(!sched1.isComplete(), "isComplete() = false with stalled warp");

    LOG_SEP("2c: Load distribution – 2 CUs");
    sched2.submitKernel(0, 2, 2);
    CHECK(sched2.hasReadyWarps(0), "CU 0 has warps");
    CHECK(sched2.hasReadyWarps(1), "CU 1 has warps");
    uint32_t cu0 = 0, cu1 = 0;
    while (sched2.hasReadyWarps(0)) { sched2.selectWarp(0); ++cu0; }
    while (sched2.hasReadyWarps(1)) { sched2.selectWarp(1); ++cu1; }
    CHECK(cu0 == 2, "CU 0 received 2 warps");
    CHECK(cu1 == 2, "CU 1 received 2 warps");

    // ═════════════════════════════════════════════════════════════════════════
    // Phase 3 – SIMT Controller
    // ═════════════════════════════════════════════════════════════════════════
    Platform::printPhaseHeader(3, "SIMT Controller");

    // ── 3a. Initialization ────────────────────────────────────────────────────
    LOG_SEP("3a: Warp initialization – 8 threads");
    simt.initializeWarp(0, 8);
    CHECK(simt.getActiveMask(0) == 0xFF, "Initial mask = 0xFF (all 8 threads active)");
    CHECK(simt.isThreadActive(0, 0),     "Thread 0 active");
    CHECK(simt.isThreadActive(0, 7),     "Thread 7 active");
    CHECK(!simt.hasPendingDivergence(0), "No pending divergence initially");

    // ── 3b. Divergent branch ──────────────────────────────────────────────────
    LOG_SEP("3b: Divergent branch – threads 0-3 take, 4-7 don't");
    bool cond_b[8] = {true, true, true, true, false, false, false, false};
    simt.handleBranch(0, cond_b);
    CHECK(simt.getActiveMask(0) == 0x0F,         "Active mask = 0x0F (taken path)");
    CHECK(simt.getTotalDivergenceEvents() == 1,   "Divergence event counted");
    CHECK(simt.getTotalWastedCycles() == 4,       "4 wasted lane-cycles (threads 4-7)");
    CHECK(simt.isThreadActive(0, 0),              "Thread 0 active (taken)");
    CHECK(!simt.isThreadActive(0, 7),             "Thread 7 inactive (not-taken)");
    CHECK(simt.hasPendingDivergence(0),           "Divergence stack non-empty");

    // ── 3c. Reconvergence ─────────────────────────────────────────────────────
    LOG_SEP("3c: Reconvergence at IPDOM");
    simt.handleJoin(0);
    CHECK(simt.getActiveMask(0) == 0xFF,          "Mask restored to 0xFF after join");
    CHECK(!simt.hasPendingDivergence(0),          "Divergence stack empty after join");
    CHECK(simt.isThreadActive(0, 7),              "Thread 7 active again");

    // ── 3d. No divergence (all threads agree) ─────────────────────────────────
    LOG_SEP("3d: No-divergence branch – all threads take same path");
    simt.initializeWarp(1, 4);
    bool cond_d[4] = {true, true, true, true};
    simt.handleBranch(1, cond_d);
    CHECK(simt.getActiveMask(1) == 0x0F,          "Mask unchanged (all threads took)");
    CHECK(simt.getTotalDivergenceEvents() == 1,   "No new divergence event");
    CHECK(!simt.hasPendingDivergence(1),          "Stack still empty");

    // ── 3e. Nested divergence ─────────────────────────────────────────────────
    LOG_SEP("3e: Nested divergence");
    simt.initializeWarp(2, 8);
    // Outer branch: threads 0-3 take, 4-7 don't
    bool cond_outer[8] = {true, true, true, true, false, false, false, false};
    simt.handleBranch(2, cond_outer);
    CHECK(simt.getActiveMask(2) == 0x0F,          "After outer branch: mask = 0x0F");

    // Inner branch (within taken path): threads 0-1 take, 2-3 don't
    bool cond_inner[8] = {true, true, false, false, false, false, false, false};
    simt.handleBranch(2, cond_inner);
    CHECK(simt.getActiveMask(2) == 0x03,          "After inner branch: mask = 0x03");
    CHECK(simt.getTotalDivergenceEvents() == 3,   "Two more divergence events (total 3)");

    // First join: threads 2-3 rejoin
    simt.handleJoin(2);
    CHECK(simt.getActiveMask(2) == 0x0F,          "After first join: mask = 0x0F");
    CHECK(simt.hasPendingDivergence(2),           "Outer divergence still pending");

    // Second join: threads 4-7 rejoin
    simt.handleJoin(2);
    CHECK(simt.getActiveMask(2) == 0xFF,          "After second join: mask = 0xFF");
    CHECK(!simt.hasPendingDivergence(2),          "All divergence resolved");

    // ── Phase 3 statistics ────────────────────────────────────────────────────
    LOG_SEP("Phase 3 Statistics");
    std::cout << "  Total divergence events : " << simt.getTotalDivergenceEvents() << "\n"
              << "  Total wasted cycles     : " << simt.getTotalWastedCycles()     << "\n";

    // ── Final result ──────────────────────────────────────────────────────────
    LOG_SEP("");
    if (overall_pass)
        std::cout << "[PASS] All Phase 0 – Phase 3 checks passed.\n\n";
    else
        std::cout << "[FAIL] One or more checks failed – see above.\n\n";

    Platform::printSimulationStats(top.getTotalCycles(),
                                   top.getTotalInstructions());
    return overall_pass ? 0 : 1;
}