// elf_loader.h - ELF32 loader for the SystemC MemoryHierarchy
//
// Reads a RISC-V ELF32 binary and populates the MemoryHierarchy's
// global memory with each PT_LOAD segment.
//
// Also resolves function symbol addresses so the KernelBridge can
// set the initial PC to the correct entry point.
//

#ifndef RISCV_GPGPU_ELF_LOADER_H
#define RISCV_GPGPU_ELF_LOADER_H

#include <cstdint>
#include <string>
#include <vector>
#include <map>

// Forward declaration — avoids pulling SystemC headers into every file.
namespace riscv_gpgpu { class MemoryHierarchy; }

namespace riscv_gpgpu {

struct SymbolEntry {
    std::string name;
    uint32_t    address;
    uint32_t    size;
};

class ElfLoader {
public:
    // Load ELF32 into memory hierarchy.
    // Returns false on failure.
    bool load(const std::string& path, MemoryHierarchy& mem);

    // ELF entry point (e_entry field from ELF header).
    uint32_t getEntryPoint() const { return entry_point_; }

    // All defined function/object symbols (name → address).
    const std::map<std::string, SymbolEntry>& getSymbols() const { return symbols_; }

    // Look up a symbol by partial name match (finds first match).
    bool findSymbol(const std::string& partial_name, SymbolEntry& out) const;

    // Text segment bounds (for statistics / PC-range validation).
    uint32_t getTextBase() const { return text_base_; }
    uint32_t getTextEnd()  const { return text_end_;  }

private:
    uint32_t entry_point_ = 0;
    uint32_t text_base_   = 0;
    uint32_t text_end_    = 0;

    std::map<std::string, SymbolEntry> symbols_;
};

} // namespace riscv_gpgpu

#endif // RISCV_GPGPU_ELF_LOADER_H
