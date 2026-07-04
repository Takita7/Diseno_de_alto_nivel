// top.h – GPGPUTop module declaration
//
// Uses forward declarations for all sub-modules so this header stays
// lightweight. The full includes live in top.cpp only.
//

#ifndef RISCV_GPGPU_TOP_H
#define RISCV_GPGPU_TOP_H

#include <systemc>
#include <memory>
#include <vector>
#include <cstdint>

namespace riscv_gpgpu {

// Forward declarations – full definitions are in their own headers,
// included only by top.cpp.
class WarpScheduler;
class MemoryHierarchy;
class ComputeUnit;

class GPGPUTop : public sc_core::sc_module {
public:

    // ── Configuration ─────────────────────────────────────────────────────────
    struct Config {
        uint32_t num_compute_units = 1;
        uint32_t max_warps_per_cu  = 4;
        uint32_t threads_per_warp  = 32;
        uint32_t shared_mem_size   = 16 * 1024;   // 16 KB
        uint32_t l1_cache_size     = 32 * 1024;   // 32 KB
        uint32_t l2_cache_size     = 512 * 1024;  // 512 KB
    };

    // ── Lifecycle ─────────────────────────────────────────────────────────────
    SC_HAS_PROCESS(GPGPUTop);
    GPGPUTop(sc_core::sc_module_name name, const Config& config);
    ~GPGPUTop();

    // ── Public API ────────────────────────────────────────────────────────────
    void launchKernel(uint32_t grid_x, uint32_t grid_y);
    bool isKernelComplete() const;

    // Statistics – delegated to sub-modules
    uint64_t getTotalCycles()       const;
    uint64_t getTotalInstructions() const;
    uint32_t getL1CacheHits()       const;
    uint32_t getL1CacheMisses()     const;
    uint32_t getDivergenceEvents()  const;

private:
    // Declared but NOT registered as SC_THREAD yet.
    // Phase 5 will register it once all sc_fifo channels are wired.
    void simulationProcess();

    Config   config_;
    sc_core::sc_clock system_clock;

    std::unique_ptr<WarpScheduler>            scheduler_;
    std::unique_ptr<MemoryHierarchy>          memory_;
    std::vector<std::unique_ptr<ComputeUnit>> compute_units_;
};

}  // namespace riscv_gpgpu

#endif  // RISCV_GPGPU_TOP_H