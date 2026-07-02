#ifndef RISCV_GPGPU_HOST_RUNTIME_H
#define RISCV_GPGPU_HOST_RUNTIME_H

#include <cstdint>
#include <string>
#include <vector>

namespace riscv_gpgpu {

struct KernelLaunchInfo {
    std::string           name;
    std::vector<uint64_t> args;
    // Workgroup (block) dimensions.
    uint32_t workgroup_x = 1, workgroup_y = 1, workgroup_z = 1;
    // Grid dimensions (number of workgroups / blocks).
    uint32_t grid_x = 1,      grid_y = 1,      grid_z = 1;
};

// Upload a packed kernel bundle manifest to the runtime.
bool uploadKernelBundle(const std::string& manifest_path);

// Launch the currently loaded kernel.
bool launchKernel(const KernelLaunchInfo& info);

// Poll the current kernel status string (IDLE / CONFIGURED / RUNNING / COMPLETED / FAILED).
bool pollKernelStatus(std::string& status);

// Block until the current kernel reports completion.
bool waitKernelCompletion();

} // namespace riscv_gpgpu

#endif // RISCV_GPGPU_HOST_RUNTIME_H
