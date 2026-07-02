#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>
#include "../../driver/src/loader.h"

namespace riscv_gpgpu {

// ─── Lifecycle ────────────────────────────────────────────────────────────
bool initializeHostAPI() {
    std::cout << "[host_api] Initializing host API\n";
    return true;
}

bool shutdownHostAPI() {
    std::cout << "[host_api] Shutting down host API\n";
    return true;
}

// ─── Device memory management ──────────────────────────────────────────────
// Allocates a device-visible buffer and writes a simulated device address.
// Pattern mirrors cudaMalloc(&ptr, size).
bool gpgpuMalloc(uint64_t& dev_ptr, size_t size) {
    return allocateDeviceBuffer(dev_ptr, size);
}

bool gpgpuFree(uint64_t dev_ptr) {
    return freeDeviceBuffer(dev_ptr);
}

bool gpgpuMemcpyH2D(uint64_t dst_dev, const void* src_host, size_t size) {
    return copyHostToDevice(dst_dev, src_host, size);
}

bool gpgpuMemcpyD2H(void* dst_host, uint64_t src_dev, size_t size) {
    return copyDeviceToHost(dst_host, src_dev, size);
}

// ─── Kernel argument binding ───────────────────────────────────────────────
static std::vector<uint64_t> g_staged_args;

bool setKernelArgument(const std::string& name, uint64_t value) {
    std::cout << "[host_api] arg '" << name << "' = 0x" << std::hex << value << std::dec << "\n";
    g_staged_args.push_back(value);
    return true;
}

bool clearKernelArguments() {
    g_staged_args.clear();
    return true;
}

// ─── Kernel launch ──────────────────────────────────────────────────────────
// Assembles a full KernelLaunchArgs and dispatches through the driver.
bool gpgpuLaunchKernel(
        const std::string& kernel_name,
        uint32_t grid_x,  uint32_t grid_y,  uint32_t grid_z,
        uint32_t block_x, uint32_t block_y, uint32_t block_z,
        const std::vector<uint64_t>& args) {

    KernelLaunchArgs la;
    la.kernel_name     = kernel_name;
    la.grid_x  = grid_x;  la.grid_y  = grid_y;  la.grid_z  = grid_z;
    la.block_x = block_x; la.block_y = block_y; la.block_z = block_z;
    la.args    = args.empty() ? g_staged_args : args;

    std::cout << "[host_api] gpgpuLaunchKernel: " << kernel_name
              << "  grid=[" << grid_x << "," << grid_y << "," << grid_z << "]"
              << "  block=[" << block_x << "," << block_y << "," << block_z << "]\n";

    if (!configureLaunch(la))  return false;
    if (!startKernel())        return false;
    if (!args.empty()) g_staged_args.clear();
    return true;
}

// ─── Synchronization ─────────────────────────────────────────────────────────
bool gpgpuSynchronize() {
    bool completed = false;
    if (!pollKernelCompletion(completed)) return false;
    std::string status;
    queryKernelStatus(status);
    std::cout << "[host_api] gpgpuSynchronize: kernel status = " << status << "\n";
    return completed;
}

} // namespace riscv_gpgpu
