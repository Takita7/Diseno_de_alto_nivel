#include <filesystem>
#include <fstream>

#include <gtest/gtest.h>
#include "llvm_backend.h"

using namespace riscv_gpgpu;
namespace fs = std::filesystem;

TEST(LLVMBackendTest, InitializeAndEmit) {
    EXPECT_TRUE(initializeLLVMBackend());

    fs::path temp_dir = fs::temp_directory_path();
    fs::path source_file = temp_dir / "riscv_gpgpu_test_kernel.c";
    fs::path output_file = temp_dir / "riscv_gpgpu_test_kernel.elf";

    std::ofstream source(source_file);
    ASSERT_TRUE(source.is_open());
    source << "void kernel() { volatile int x = 42; x += 1; }\n";
    source.close();

    EXPECT_TRUE(emitKernelBinary(source_file.string(), output_file.string()));
    EXPECT_TRUE(fs::exists(output_file));
    EXPECT_GT(fs::file_size(output_file), 0u);

    fs::remove(source_file);
    fs::remove(output_file);
}
