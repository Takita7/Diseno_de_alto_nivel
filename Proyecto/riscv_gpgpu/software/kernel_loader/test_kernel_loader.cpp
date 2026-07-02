#include <filesystem>
#include <fstream>
#include <gtest/gtest.h>
#include "kernel_loader.cpp"
#include "../llvm/backend/llvm_backend.h"

using namespace riscv_gpgpu;
namespace fs = std::filesystem;

TEST(KernelLoaderTest, PackAndLoad) {
    const std::string manifest = "/tmp/test_kernel_manifest.json";
    EXPECT_TRUE(packKernelBundle("example", "/tmp/example.bin", manifest));
    EXPECT_TRUE(loadKernelBundle(manifest));
    std::remove(manifest.c_str());
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
