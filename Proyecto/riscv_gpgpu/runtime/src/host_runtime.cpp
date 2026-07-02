#include <iostream>
#include <string>
#include <vector>
#include <cstdint>
#include "../../software/kernel_loader/kernel_loader.h"
#include "../../driver/src/loader.h"

namespace riscv_gpgpu {

struct KernelLaunchInfo {
    std::string name;
    std::vector<uint64_t> args;
    uint32_t workgroup_x = 1;
    uint32_t workgroup_y = 1;
    uint32_t workgroup_z = 1;
};

static std::string current_kernel_name;
static std::string current_binary_path;
static uint64_t current_binary_size = 0;
static bool current_kernel_loaded = false;

bool uploadKernelBundle(const std::string& manifest_path) {
    std::string kernel_name;
    std::string binary_path;
    uint64_t binary_size = 0;
    if (!inspectKernelBundle(manifest_path, kernel_name, binary_path, binary_size)) {
        return false;
    }

    if (!loadKernelBinary(binary_path)) {
        return false;
    }

    current_kernel_name = kernel_name;
    current_binary_path = binary_path;
    current_binary_size = binary_size;
    current_kernel_loaded = true;
    std::cout << "[runtime] Kernel bundle loaded: " << current_kernel_name << "\n";
    return true;
}

bool launchKernel(const KernelLaunchInfo& info) {
    if (!current_kernel_loaded) {
        std::cerr << "[runtime] No kernel loaded before launch.\n";
        return false;
    }
    if (info.name != current_kernel_name) {
        std::cerr << "[runtime] Kernel name mismatch: expected '" << current_kernel_name << "' got '" << info.name << "'\n";
        return false;
    }
    if (!configureKernel(info.name, info.args)) {
        return false;
    }
    if (!startKernel()) {
        return false;
    }
    std::cout << "[runtime] Launching kernel: " << info.name << " with workgroup " << info.workgroup_x << "x" << info.workgroup_y << "x" << info.workgroup_z << "\n";
    return true;
}

bool pollKernelStatus(std::string& status) {
    if (!current_kernel_loaded) {
        std::cerr << "[runtime] No kernel with status to poll.\n";
        return false;
    }
    if (!queryKernelStatus(status)) {
        return false;
    }
    std::cout << "[runtime] Kernel status: " << status << "\n";
    return true;
}

} // namespace riscv_gpgpu
