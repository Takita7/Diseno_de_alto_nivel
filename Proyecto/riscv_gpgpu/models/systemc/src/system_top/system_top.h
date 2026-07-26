// system_top.h – Multi-GPU top-level module
//
// Owns N GPGPUTop instances (all sharing the same Config) and handles
// work distribution when a kernel is launched across them.
//
// Work distribution:
//   total_warps = grid_x * grid_y
//   base = total_warps / num_gpus
//   remainder = total_warps % num_gpus
//   GPU i gets (base + (i < remainder ? 1 : 0)) warps
//   GPU i warp_id_offset = sum of warps assigned to GPUs 0..i-1
//
// Example: 5 warps, 2 GPUs -> GPU 0 gets 3 warps (offset 0),
//                             GPU 1 gets 2 warps (offset 3)
//

#ifndef RISCV_GPGPU_SYSTEM_TOP_H
#define RISCV_GPGPU_SYSTEM_TOP_H

#include <systemc>
#include <vector>
#include <memory>
#include <cstdint>
#include "../top/top.h"

namespace riscv_gpgpu {

class SystemTop : public sc_core::sc_module {
public:

    struct Config {
        uint32_t        num_gpus = 2;
        GPGPUTop::Config gpu_config;   // same config applied to every GPU
    };

    SC_HAS_PROCESS(SystemTop);
    SystemTop(sc_core::sc_module_name name, const Config& config);
    ~SystemTop();

    // ── Kernel launch ─────────────────────────────────────────────────────────
    // Splits total warps (grid_x * grid_y) evenly across all GPUs,
    // assigning each GPU a non-overlapping warp_id_offset.
    void launchKernel(uint32_t grid_x, uint32_t grid_y,
                      std::vector<Instruction> program);

    // ── Status ────────────────────────────────────────────────────────────────
    bool isComplete() const;

    // ── Per-GPU access ────────────────────────────────────────────────────────
    uint32_t    getNumGPUs()        const { return static_cast<uint32_t>(gpus_.size()); }
    GPGPUTop&   getGPU(uint32_t i)        { return *gpus_[i]; }
    const GPGPUTop& getGPU(uint32_t i) const { return *gpus_[i]; }

    // ── Aggregated statistics ─────────────────────────────────────────────────
    uint64_t getTotalInstructions() const;
    uint64_t getL1CacheHits()       const;
    uint64_t getL1CacheMisses()     const;
    uint32_t getDivergenceEvents()  const;

private:
    Config config_;
    std::vector<std::unique_ptr<GPGPUTop>> gpus_;
};

}  // namespace riscv_gpgpu

#endif  // RISCV_GPGPU_SYSTEM_TOP_H
