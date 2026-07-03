// top.h - Top-level SystemC module
//
// Integrates ComputeUnits, WarpScheduler, SIMTController, and MemoryHierarchy.
//

#ifndef RISCV_GPGPU_TOP_H
#define RISCV_GPGPU_TOP_H

#include <systemc>
#include <array>
#include <vector>
#include <memory>
#include <cstdint>

namespace riscv_gpgpu {

class ComputeUnit;
class WarpScheduler;
class MemoryHierarchy;
class SIMTController;

class GPGPUTop : public sc_core::sc_module {
public:
    sc_core::sc_in<bool> clk{"clk"};
    sc_core::sc_in<bool> reset{"reset"};

    struct Config {
        uint32_t num_compute_units;
        uint32_t threads_per_warp;
        uint32_t max_warps_per_cu;
        uint32_t shared_mem_size;
        uint32_t l1_cache_size;
        uint32_t l2_cache_size;
    };

    GPGPUTop(sc_core::sc_module_name name, const Config& config);
    ~GPGPUTop();

    // ── Launch interface ──────────────────────────────────────────────────────
    void launchKernel(uint32_t grid_x, uint32_t grid_y);
    bool isKernelComplete() const;

    // ── Pre-launch kernel configuration ──────────────────────────────────────
    // Set entry point, initial register file, and return sentinel for all CUs.
    void configureKernel(uint32_t entry_pc,
                         const std::array<uint32_t, 32>& init_regs,
                         uint32_t return_sentinel = 0x00000000u);

    // ── Shared memory hierarchy access (for KernelBridge) ────────────────────
    MemoryHierarchy* getMemoryHierarchy();

    // ── Statistics ────────────────────────────────────────────────────────────
    uint64_t getTotalCycles()       const;
    uint64_t getTotalInstructions() const;
    uint32_t getL1CacheHits()       const;
    uint32_t getL1CacheMisses()     const;
    uint32_t getDivergenceEvents()  const;

private:
    Config config_;

    // Internal clock/reset signals (self-driven)
    sc_core::sc_signal<bool> clk_sig_{"clk_sig"};
    sc_core::sc_signal<bool> reset_sig_{"reset_sig"};

    // Per-CU signals for memory handshake ports
    std::vector<std::unique_ptr<sc_core::sc_signal<bool>>> mem_ready_sigs_;
    std::vector<std::unique_ptr<sc_core::sc_signal<bool>>> mem_req_sigs_;

    std::vector<std::unique_ptr<ComputeUnit>>  compute_units_;
    std::unique_ptr<WarpScheduler>             scheduler_;
    std::unique_ptr<SIMTController>            simt_controller_;
    std::unique_ptr<MemoryHierarchy>           memory_;

    SC_HAS_PROCESS(GPGPUTop);
    void simulationProcess();
};

}  // namespace riscv_gpgpu

#endif  // RISCV_GPGPU_TOP_H
