#ifndef RISCV_GPGPU_DRIVER_LOADER_H
#define RISCV_GPGPU_DRIVER_LOADER_H

#include <cstdint>
#include <string>
#include <vector>

namespace riscv_gpgpu {

bool loadKernelBinary(const std::string& path);
bool configureKernel(const std::string& kernel_name, const std::vector<uint64_t>& args);
bool startKernel();
bool queryKernelStatus(std::string& status);

} // namespace riscv_gpgpu

#endif // RISCV_GPGPU_DRIVER_LOADER_H
