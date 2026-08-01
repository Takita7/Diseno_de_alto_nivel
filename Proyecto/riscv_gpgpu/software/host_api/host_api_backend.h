#ifndef RISCV_GPGPU_HOST_API_BACKEND_H
#define RISCV_GPGPU_HOST_API_BACKEND_H

#include <string>
#include <vector>

#include "../../driver/src/loader.h"

namespace riscv_gpgpu {

using KernelExecutionBackend = bool (*)(
    const KernelLaunchArgs& launch,
    const std::vector<uint8_t>& elf_image,
    std::string& error);

void installKernelExecutionBackend(KernelExecutionBackend backend);
KernelExecutionBackend getKernelExecutionBackend();

} // namespace riscv_gpgpu

#endif // RISCV_GPGPU_HOST_API_BACKEND_H
