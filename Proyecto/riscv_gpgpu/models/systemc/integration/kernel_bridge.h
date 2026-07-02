// kernel_bridge.h - Software ↔ SystemC integration bridge
//
// KernelBridge connects the software driver stack (driver/src/loader.cpp)
// to the SystemC simulation (models/systemc/top/top.h).
//
// Usage:
//   KernelBridge bridge;
//   bridge.runOnHardware("vector_add", launch_args, {a_ptr, b_ptr, c_ptr});
//   // After return, device buffers updated with results.
//

#ifndef RISCV_GPGPU_KERNEL_BRIDGE_H
#define RISCV_GPGPU_KERNEL_BRIDGE_H

#include <cstdint>
#include <string>
#include <vector>

namespace riscv_gpgpu {

struct KernelLaunchArgs;  // from driver/src/loader.h

class KernelBridge {
public:
    struct Config {
        uint32_t num_compute_units = 1;
        uint32_t threads_per_warp  = 32;
        uint32_t max_warps_per_cu  = 16;
        uint32_t shared_mem_size   = 49152;
        uint32_t l1_cache_size     = 16384;
        uint32_t l2_cache_size     = 262144;
        uint32_t max_sim_cycles    = 2000000;
        bool     print_stats       = true;
    };

    explicit KernelBridge(Config cfg);
    KernelBridge() : KernelBridge(Config{}) {}

    // Run the current driver kernel on the SystemC simulation.
    //
    // Parameters:
    //   kernel_name  - unmangled kernel name (e.g. "vector_add")
    //   binary_path  - path to the RISC-V ELF binary (from getKernelBinaryPath())
    //   launch_args  - kernel launch args including args[] = {ptr_a, ptr_b, ptr_c, n}
    //   device_ptrs  - list of device addresses whose contents should be mapped
    //                  into the SystemC memory and read back after simulation.
    //
    // Returns true on success.
    bool runOnHardware(const std::string& kernel_name,
                       const std::string& binary_path,
                       const std::vector<uint64_t>& kernel_args,
                       const std::vector<uint64_t>& device_ptrs);

    // Statistics from the last run.
    uint64_t lastTotalCycles()       const { return last_cycles_; }
    uint64_t lastTotalInstructions() const { return last_instructions_; }
    uint32_t lastL1Hits()            const { return last_l1_hits_; }
    uint32_t lastL1Misses()          const { return last_l1_misses_; }

private:
    Config   cfg_;
    uint64_t last_cycles_       = 0;
    uint64_t last_instructions_ = 0;
    uint32_t last_l1_hits_      = 0;
    uint32_t last_l1_misses_    = 0;
};

} // namespace riscv_gpgpu

#endif // RISCV_GPGPU_KERNEL_BRIDGE_H
