#ifndef RISCV_GPGPU_KERNEL_LOADER_H
#define RISCV_GPGPU_KERNEL_LOADER_H

#include <cstdint>
#include <string>

namespace riscv_gpgpu {

bool packKernelBundle(
    const std::string& kernel_name,
    const std::string& binary_path,
    const std::string& manifest_path,
    uint32_t workgroup_x,
    uint32_t workgroup_y,
    uint32_t workgroup_z,
    uint64_t shared_mem_bytes);

bool packKernelBundle(const std::string& kernel_name, const std::string& binary_path, const std::string& manifest_path);

bool loadKernelBundle(const std::string& manifest_path);

bool inspectKernelBundle(
    const std::string& manifest_path,
    std::string& kernel_name,
    std::string& binary_path,
    uint64_t& binary_size);

} // namespace riscv_gpgpu

#endif // RISCV_GPGPU_KERNEL_LOADER_H
