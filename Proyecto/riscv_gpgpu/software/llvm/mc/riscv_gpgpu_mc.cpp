#include "riscv_gpgpu_mc.h"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

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
    std::cout << "[llvm_mc] Running: " << command << "\n";
    return std::system(command.c_str()) == 0;
}

static bool writeFile(const fs::path& path, const std::string& content) {
    std::ofstream out(path, std::ios::binary);
    if (!out.is_open()) {
        return false;
    }
    out << content;
    return out.good();
}

static bool readFileBytes(const fs::path& path, std::vector<uint8_t>& out) {
    std::ifstream in(path, std::ios::binary);
    if (!in.is_open()) {
        return false;
    }
    out.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    return !out.empty();
}

bool assembleKernelASM(const std::string& asm_text, std::vector<uint8_t>& out_binary) {
    fs::path temp_dir = fs::temp_directory_path();
    fs::path asm_file = temp_dir / "riscv_gpgpu_kernel.s";
    fs::path obj_file = temp_dir / "riscv_gpgpu_kernel.o";
    if (!writeFile(asm_file, asm_text)) {
        std::cerr << "[llvm_mc] Failed to write temporary assembly file.\n";
        return false;
    }

    std::string asm_cmd = "clang -target riscv32-unknown-elf -march=rv32gc -mabi=ilp32 -c " + shellEscape(asm_file.string()) + " -o " + shellEscape(obj_file.string());
    if (!runCommand(asm_cmd)) {
        std::cerr << "[llvm_mc] Failed to assemble RISC-V assembly.\n";
        return false;
    }

    bool result = readFileBytes(obj_file, out_binary);
    fs::remove(asm_file);
    fs::remove(obj_file);
    return result;
}

bool linkKernelObjects(const std::vector<std::vector<uint8_t>>& objects, std::vector<uint8_t>& out_executable) {
    fs::path temp_dir = fs::temp_directory_path();
    std::vector<fs::path> object_paths;
    for (size_t i = 0; i < objects.size(); ++i) {
        fs::path obj_path = temp_dir / ("riscv_gpgpu_obj_" + std::to_string(i) + ".o");
        std::ofstream out(obj_path, std::ios::binary);
        if (!out.is_open()) {
            std::cerr << "[llvm_mc] Failed to write object file: " << obj_path << "\n";
            return false;
        }
        out.write(reinterpret_cast<const char*>(objects[i].data()), objects[i].size());
        out.close();
        object_paths.push_back(obj_path);
    }

    fs::path output_file = temp_dir / "riscv_gpgpu_linked.elf";
    std::ostringstream link_cmd;
    link_cmd << "clang -target riscv32-unknown-elf -march=rv32gc -mabi=ilp32 -nostdlib -Wl,--no-entry -shared -o " << shellEscape(output_file.string());
    for (auto const& path : object_paths) {
        link_cmd << " " << shellEscape(path.string());
    }

    if (!runCommand(link_cmd.str())) {
        std::cerr << "[llvm_mc] Failed to link object files.\n";
        return false;
    }

    bool result = readFileBytes(output_file, out_executable);
    for (auto const& path : object_paths) {
        fs::remove(path);
    }
    fs::remove(output_file);
    return result;
}

} // namespace riscv_gpgpu
