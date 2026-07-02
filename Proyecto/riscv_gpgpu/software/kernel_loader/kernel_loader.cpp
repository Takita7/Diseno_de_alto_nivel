#include <iostream>
#include <fstream>
#include <string>
#include <vector>

namespace riscv_gpgpu {

bool packKernelBundle(const std::string& kernel_name, const std::string& binary_path, const std::string& manifest_path) {
    std::cout << "[kernel_loader] Packing kernel '" << kernel_name << "' from " << binary_path << " into " << manifest_path << "\n";
    std::ofstream manifest(manifest_path);
    if (!manifest.is_open()) {
        return false;
    }
    manifest << "{\n";
    manifest << "  \"kernel_name\": \"" << kernel_name << "\",\n";
    manifest << "  \"binary_path\": \"" << binary_path << "\"\n";
    manifest << "}\n";
    manifest.close();
    return true;
}

bool loadKernelBundle(const std::string& manifest_path) {
    std::cout << "[kernel_loader] Loading kernel bundle from " << manifest_path << "\n";
    std::ifstream manifest(manifest_path);
    return manifest.is_open();
}

} // namespace riscv_gpgpu
