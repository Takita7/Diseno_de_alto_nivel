#include <cstdint>
#include <iostream>
#include <string>
#include <vector>
#include "riscv_gpgpu_mc.h"

namespace riscv_gpgpu {

bool assembleKernelASM(const std::string& asm_text, std::vector<uint8_t>& out_binary) {
    std::cout << "[llvm_mc] Assembling kernel ASM stub\n";
    out_binary.clear();
    for (char c : asm_text) {
        out_binary.push_back(static_cast<uint8_t>(c));
    }
    return true;
}

bool linkKernelObjects(const std::vector<std::vector<uint8_t>>& objects, std::vector<uint8_t>& out_executable) {
    std::cout << "[llvm_mc] Linking kernel objects stub\n";
    out_executable.clear();
    for (auto const& obj : objects) {
        out_executable.insert(out_executable.end(), obj.begin(), obj.end());
    }
    return true;
}

} // namespace riscv_gpgpu
