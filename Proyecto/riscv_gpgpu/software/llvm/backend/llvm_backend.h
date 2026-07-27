#ifndef RISCV_GPGPU_LLVM_BACKEND_H
#define RISCV_GPGPU_LLVM_BACKEND_H

#include <string>

namespace riscv_gpgpu {

bool initializeLLVMBackend();

bool emitKernelBinary(const std::string& kernel_source_path);

bool emitKernelBinary(const std::string& kernel_source_path, const std::string& output_path);

} // namespace riscv_gpgpu

#endif // RISCV_GPGPU_LLVM_BACKEND_H
