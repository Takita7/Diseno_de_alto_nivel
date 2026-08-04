// ptx_transpiler.cpp — PTX text → RISC-V ELF orchestrator

#include "ptx_transpiler.h"
#include "ptx_parser.h"
#include "rv_emitter.h"

#include <array>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <system_error>
#include <unistd.h>

namespace riscv_gpgpu {
namespace ptx {

// ── Shell helper ──────────────────────────────────────────────────────────────

int PtxTranspiler::runCmd(const std::string& cmd, std::string& output) {
    output.clear();
    std::array<char, 256> buf;
    // Redirect stderr to stdout
    std::string full_cmd = cmd + " 2>&1";
    FILE* pipe = popen(full_cmd.c_str(), "r");
    if (!pipe) { output = "popen failed"; return -1; }
    while (fgets(buf.data(), static_cast<int>(buf.size()), pipe))
        output += buf.data();
    return pclose(pipe);
}

// ── Assembly-only path ────────────────────────────────────────────────────────

std::string PtxTranspiler::toAssembly(const std::string& ptx_text,
                                       std::string& kernel_name_out) {
    PtxParser parser;
    PtxKernel kernel = parser.parse(ptx_text);
    if (kernel.name.empty()) {
        kernel_name_out.clear();
        return "";
    }
    kernel_name_out = kernel.name;

    RvEmitter emitter;
    std::string asm_text = emitter.emit(kernel);
    if (!emitter.lastError().empty()) {
        kernel_name_out.clear();
        return "";
    }
    return asm_text;
}

// ── Compile to ELF ────────────────────────────────────────────────────────────

RiscvElf PtxTranspiler::compile(const std::string& ptx_text) {
    RiscvElf result;

    // ── 1. Parse PTX → AST ────────────────────────────────────────────────────
    PtxParser parser;
    PtxKernel kernel = parser.parse(ptx_text);
    if (kernel.name.empty()) {
        result.error = "PTX parse error: " + parser.lastError();
        return result;
    }
    result.entry_symbol = kernel.name;

    // ── 2. Emit RISC-V assembly ────────────────────────────────────────────────
    RvEmitter emitter;
    result.asm_text = emitter.emit(kernel);
    if (!emitter.lastError().empty()) {
        result.error = "RV emit error: " + emitter.lastError();
        return result;
    }

    char temp_path[] = "/tmp/ptx_rv32_XXXXXX";
    char* created_path = mkdtemp(temp_path);
    if (created_path == nullptr) {
        result.error = "Cannot create temporary compilation directory";
        return result;
    }

    const std::filesystem::path temp_dir(created_path);
    struct TempDirectoryGuard {
        std::filesystem::path path;
        ~TempDirectoryGuard() {
            std::error_code error;
            std::filesystem::remove_all(path, error);
        }
    } guard{temp_dir};

    const std::filesystem::path asm_path = temp_dir / "kernel.s";
    const std::filesystem::path elf_path = temp_dir / "kernel.elf";

    {
        std::ofstream file(asm_path);
        if (!file) {
            result.error = "Cannot write temporary RISC-V assembly";
            return result;
        }
        file << result.asm_text;
    }

    std::string cmd = clang_
        + " --target=riscv32-unknown-elf"
        + " -march=rv32imf -mabi=ilp32f"
        + " -fuse-ld=lld -nostdlib"
        + " \"" + asm_path.string() + "\""
        + " -o \"" + elf_path.string() + "\"";

    std::string cmd_out;
    int rc = runCmd(cmd, cmd_out);
    if (rc != 0) {
        result.error = "clang error (rc=" + std::to_string(rc) + "): " + cmd_out;
        return result;
    }

    std::ifstream elf_file(elf_path, std::ios::binary);
    if (!elf_file) {
        result.error = "Cannot read temporary ELF";
        return result;
    }
    result.bytes.assign(std::istreambuf_iterator<char>(elf_file),
                        std::istreambuf_iterator<char>());
    result.ok = !result.bytes.empty();
    if (!result.ok) result.error = "Assembler produced an empty ELF image";
    return result;
}

// ── Compile to file ───────────────────────────────────────────────────────────

bool PtxTranspiler::compileToFile(const std::string& ptx_text,
                                   const std::string& output_path) {
    RiscvElf result = compile(ptx_text);
    if (!result.ok) {
        std::cerr << "[ptx_transpiler] " << result.error << "\n";
        return false;
    }
    std::ofstream out(output_path, std::ios::binary);
    if (!out) { std::cerr << "[ptx_transpiler] Cannot write " << output_path << "\n"; return false; }
    out.write(reinterpret_cast<const char*>(result.bytes.data()),
              static_cast<std::streamsize>(result.bytes.size()));
    return true;
}

} // namespace ptx
} // namespace riscv_gpgpu
