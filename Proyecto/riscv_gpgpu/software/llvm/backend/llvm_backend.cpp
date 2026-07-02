#include <iostream>
#include <string>

namespace riscv_gpgpu {

bool initializeLLVMBackend() {
    std::cout << "[llvm_backend] Initializing riscv-gpgpu backend scaffold\n";
    return true;
}

bool emitKernelBinary(const std::string& kernel_name) {
    std::cout << "[llvm_backend] Emitting kernel binary for " << kernel_name << "\n";
    return true;
}

} // namespace riscv_gpgpu
