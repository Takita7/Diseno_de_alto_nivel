// top.h – GPGPUTop module declaration
//

#ifndef RISCV_GPGPU_TOP_H
#define RISCV_GPGPU_TOP_H

#include <systemc>
#include <memory>
#include <vector>
#include <cstdint>
#include "../common/types.h"

namespace riscv_gpgpu {

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

    void launchKernel(uint32_t grid_x, uint32_t grid_y,
                      std::vector<Instruction> program,
                      uint32_t warp_id_offset = 0);

    bool isKernelComplete() const;

    uint64_t getTotalCycles()       const;
    uint64_t getTotalInstructions() const;
    uint64_t getL1CacheHits()       const;
    uint64_t getL1CacheMisses()     const;
    uint32_t getDivergenceEvents()  const;
    uint32_t getNextWarpId()        const;   // warp counter before next launch
    uint32_t readWord(Address addr) const;   // direct memory read for test verification

private:
    void        simulationProcess();
    WarpContext buildWarpContext(WarpID warp_id) const;

    Config             config_;
    sc_core::sc_clock  system_clock;
    sc_core::sc_event  kernel_launch_event_;
    std::vector<Instruction> kernel_program_;
    uint32_t           warp_id_offset_       = 0;
    uint32_t           kernel_start_warp_id_ = 0;   // 
    uint32_t           total_warps_          = 0;   // grid_x * grid_y

    std::unique_ptr<WarpScheduler>            scheduler_;
    std::unique_ptr<MemoryHierarchy>          memory_;
    std::vector<std::unique_ptr<ComputeUnit>> compute_units_;
};

}  // riscv_gpgpu

#endif  // RISCV_GPGPU_TOP_H