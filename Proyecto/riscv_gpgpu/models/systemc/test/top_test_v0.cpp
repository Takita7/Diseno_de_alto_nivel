// top_test.cpp – Phase 0: Build & initialization check
//
// Goal: verify that all modules instantiate correctly and the
// simulation kernel starts without errors.
// No kernel is launched yet – that comes in Phase 2+.
//

#include <systemc>
#include <iostream>

#include "top/top.h"
#include "common/platform.h"
#include "common/logging.h"

using namespace riscv_gpgpu;

int sc_main(int /*argc*/, char* /*argv*/[]) {

    Platform::printSimulationBanner();
    Platform::printPhaseHeader(0, "Build & Initialization");

    // ── Instantiate with a minimal single-CU configuration ────────────────────
    GPGPUTop::Config config;
    config.num_compute_units = 1;
    config.max_warps_per_cu  = 4;
    config.threads_per_warp  = 32;
    config.shared_mem_size   = 16 * 1024;
    config.l1_cache_size     = 32 * 1024;
    config.l2_cache_size     = 512 * 1024;

    GPGPUTop top("gpgpu_top", config);

    // Advance by 0 ns: runs elaboration and delta-cycle zero, then returns.
    sc_core::sc_start(sc_core::sc_time(0, sc_core::SC_NS));

    // ── Check ─────────────────────────────────────────────────────────────────
    LOG_SEP("Phase 0 Results");

    bool pass = true;

    // After construction with stub compute unit, isComplete() should be true
    // (the stub marks itself complete immediately).
    if (!top.isKernelComplete()) {
        LOG_ERROR("FAIL – isKernelComplete() returned false before any kernel launch");
        pass = false;
    }

    if (pass) {
        std::cout << "[PASS] All modules instantiated cleanly.\n"
                  << "[PASS] sc_start(0 ns) returned without errors.\n"
                  << "[PASS] Phase 0 complete – ready for Phase 1.\n\n";
    }

    Platform::printSimulationStats(top.getTotalCycles(),
                                   top.getTotalInstructions());

    return pass ? 0 : 1;
}