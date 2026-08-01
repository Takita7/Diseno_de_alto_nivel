#ifndef RISCV_GPGPU_HOST_API_H
#define RISCV_GPGPU_HOST_API_H

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace riscv_gpgpu {

// ── Lifecycle ────────────────────────────────────────────────────────────────
bool initializeHostAPI();
bool shutdownHostAPI();

// ── Device memory management ─────────────────────────────────────────────────
bool gpgpuMalloc(uint64_t& dev_ptr, size_t size);
bool gpgpuFree(uint64_t dev_ptr);
bool gpgpuMemcpyH2D(uint64_t dst_dev, const void* src_host, size_t size);
bool gpgpuMemcpyD2H(void* dst_host, uint64_t src_dev, size_t size);

// ── Kernel argument staging ───────────────────────────────────────────────────
bool setKernelArgument(const std::string& name, uint64_t value);
bool clearKernelArguments();

bool gpgpuRegisterPtx(const std::string& kernel_name, const std::string& ptx_source);
bool gpgpuUnregisterPtx(const std::string& kernel_name);
bool gpgpuGetRegisteredPtx(const std::string& kernel_name, std::string& ptx_source);
bool gpgpuRequireRegisteredPtx(const std::string& kernel_name, bool required);
bool gpgpuGetLastError(std::string& error);

// ── Kernel launch ─────────────────────────────────────────────────────────────
bool gpgpuLaunchKernel(
    const std::string& kernel_name,
    uint32_t grid_x,  uint32_t grid_y,  uint32_t grid_z,
    uint32_t block_x, uint32_t block_y, uint32_t block_z,
    const std::vector<uint64_t>& args);

bool gpgpuLaunchKernel(
    const std::string& kernel_name,
    uint32_t grid_x, uint32_t grid_y, uint32_t grid_z,
    uint32_t block_x, uint32_t block_y, uint32_t block_z,
    const std::vector<uint64_t>& args,
    uint32_t shared_mem_bytes);

// ── Synchronization ───────────────────────────────────────────────────────────
bool gpgpuSynchronize();

} // namespace riscv_gpgpu

#endif // RISCV_GPGPU_HOST_API_H
