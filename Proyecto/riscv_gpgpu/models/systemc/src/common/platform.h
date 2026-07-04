// platform.h - SystemC platform utilities
//
// Provides SystemC-specific utilities and abstractions
// for the architecture model
//

#ifndef RISCV_GPGPU_PLATFORM_H
#define RISCV_GPGPU_PLATFORM_H

#include <systemc>
#include <string>
#include <iostream>
#include <cstdint>

namespace riscv_gpgpu {

// System clock period – must match the sc_clock in GPGPUTop
static constexpr double GPGPU_CLOCK_PERIOD_NS = 10.0;

class Platform {
public:

    // Current simulation time
    static sc_core::sc_time getCurrentTime() {
        return sc_core::sc_time_stamp();
    }

    // Approximate cycle count relative to the 10 ns system clock.
    // Individual modules track their own counters; use this only for
    // quick sanity checks in tests.
    static uint64_t getCurrentCycle() {
        double time_ns = sc_core::sc_time_stamp().to_seconds() * 1.0e9;
        return static_cast<uint64_t>(time_ns / GPGPU_CLOCK_PERIOD_NS);
    }

    // Build a hierarchical module name, e.g. getModuleName("cu", 2) -> "cu_2"
    static std::string getModuleName(const std::string& base, uint32_t id) {
        return base + "_" + std::to_string(id);
    }

    // ── Output helpers ────────────────────────────────────────────────────────

    static void printSimulationBanner() {
        std::cout
            << "\n"
            << "==============================================\n"
            << "  RISC-V GPGPU  –  SystemC Functional Model  \n"
            << "==============================================\n"
            << "  SystemC version : " << SC_VERSION << "\n"
            << "  Clock period    : " << GPGPU_CLOCK_PERIOD_NS << " ns\n"
            << "==============================================\n\n"
            << std::flush;
    }

    // Numbered phase header – call at the start of each test section
    static void printPhaseHeader(int phase, const std::string& name) {
        std::string title = "Phase " + std::to_string(phase) + ": " + name;
        std::string bar(title.size() + 4, '=');
        std::cout << "\n" << bar << "\n"
                  << "  " << title << "\n"
                  << bar << "\n" << std::flush;
    }

    static void printSimulationStats(uint64_t total_cycles,
                                     uint64_t total_instructions) {
        std::cout
            << "\n"
            << "==============================================\n"
            << "  Simulation Complete\n"
            << "  Total cycles       : " << total_cycles       << "\n"
            << "  Total instructions : " << total_instructions  << "\n"
            << "==============================================\n\n"
            << std::flush;
    }
};

}  // namespace riscv_gpgpu

#endif  // RISCV_GPGPU_PLATFORM_H