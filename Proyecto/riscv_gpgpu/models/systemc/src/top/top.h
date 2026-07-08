// top.h – GPGPUTop module declaration
//
// Phase 5 adds:
//   - SC_THREAD(simulationProcess) registered in constructor
//   - kernel_launch_event_: wakes simulationProcess when launchKernel() is called
//   - kernel_program_: hardcoded SAXPY program passed to each WarpContext
//   - buildWarpContext(): builds a WarpContext for a given warp ID
//

#ifndef RISCV_GPGPU_TOP_H
#define RISCV_GPGPU_TOP_H

#include <systemc>
#include <memory>
#include <vector>
#include <cstdint>
#include "../common/types.h"   // WarpContext, Instruction, WarpID, Opcode

namespace riscv_gpgpu {

// Forward declarations
class WarpScheduler;
class MemoryHierarchy;
class ComputeUnit;

class GPGPUTop : public sc_core::sc_module {
public:

    struct Config {
        uint32_t num_compute_units = 1;
        uint32_t max_warps_per_cu  = 4;
        uint32_t threads_per_warp  = 32;
        uint32_t shared_mem_size   = 16 * 1024;
        uint32_t l1_cache_size     = 32 * 1024;
        uint32_t l2_cache_size     = 512 * 1024;
    };

    SC_HAS_PROCESS(GPGPUTop);
    GPGPUTop(sc_core::sc_module_name name, const Config& config);
    ~GPGPUTop();

    // ── Public API ────────────────────────────────────────────────────────────
    void launchKernel    (uint32_t grid_x, uint32_t grid_y);
    bool isKernelComplete() const;

    // Statistics
    uint64_t getTotalCycles()       const;
    uint64_t getTotalInstructions() const;
    uint64_t getL1CacheHits()       const;   // was uint32_t – fixed to match MemoryHierarchy
    uint64_t getL1CacheMisses()     const;   // was uint32_t – fixed to match MemoryHierarchy
    uint32_t getDivergenceEvents()  const;

private:
    // ── Phase 5: SC_THREAD execution loop ─────────────────────────────────────
    void simulationProcess();

    // Builds a WarpContext for warp_id, pre-loaded with the kernel program
    // and SAXPY register values.
    WarpContext buildWarpContext(WarpID warp_id) const;

    // ── Data members ──────────────────────────────────────────────────────────
    Config   config_;
    sc_core::sc_clock  system_clock;
    sc_core::sc_event  kernel_launch_event_;   // fired by launchKernel()
    std::vector<Instruction> kernel_program_;  // set by launchKernel()

    std::unique_ptr<WarpScheduler>            scheduler_;
    std::unique_ptr<MemoryHierarchy>          memory_;
    std::vector<std::unique_ptr<ComputeUnit>> compute_units_;
};

}  // namespace riscv_gpgpu

#endif  // RISCV_GPGPU_TOP_H