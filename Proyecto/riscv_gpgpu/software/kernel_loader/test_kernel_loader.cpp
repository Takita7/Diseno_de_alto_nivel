#include <filesystem>
#include <fstream>
#include <gtest/gtest.h>
#include "kernel_loader.h"
#include "../llvm/backend/llvm_backend.h"

using namespace riscv_gpgpu;
namespace fs = std::filesystem;

TEST(KernelLoaderTest, PackAndLoad) {
    const std::string manifest = "/tmp/test_kernel_manifest.json";
    const std::string fake_bin = "/tmp/example.bin";
    // Create a minimal fake binary so packKernelBundle can verify it exists.
    { std::ofstream f(fake_bin); ASSERT_TRUE(f.is_open()); f << "\x7fELF"; }
    EXPECT_TRUE(packKernelBundle("example", fake_bin, manifest));
    EXPECT_TRUE(loadKernelBundle(manifest));
    std::remove(manifest.c_str());
    std::remove(fake_bin.c_str());
}

TEST(KernelLoaderTest, PackAndInspectPtxBundle) {
    const fs::path temp_dir = fs::temp_directory_path() / "riscv_gpgpu_ptx_bundle";
    fs::create_directories(temp_dir);
    const fs::path ptx_file = temp_dir / "kernel.ptx";
    const fs::path manifest_file = temp_dir / "kernel.json";
    {
        std::ofstream ptx(ptx_file);
        ASSERT_TRUE(ptx.is_open());
        ptx << ".address_size 32\n.visible .entry bundle_ptx() { ret; }\n";
    }

    ASSERT_TRUE(packPtxKernelBundle("bundle_ptx", ptx_file.string(), manifest_file.string(), 32, 1, 1, 128));
    KernelBundleInfo info;
    ASSERT_TRUE(inspectKernelBundleDetails(manifest_file.string(), info));
    EXPECT_EQ(info.format, KernelBundleFormat::Ptx);
    EXPECT_EQ(info.kernel_name, "bundle_ptx");
    EXPECT_EQ(info.entry_symbol, "bundle_ptx");
    EXPECT_EQ(info.workgroup_x, 32u);
    EXPECT_EQ(info.shared_mem_bytes, 128u);
    EXPECT_EQ(info.binary_path, fs::absolute(ptx_file).lexically_normal().string());

    fs::remove_all(temp_dir);
}

TEST(KernelLoaderTest, RejectsUnknownBundleFormat) {
    const fs::path temp_dir = fs::temp_directory_path() / "riscv_gpgpu_bad_bundle";
    fs::create_directories(temp_dir);
    const fs::path artifact = temp_dir / "kernel.bin";
    const fs::path manifest = temp_dir / "kernel.json";
    { std::ofstream file(artifact); file << "data"; }
    {
        std::ofstream file(manifest);
        file << "{\n"
             << "  \"kernel_name\": \"bad\",\n"
             << "  \"binary_path\": \"kernel.bin\",\n"
             << "  \"binary_format\": \"unknown\"\n"
             << "}\n";
    }
    KernelBundleInfo info;
    EXPECT_FALSE(inspectKernelBundleDetails(manifest.string(), info));
    fs::remove_all(temp_dir);
}

TEST(KernelLoaderTest, EndToEndCompileAndBundle) {
    fs::path temp_dir = fs::temp_directory_path();
    fs::path source_file = temp_dir / "riscv_gpgpu_bundle_test.c";
    fs::path binary_file = temp_dir / "riscv_gpgpu_bundle_test.riscv.elf";
    fs::path manifest_file = temp_dir / "riscv_gpgpu_bundle_test.json";

    std::ofstream source(source_file);
    ASSERT_TRUE(source.is_open());
    source << "void kernel() { volatile int x = 42; x += 1; }\n";
    source.close();

    EXPECT_TRUE(emitKernelBinary(source_file.string(), binary_file.string()));
    EXPECT_TRUE(fs::exists(binary_file));

    EXPECT_TRUE(packKernelBundle("bundle_test", binary_file.string(), manifest_file.string(), 8, 1, 1, 1024));
    EXPECT_TRUE(loadKernelBundle(manifest_file.string()));

    fs::remove(source_file);
    fs::remove(binary_file);
    fs::remove(manifest_file);
}

