#include "llvm_backend.h"

#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <sstream>
#include <string>

namespace riscv_gpgpu {
namespace fs = std::filesystem;

static std::string shellEscape(const std::string& input) {
    std::string escaped = "'";
    for (char c : input) {
        if (c == '\'') {
            escaped += "'\''";
        } else {
            escaped.push_back(c);
        }
    }
    escaped += "'";
    return escaped;
}

static bool runCommand(const std::string& command) {
    std::cout << "[llvm_backend] Running: " << command << "\n";
    int result = std::system(command.c_str());
    return result == 0;
}

bool initializeLLVMBackend() {
    bool has_clang = runCommand("command -v clang >/dev/null 2>&1");
    bool has_lld = runCommand("command -v ld.lld >/dev/null 2>&1 || command -v lld >/dev/null 2>&1");
    if (!has_clang || !has_lld) {
        std::cerr << "[llvm_backend] Missing required LLVM tools: clang and/or lld.\n";
        return false;
    }
    std::cout << "[llvm_backend] LLVM toolchain available.\n";
    return true;
}

bool emitKernelBinary(const std::string& kernel_source_path) {
    fs::path source_path(kernel_source_path);
    if (!fs::exists(source_path)) {
        std::cerr << "[llvm_backend] Kernel source not found: " << kernel_source_path << "\n";
        return false;
    }
    fs::path output_path = source_path;
    output_path.replace_extension(".riscv.elf");
    return emitKernelBinary(kernel_source_path, output_path.string());
}

bool emitKernelBinary(const std::string& kernel_source_path, const std::string& output_path) {
    fs::path source_path(kernel_source_path);
    fs::path output_file(output_path);
    if (!fs::exists(source_path)) {
        std::cerr << "[llvm_backend] Kernel source not found: " << kernel_source_path << "\n";
        return false;
    }

    fs::path object_file = output_file;
    object_file.replace_extension(".o");

    std::string compile_cmd = "clang -target riscv32-unknown-elf -march=rv32gc -mabi=ilp32 -O2 -c " + shellEscape(source_path.string()) + " -o " + shellEscape(object_file.string());
    if (!runCommand(compile_cmd)) {
        std::cerr << "[llvm_backend] Failed to compile kernel source.\n";
        return false;
    }

    fs::create_directories(output_file.parent_path());
    std::string link_cmd = "clang -target riscv32-unknown-elf -march=rv32gc -mabi=ilp32 -nostdlib -Wl,--no-entry -shared -o " + shellEscape(output_file.string()) + " " + shellEscape(object_file.string());
    if (!runCommand(link_cmd)) {
        std::cerr << "[llvm_backend] Failed to link kernel object.\n";
        return false;
    }

    if (!fs::exists(output_file) || fs::file_size(output_file) == 0) {
        std::cerr << "[llvm_backend] Output kernel binary is invalid: " << output_file << "\n";
        return false;
    }

    std::cout << "[llvm_backend] Generated kernel binary: " << output_file << "\n";
    return true;
}

} // namespace riscv_gpgpu
