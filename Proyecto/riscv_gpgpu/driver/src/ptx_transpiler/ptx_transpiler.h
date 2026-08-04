// ptx_transpiler.h — PTX text → RISC-V ELF orchestrator
//
// Orchestrates:
//   PtxParser::parse(ptx) → PtxKernel AST
//   RvEmitter::emit(ast)  → RISC-V .s text
//   clang --target=riscv32-unknown-elf -march=rv32imf -mabi=ilp32f -fuse-ld=lld → ELF

#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace riscv_gpgpu {
namespace ptx {

struct RiscvElf {
    std::vector<uint8_t> bytes;        // ELF file contents (empty on error)
    std::string          entry_symbol; // kernel function name
    std::string          asm_text;     // intermediate .s text (for debugging)
    bool                 ok = false;
    std::string          error;
};

class PtxTranspiler {
public:
    // Compile PTX source text → RISC-V ELF bytes.
    //
    // Requires clang with riscv32 target and lld.
    // Intermediate files are written to /tmp with unique names.
    RiscvElf compile(const std::string& ptx_text);

    // Compile PTX and write the ELF to output_path.
    // Returns true on success.
    bool compileToFile(const std::string& ptx_text,
                       const std::string& output_path);

    // Generate RISC-V assembly text without invoking the assembler.
    // Useful for testing the parser+emitter without clang.
    std::string toAssembly(const std::string& ptx_text, std::string& kernel_name_out);

    // Path to clang binary (default: "clang").
    void setClangPath(const std::string& path) { clang_ = path; }

private:
    std::string clang_ = "clang";

    // Invoke shell command and capture stdout+stderr.
    // Returns exit code.
    static int runCmd(const std::string& cmd, std::string& output);
};

} // namespace ptx
} // namespace riscv_gpgpu
