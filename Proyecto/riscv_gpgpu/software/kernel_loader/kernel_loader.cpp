#include "kernel_loader.h"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

namespace riscv_gpgpu {
namespace fs = std::filesystem;

static bool pathExists(const std::string& path) {
    return fs::exists(fs::path(path));
}

static bool writeManifest(const std::string& manifest_path, const std::string& content) {
    std::ofstream manifest(manifest_path);
    if (!manifest.is_open()) {
        return false;
    }
    manifest << content;
    return manifest.good();
}

static std::string trim(const std::string& value) {
    size_t start = 0;
    while (start < value.size() && isspace(static_cast<unsigned char>(value[start]))) {
        start++;
    }
    size_t end = value.size();
    while (end > start && isspace(static_cast<unsigned char>(value[end - 1]))) {
        end--;
    }
    return value.substr(start, end - start);
}

static bool parseJsonStringValue(const std::string& line, const std::string& key, std::string& out_value) {
    auto key_pos = line.find('"' + key + '"');
    if (key_pos == std::string::npos) {
        return false;
    }
    auto colon_pos = line.find(':', key_pos);
    if (colon_pos == std::string::npos) {
        return false;
    }
    auto quote_pos = line.find('"', colon_pos + 1);
    if (quote_pos == std::string::npos) {
        return false;
    }
    auto quote_end = line.find('"', quote_pos + 1);
    if (quote_end == std::string::npos) {
        return false;
    }
    out_value = line.substr(quote_pos + 1, quote_end - quote_pos - 1);
    return true;
}

static bool extractFirstFunctionSymbol(const std::string& report, std::string& symbol) {
    std::istringstream iss(report);
    std::string line;
    while (std::getline(iss, line)) {
        std::istringstream lss(line);
        std::string sym, type, addr;
        if (!(lss >> sym >> type >> addr)) continue;
        if (type == "T" || type == "t" || type == "W" || type == "w") {
            symbol = sym;
            return true;
        }
    }
    return false;
}

static bool parseJsonUint64Value(const std::string& line, const std::string& key, uint64_t& out_value) {
    auto key_pos = line.find('"' + key + '"');
    if (key_pos == std::string::npos) {
        return false;
    }
    auto colon_pos = line.find(':', key_pos);
    if (colon_pos == std::string::npos) {
        return false;
    }
    auto value_str = trim(line.substr(colon_pos + 1));
    size_t end_pos = 0;
    out_value = std::stoull(value_str, &end_pos);
    return end_pos > 0;
}

static bool parseJsonUint32Value(const std::string& line, const std::string& key, uint32_t& out_value) {
    uint64_t temp = 0;
    if (!parseJsonUint64Value(line, key, temp)) {
        return false;
    }
    out_value = static_cast<uint32_t>(temp);
    return true;
}

bool packKernelBundle(
    const std::string& kernel_name,
    const std::string& binary_path,
    const std::string& manifest_path,
    uint32_t workgroup_x,
    uint32_t workgroup_y,
    uint32_t workgroup_z,
    uint64_t shared_mem_bytes) {

    std::cout << "[kernel_loader] Packing kernel '" << kernel_name << "' from " << binary_path << " into " << manifest_path << "\n";
    if (!pathExists(binary_path)) {
        std::cerr << "[kernel_loader] Binary path not found: " << binary_path << "\n";
        return false;
    }

    std::string entry_symbol;
    if (!resolveEntrySymbol(binary_path, kernel_name, entry_symbol)) {
        std::string symbols;
        if (listKernelSymbols(binary_path, symbols)) {
            extractFirstFunctionSymbol(symbols, entry_symbol);
        }
        if (entry_symbol.empty()) {
            entry_symbol = kernel_name;
        }
    }

    uint64_t binary_size = fs::file_size(fs::path(binary_path));
    std::ostringstream manifest;
    manifest << "{\n";
    manifest << "  \"kernel_name\": \"" << kernel_name << "\",\n";
    manifest << "  \"binary_path\": \"" << binary_path << "\",\n";
    manifest << "  \"entry_symbol\": \"" << entry_symbol << "\",\n";
    manifest << "  \"binary_size\": " << binary_size << ",\n";
    manifest << "  \"workgroup_x\": " << workgroup_x << ",\n";
    manifest << "  \"workgroup_y\": " << workgroup_y << ",\n";
    manifest << "  \"workgroup_z\": " << workgroup_z << ",\n";
    manifest << "  \"shared_mem_bytes\": " << shared_mem_bytes << "\n";
    manifest << "}\n";

    return writeManifest(manifest_path, manifest.str());
}

bool packKernelBundle(const std::string& kernel_name, const std::string& binary_path, const std::string& manifest_path) {
    return packKernelBundle(kernel_name, binary_path, manifest_path, 1, 1, 1, 0);
}

bool loadKernelBundle(const std::string& manifest_path) {
    std::string kernel_name;
    std::string binary_path;
    uint64_t binary_size = 0;
    if (!inspectKernelBundle(manifest_path, kernel_name, binary_path, binary_size)) {
        return false;
    }

    std::cout << "[kernel_loader] Loaded bundle for kernel '" << kernel_name << "' (" << binary_path << ", " << binary_size << " bytes)\n";
    return true;
}

bool inspectKernelBundleDetails(
    const std::string& manifest_path,
    KernelBundleInfo& info) {

    std::cout << "[kernel_loader] Inspecting kernel bundle manifest " << manifest_path << "\n";
    std::ifstream manifest(manifest_path);
    if (!manifest.is_open()) {
        std::cerr << "[kernel_loader] Failed to open manifest: " << manifest_path << "\n";
        return false;
    }

    info = KernelBundleInfo{};
    uint64_t seen_size = 0;
    std::string line;
    while (std::getline(manifest, line)) {
        parseJsonStringValue(line, "kernel_name", info.kernel_name);
        parseJsonStringValue(line, "binary_path", info.binary_path);
        parseJsonStringValue(line, "entry_symbol", info.entry_symbol);
        parseJsonUint64Value(line, "binary_size", seen_size);
        parseJsonUint32Value(line, "workgroup_x", info.workgroup_x);
        parseJsonUint32Value(line, "workgroup_y", info.workgroup_y);
        parseJsonUint32Value(line, "workgroup_z", info.workgroup_z);
        parseJsonUint64Value(line, "shared_mem_bytes", info.shared_mem_bytes);
    }

    if (info.kernel_name.empty() || info.binary_path.empty()) {
        std::cerr << "[kernel_loader] Manifest missing required fields.\n";
        return false;
    }
    if (!pathExists(info.binary_path)) {
        std::cerr << "[kernel_loader] Binary referenced in manifest is missing: " << info.binary_path << "\n";
        return false;
    }

    info.binary_size = fs::file_size(fs::path(info.binary_path));
    if (seen_size != 0 && seen_size != info.binary_size) {
        std::cerr << "[kernel_loader] Binary size mismatch: manifest=" << seen_size << " actual=" << info.binary_size << "\n";
        return false;
    }

    return true;
}

bool inspectKernelBundle(
    const std::string& manifest_path,
    std::string& kernel_name,
    std::string& binary_path,
    uint64_t& binary_size) {

    KernelBundleInfo info;
    if (!inspectKernelBundleDetails(manifest_path, info)) {
        return false;
    }
    kernel_name = info.kernel_name;
    binary_path = info.binary_path;
    binary_size = info.binary_size;
    return true;
}

// ─── ELF entry point resolution ───────────────────────────────────────────────
//
// Uses `nm` (from binutils-riscv64-unknown-elf) to scan the symbol table.
// Falls back to `riscv64-unknown-elf-nm` then `nm` (host) in that order.
// Matches the first FUNC symbol whose demangled or mangled name contains
// kernel_base_name.

static std::string runCommand(const std::string& cmd) {
    FILE* pipe = popen(cmd.c_str(), "r");
    if (!pipe) return {};
    std::string result;
    char buf[256];
    while (fgets(buf, sizeof(buf), pipe))
        result += buf;
    pclose(pipe);
    return result;
}

static std::string detectNm() {
    // Prefer the RISC-V-targeted nm for demangled names; fall back to system nm.
    for (const auto& candidate : {
            "riscv64-unknown-elf-nm",
            "riscv32-unknown-elf-nm",
            "nm"}) {
        std::string test = std::string("which ") + candidate + " 2>/dev/null";
        std::string found = runCommand(test);
        if (!found.empty()) return candidate;
    }
    return {};
}

bool resolveEntrySymbol(
        const std::string& binary_path,
        const std::string& kernel_base_name,
        std::string& entry_symbol) {

    if (!pathExists(binary_path)) {
        std::cerr << "[kernel_loader] resolveEntrySymbol: binary not found: " << binary_path << "\n";
        return false;
    }

    std::string nm = detectNm();
    if (nm.empty()) {
        std::cerr << "[kernel_loader] resolveEntrySymbol: no nm utility found in PATH\n";
        return false;
    }

    // --defined-only -f posix: <name> <type> <value> <size>
    std::string cmd = nm + " --defined-only -f posix \"" + binary_path + "\" 2>/dev/null";
    std::string output = runCommand(cmd);
    if (output.empty()) {
        std::cerr << "[kernel_loader] resolveEntrySymbol: nm produced no output for " << binary_path << "\n";
        return false;
    }

    // Parse posix-format lines: <symbol> <type> <value> [<size>]
    // Types: T = .text (global), t = .text (local), W = weak, etc.
    // We match any symbol name that contains kernel_base_name and is a FUNC.
    std::istringstream iss(output);
    std::string line;
    while (std::getline(iss, line)) {
        std::istringstream lss(line);
        std::string sym, type, addr;
        if (!(lss >> sym >> type >> addr)) continue;
        // RISC-V nm reports T or t for text (function) symbols.
        if (type != "T" && type != "t" && type != "W" && type != "w") continue;
        if (sym.find(kernel_base_name) != std::string::npos) {
            entry_symbol = sym;
            std::cout << "[kernel_loader] Resolved entry symbol: '" << sym
                      << "' for kernel '" << kernel_base_name << "'\n";
            return true;
        }
    }

    // No match found.
    std::cerr << "[kernel_loader] resolveEntrySymbol: no symbol containing '"
              << kernel_base_name << "' found in " << binary_path << "\n";
    return false;
}

bool listKernelSymbols(const std::string& binary_path, std::string& symbol_report) {
    if (!pathExists(binary_path)) {
        std::cerr << "[kernel_loader] listKernelSymbols: binary not found: " << binary_path << "\n";
        return false;
    }

    std::string nm = detectNm();
    if (nm.empty()) {
        symbol_report = "<nm not found>";
        return false;
    }

    std::string cmd = nm + " --defined-only -f posix \"" + binary_path + "\" 2>/dev/null";
    symbol_report = runCommand(cmd);
    if (symbol_report.empty()) symbol_report = "<no symbols>";
    return true;
}

} // namespace riscv_gpgpu
