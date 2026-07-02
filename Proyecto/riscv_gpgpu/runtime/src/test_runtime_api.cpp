#include <filesystem>
#include <fstream>
#include <gtest/gtest.h>
#include "host_runtime.cpp"
#include "../../software/kernel_loader/kernel_loader.h"
#include "../../llvm/backend/llvm_backend.h"

using namespace riscv_gpgpu;
namespace fs = std::filesystem;

TEST(RuntimeApiTest, UploadLaunchPoll) {
    fs::path temp_dir = fs::temp_directory_path();
    fs::path source_file  = temp_dir / "riscv_gpgpu_runtime_test.c";
    fs::path binary_file  = temp_dir / "riscv_gpgpu_runtime_test.riscv.elf";
    fs::path manifest_file = temp_dir / "riscv_gpgpu_runtime_test.json";

    std::ofstream source(source_file);
    ASSERT_TRUE(source.is_open());
    source << "void kernel() { volatile int x = 42; x += 1; }\n";
    source.close();

    EXPECT_TRUE(emitKernelBinary(source_file.string(), binary_file.string()));
    EXPECT_TRUE(fs::exists(binary_file));
    EXPECT_TRUE(packKernelBundle("runtime_test", binary_file.string(), manifest_file.string(), 4, 1, 1, 512));
    EXPECT_TRUE(uploadKernelBundle(manifest_file.string()));

    KernelLaunchInfo info;
    info.name        = "runtime_test";
    info.args        = {42, 84};
    info.workgroup_x = 4;
    info.workgroup_y = 1;
    info.workgroup_z = 1;
    info.grid_x      = 2;
    info.grid_y      = 1;
    info.grid_z      = 1;

    EXPECT_TRUE(launchKernel(info));

    std::string status;
    EXPECT_TRUE(pollKernelStatus(status));
    // After simulation completes synchronously the status is COMPLETED.
    EXPECT_EQ(status, "COMPLETED");

    fs::remove(source_file);
    fs::remove(binary_file);
    fs::remove(manifest_file);
}

TEST(RuntimeApiTest, WaitKernelCompletion) {
    fs::path temp_dir     = fs::temp_directory_path();
    fs::path source_file  = temp_dir / "riscv_gpgpu_wait_test.c";
    fs::path binary_file  = temp_dir / "riscv_gpgpu_wait_test.riscv.elf";
    fs::path manifest_file = temp_dir / "riscv_gpgpu_wait_test.json";

    std::ofstream source(source_file);
    ASSERT_TRUE(source.is_open());
    source << "int add(int a, int b) { return a + b; }\n";
    source.close();

    EXPECT_TRUE(emitKernelBinary(source_file.string(), binary_file.string()));
    EXPECT_TRUE(packKernelBundle("wait_test", binary_file.string(), manifest_file.string()));
    EXPECT_TRUE(uploadKernelBundle(manifest_file.string()));

    KernelLaunchInfo info;
    info.name   = "wait_test";
    info.grid_x = 8; info.grid_y = 1; info.grid_z = 1;
    info.workgroup_x = 32; info.workgroup_y = 1; info.workgroup_z = 1;

    EXPECT_TRUE(launchKernel(info));
    EXPECT_TRUE(waitKernelCompletion());

    fs::remove(source_file);
    fs::remove(binary_file);
    fs::remove(manifest_file);
}
