#include <iostream>
#include <string>
#include <cstdint>

namespace riscv_gpgpu {

bool initializeHostAPI() {
    std::cout << "[host_api] Initializing host API layer\n";
    return true;
}

bool shutdownHostAPI() {
    std::cout << "[host_api] Shutting down host API layer\n";
    return true;
}

bool setKernelArgument(const std::string& name, uint64_t value) {
    std::cout << "[host_api] Setting kernel argument '" << name << "' = " << value << "\n";
    return true;
}

} // namespace riscv_gpgpu
