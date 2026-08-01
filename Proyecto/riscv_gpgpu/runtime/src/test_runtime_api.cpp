#include <filesystem>
#include <fstream>
#include <gtest/gtest.h>
#include "host_runtime.h"
#include "../../software/kernel_loader/kernel_loader.h"
#include "../../software/host_api/host_api.h"
#include "../../llvm/backend/llvm_backend.h"
#include "../../driver/src/loader.h"

using namespace riscv_gpgpu;
namespace fs = std::filesystem;

TEST(RuntimeApiTest, UploadPtxBundleRegistersSource) {
    const fs::path temp_dir = fs::temp_directory_path() / "riscv_gpgpu_runtime_ptx";
    fs::create_directories(temp_dir);
    const fs::path ptx_file = temp_dir / "auto_kernel.ptx";
    const fs::path manifest_file = temp_dir / "auto_kernel.json";
    const std::string ptx_source = ".address_size 32\n.visible .entry auto_kernel() { ret; }\n";
    { std::ofstream file(ptx_file); file << ptx_source; }

    ASSERT_TRUE(packPtxKernelBundle("auto_kernel", ptx_file.string(), manifest_file.string(), 8, 1, 1, 64));
    ASSERT_TRUE(uploadKernelBundle(manifest_file.string()));

    std::string registered;
    EXPECT_TRUE(gpgpuGetRegisteredPtx("auto_kernel", registered));
    EXPECT_EQ(registered, ptx_source);
    EXPECT_TRUE(unloadKernelBundle());
    EXPECT_FALSE(gpgpuGetRegisteredPtx("auto_kernel", registered));

    fs::remove_all(temp_dir);
}

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
    // Leave workgroup dims default to test bundle-provided metadata.
    info.grid_x      = 2;
    info.grid_y      = 1;
    info.grid_z      = 1;

    EXPECT_TRUE(launchKernel(info));

    KernelLaunchArgs launch_args;
    ASSERT_TRUE(getCurrentLaunchArgs(launch_args));
    EXPECT_EQ(launch_args.entry_symbol, "kernel");
    EXPECT_EQ(launch_args.block_x, 4u);
    EXPECT_EQ(launch_args.shared_mem_bytes, 512u);

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

// ── T068: ThreadContext mapping verification ──────────────────────────────────

TEST(RuntimeApiTest, ThreadContextMapping1D) {
    // grid=(4,1,1), block=(8,1,1) → 32 threads
    auto ctxs = computeThreadContexts(4, 1, 1, 8, 1, 1);
    ASSERT_EQ(ctxs.size(), 32u);
    for (uint32_t t = 0; t < 32u; ++t) {
        const auto& c = ctxs[t];
        EXPECT_EQ(c.global_id, t);
        EXPECT_EQ(c.ntid_x, 8u);
        // Reconstruct global_id from tid/ctaid
        uint32_t recomputed = c.ctaid_x * c.ntid_x + c.tid_x;
        EXPECT_EQ(recomputed, t)
            << "Thread " << t << ": ctaid_x=" << c.ctaid_x << " tid_x=" << c.tid_x;
    }
}

TEST(RuntimeApiTest, ThreadContextMapping2D) {
    // grid=(2,2,1), block=(4,4,1) → 64 threads
    auto ctxs = computeThreadContexts(2, 2, 1, 4, 4, 1);
    ASSERT_EQ(ctxs.size(), 64u);
    for (uint32_t t = 0; t < 64u; ++t) {
        const auto& c = ctxs[t];
        EXPECT_EQ(c.global_id, t);
        EXPECT_EQ(c.ntid_x, 4u);
        uint32_t blk = c.ctaid_y * 2u + c.ctaid_x;
        uint32_t thd = c.tid_y * 4u + c.tid_x;
        EXPECT_EQ(blk * 16u + thd, t) << "Thread " << t;
    }
}

TEST(RuntimeApiTest, ThreadContextSingleThread) {
    auto ctxs = computeThreadContexts(1, 1, 1, 1, 1, 1);
    ASSERT_EQ(ctxs.size(), 1u);
    EXPECT_EQ(ctxs[0].global_id, 0u);
    EXPECT_EQ(ctxs[0].tid_x, 0u);
    EXPECT_EQ(ctxs[0].ctaid_x, 0u);
}
