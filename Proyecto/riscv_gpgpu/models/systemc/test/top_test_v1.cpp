// top_test.cpp – Phase 0 + Phase 1 tests
//

#include <systemc>
#include <iostream>

#include "top/top.h"
#include "memory/memory_hierarchy.h"
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
    MemoryHierarchy::Config mem_config;
    mem_config.shared_mem_size = 16 * 1024;
    mem_config.l1_cache_size   = 32 * 1024;
    mem_config.l2_cache_size   = 512 * 1024;
    mem_config.cache_line_size = 128;
    MemoryHierarchy mem_test("mem_test", mem_config);
    mem_test.clk(test_clock);

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

    LOG_SEP("");
    if (overall_pass)
        std::cout << "[PASS] All Phase 0 + Phase 1 checks passed.\n\n";
    else
        std::cout << "[FAIL] One or more checks failed – see above.\n\n";

    Platform::printSimulationStats(top.getTotalCycles(),
                                   top.getTotalInstructions());
    return overall_pass ? 0 : 1;
}