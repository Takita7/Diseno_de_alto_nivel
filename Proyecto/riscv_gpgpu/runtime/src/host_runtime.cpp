#include "host_runtime.h"
#include <iostream>
#include <string>
#include <vector>
#include <cstdint>
#include "../../software/kernel_loader/kernel_loader.h"
#include "../../driver/src/loader.h"

namespace riscv_gpgpu {

static std::string current_kernel_name;
static std::string current_binary_path;
static uint64_t    current_binary_size  = 0;
static bool        current_kernel_loaded = false;

// ─── Bundle upload ─────────────────────────────────────────────────────────
bool uploadKernelBundle(const std::string& manifest_path) {
    std::string kernel_name, binary_path;
    uint64_t    binary_size = 0;
    if (!inspectKernelBundle(manifest_path, kernel_name, binary_path, binary_size))
        return false;
    if (!loadKernelBinary(binary_path))
        return false;
    current_kernel_name   = kernel_name;
    current_binary_path   = binary_path;
    current_binary_size   = binary_size;
    current_kernel_loaded = true;
    std::cout << "[runtime] Kernel bundle loaded: " << current_kernel_name
              << " (" << binary_size << " bytes)\n";
    return true;
}

// ─── Kernel launch ──────────────────────────────────────────────────────────
bool launchKernel(const KernelLaunchInfo& info) {
    if (!current_kernel_loaded) {
        std::cerr << "[runtime] No kernel loaded\n";
        return false;
    }
    if (info.name != current_kernel_name) {
        std::cerr << "[runtime] Kernel name mismatch: expected '" << current_kernel_name
                  << "' got '" << info.name << "'\n";
        return false;
    }
    KernelLaunchArgs la;
    la.kernel_name = info.name;
    la.grid_x  = info.grid_x;      la.grid_y  = info.grid_y;      la.grid_z  = info.grid_z;
    la.block_x = info.workgroup_x; la.block_y = info.workgroup_y; la.block_z = info.workgroup_z;
    la.args    = info.args;
    if (!configureLaunch(la))  return false;
    if (!startKernel())        return false;
    std::cout << "[runtime] Launched '" << info.name << "'"
              << "  grid=[" << info.grid_x << "," << info.grid_y << "," << info.grid_z << "]"
              << "  block=[" << info.workgroup_x << "," << info.workgroup_y << "," << info.workgroup_z << "]\n";
    return true;
}

// ─── Status ───────────────────────────────────────────────────────────────────
bool pollKernelStatus(std::string& status) {
    if (!current_kernel_loaded) {
        std::cerr << "[runtime] No kernel loaded\n";
        return false;
    }
    if (!queryKernelStatus(status)) return false;
    std::cout << "[runtime] Kernel '" << current_kernel_name << "' status: " << status << "\n";
    return true;
}

bool waitKernelCompletion() {
    if (!current_kernel_loaded) {
        std::cerr << "[runtime] No kernel loaded\n";
        return false;
    }
    bool completed = false;
    if (!pollKernelCompletion(completed)) return false;
    std::string status;
    queryKernelStatus(status);
    std::cout << "[runtime] Kernel '" << current_kernel_name << "' completed with status: " << status << "\n";
    return completed;
}

} // namespace riscv_gpgpu
