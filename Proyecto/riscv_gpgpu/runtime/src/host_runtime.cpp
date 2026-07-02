#include <iostream>
#include <string>
#include <vector>
#include <cstdint>

namespace riscv_gpgpu {

struct KernelLaunchInfo {
    std::string name;
    std::vector<uint64_t> args;
    uint32_t workgroup_x = 1;
    uint32_t workgroup_y = 1;
    uint32_t workgroup_z = 1;
};

bool uploadKernel(const KernelLaunchInfo& info) {
    std::cout << "[runtime] Uploading kernel: " << info.name << "\n";
    return true;
}

bool launchKernel(const KernelLaunchInfo& info) {
    std::cout << "[runtime] Launching kernel: " << info.name << " with workgroup " << info.workgroup_x << "x" << info.workgroup_y << "x" << info.workgroup_z << "\n";
    return true;
}

bool pollKernelStatus(std::string& status) {
    status = "COMPLETED";
    return true;
}

} // namespace riscv_gpgpu