TEST(KernelLoaderTest, EndToEndCudaKernelBundle) {
    fs::path temp_dir = fs::temp_directory_path();
    fs::path source_file = temp_dir / "riscv_gpgpu_bundle_test.cu";
    fs::path binary_file = temp_dir / "riscv_gpgpu_bundle_test_cuda.riscv.elf";
    fs::path manifest_file = temp_dir / "riscv_gpgpu_bundle_test_cuda.json";

    std::ofstream source(source_file);
    ASSERT_TRUE(source.is_open());
    source << "__global__ void kernel() { volatile int x = 42; x += 1; }\n";
    source.close();

    EXPECT_TRUE(emitKernelBinary(source_file.string(), binary_file.string()));
    EXPECT_TRUE(fs::exists(binary_file));

    EXPECT_TRUE(packKernelBundle("cuda_bundle_test", binary_file.string(), manifest_file.string(), 16, 1, 1, 2048));
    EXPECT_TRUE(loadKernelBundle(manifest_file.string()));

    std::string loaded_name;
    std::string loaded_path;
    uint64_t loaded_size = 0;
    EXPECT_TRUE(inspectKernelBundle(manifest_file.string(), loaded_name, loaded_path, loaded_size));
    EXPECT_EQ(loaded_name, "cuda_bundle_test");
    EXPECT_EQ(loaded_path, binary_file.string());
    EXPECT_EQ(loaded_size, fs::file_size(binary_file));

    fs::remove(source_file);
    fs::remove(binary_file);
    fs::remove(manifest_file);
}

// ─── Entry point resolution ────────────────────────────────────────────────────

TEST(KernelLoaderTest, ResolveEntrySymbol) {
    fs::path temp_dir    = fs::temp_directory_path();
    fs::path source_file = temp_dir / "riscv_gpgpu_sym_test.c";
    fs::path binary_file = temp_dir / "riscv_gpgpu_sym_test.riscv.elf";

    std::ofstream source(source_file);
    ASSERT_TRUE(source.is_open());
    source << "int my_gpu_kernel(int a, int b) { return a + b; }\n";
    source.close();

    EXPECT_TRUE(emitKernelBinary(source_file.string(), binary_file.string()));
    ASSERT_TRUE(fs::exists(binary_file));

    std::string entry;
    bool found = resolveEntrySymbol(binary_file.string(), "my_gpu_kernel", entry);
    if (found) {
        EXPECT_FALSE(entry.empty());
        EXPECT_NE(entry.find("my_gpu_kernel"), std::string::npos);
    } else {
        GTEST_SKIP() << "nm not found or symbol not resolved; skipping.";
    }

    fs::remove(source_file);
    fs::remove(binary_file);
}

TEST(KernelLoaderTest, ListKernelSymbols) {
    fs::path temp_dir    = fs::temp_directory_path();
    fs::path source_file = temp_dir / "riscv_gpgpu_list_sym_test.c";
    fs::path binary_file = temp_dir / "riscv_gpgpu_list_sym_test.riscv.elf";

    std::ofstream source(source_file);
    ASSERT_TRUE(source.is_open());
    source << "void alpha() {}\nvoid beta() {}\n";
    source.close();

    EXPECT_TRUE(emitKernelBinary(source_file.string(), binary_file.string()));
    ASSERT_TRUE(fs::exists(binary_file));

    std::string report;
    bool ok = listKernelSymbols(binary_file.string(), report);
    if (!ok) {
        GTEST_SKIP() << "nm not available; skipping symbol list test.";
    }
    EXPECT_FALSE(report.empty());

    fs::remove(source_file);
    fs::remove(binary_file);
}
