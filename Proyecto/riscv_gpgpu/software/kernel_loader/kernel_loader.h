#ifndef RISCV_GPGPU_KERNEL_LOADER_H
#define RISCV_GPGPU_KERNEL_LOADER_H

#include <cstdint>
#include <string>

namespace riscv_gpgpu {

// ── Bundle pack / load / inspect ───────────────────────────────────────────────
bool packKernelBundle(
    const std::string& kernel_name,
    const std::string& binary_path,
    const std::string& manifest_path,
    uint32_t workgroup_x,
    uint32_t workgroup_y,
    uint32_t workgroup_z,
    uint64_t shared_mem_bytes);

bool packKernelBundle(const std::string& kernel_name, const std::string& binary_path, const std::string& manifest_path);

bool loadKernelBundle(const std::string& manifest_path);

bool inspectKernelBundle(
    const std::string& manifest_path,
    std::string& kernel_name,
    std::string& binary_path,
    uint64_t& binary_size);

// ── ELF entry point resolution ───────────────────────────────────────────────
// Scans the ELF symbol table to find the mangled name of a kernel function.
// kernel_base_name should be the undecorated name (e.g. "vector_add").
// On success, entry_symbol receives the exact mangled symbol name.
bool resolveEntrySymbol(
    const std::string& binary_path,
    const std::string& kernel_base_name,
    std::string& entry_symbol);

// Returns a human-readable dump of all global FUNC symbols in the ELF.
bool listKernelSymbols(const std::string& binary_path, std::string& symbol_report);

} // namespace riscv_gpgpu

#endif // RISCV_GPGPU_KERNEL_LOADER_H
