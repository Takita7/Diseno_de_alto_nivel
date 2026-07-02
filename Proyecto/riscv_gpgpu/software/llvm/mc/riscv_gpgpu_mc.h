#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace riscv_gpgpu {

bool assembleKernelASM(const std::string& asm_text, std::vector<uint8_t>& out_binary);
bool linkKernelObjects(const std::vector<std::vector<uint8_t>>& objects, std::vector<uint8_t>& out_executable);

} // namespace riscv_gpgpu
