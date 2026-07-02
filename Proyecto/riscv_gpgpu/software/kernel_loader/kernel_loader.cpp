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

    uint64_t binary_size = fs::file_size(fs::path(binary_path));
    std::ostringstream manifest;
    manifest << "{\n";
    manifest << "  \"kernel_name\": \"" << kernel_name << "\",\n";
    manifest << "  \"binary_path\": \"" << binary_path << "\",\n";
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

bool inspectKernelBundle(
    const std::string& manifest_path,
    std::string& kernel_name,
    std::string& binary_path,
    uint64_t& binary_size) {

    std::cout << "[kernel_loader] Inspecting kernel bundle manifest " << manifest_path << "\n";
    std::ifstream manifest(manifest_path);
    if (!manifest.is_open()) {
        std::cerr << "[kernel_loader] Failed to open manifest: " << manifest_path << "\n";
        return false;
    }

    kernel_name.clear();
    binary_path.clear();
    binary_size = 0;
    uint64_t seen_size = 0;
    std::string line;
    while (std::getline(manifest, line)) {
        parseJsonStringValue(line, "kernel_name", kernel_name);
        parseJsonStringValue(line, "binary_path", binary_path);
        parseJsonUint64Value(line, "binary_size", seen_size);
    }

    if (kernel_name.empty() || binary_path.empty()) {
        std::cerr << "[kernel_loader] Manifest missing required fields.\n";
        return false;
    }
    if (!pathExists(binary_path)) {
        std::cerr << "[kernel_loader] Binary referenced in manifest is missing: " << binary_path << "\n";
        return false;
    }

    binary_size = fs::file_size(fs::path(binary_path));
    if (seen_size != 0 && seen_size != binary_size) {
        std::cerr << "[kernel_loader] Binary size mismatch: manifest=" << seen_size << " actual=" << binary_size << "\n";
        return false;
    }

    return true;
}

} // namespace riscv_gpgpu
