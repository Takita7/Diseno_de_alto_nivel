#include <cstdint>
#include <gtest/gtest.h>
#include "host_api.cpp"

using namespace riscv_gpgpu;

// ─── Lifecycle ────────────────────────────────────────────────────────────────
TEST(HostApiTest, InitializeShutdown) {
    EXPECT_TRUE(initializeHostAPI());
    EXPECT_TRUE(shutdownHostAPI());
}

// ─── Device memory management ─────────────────────────────────────────────────
TEST(HostApiTest, MallocAndFree) {
    uint64_t ptr = 0;
    EXPECT_TRUE(gpgpuMalloc(ptr, 128));
    EXPECT_NE(ptr, 0u);
    EXPECT_TRUE(gpgpuFree(ptr));
}

TEST(HostApiTest, MemcpyH2DAndD2H) {
    uint64_t ptr = 0;
    ASSERT_TRUE(gpgpuMalloc(ptr, sizeof(int) * 4));

    int src[4] = {10, 20, 30, 40};
    EXPECT_TRUE(gpgpuMemcpyH2D(ptr, src, sizeof(src)));

    int dst[4] = {};
    EXPECT_TRUE(gpgpuMemcpyD2H(dst, ptr, sizeof(dst)));
    EXPECT_EQ(dst[0], 10);
    EXPECT_EQ(dst[1], 20);
    EXPECT_EQ(dst[2], 30);
    EXPECT_EQ(dst[3], 40);

    EXPECT_TRUE(gpgpuFree(ptr));
}

// ─── Kernel argument staging ───────────────────────────────────────────────────
TEST(HostApiTest, SetAndClearKernelArgument) {
    EXPECT_TRUE(clearKernelArguments());
    EXPECT_TRUE(setKernelArgument("arg0", 1234));
    EXPECT_TRUE(setKernelArgument("arg1", 5678));
    EXPECT_TRUE(clearKernelArguments());
}

// ─── Kernel launch and sync ───────────────────────────────────────────────────
TEST(HostApiTest, LaunchKernelAndSynchronize) {
    std::vector<uint64_t> args = {0x10000000ULL, 0x10004000ULL, 256};
    EXPECT_TRUE(gpgpuLaunchKernel("bench_kernel",
                                   4, 1, 1,   // grid
                                   64, 1, 1,  // block
                                   args));
    EXPECT_TRUE(gpgpuSynchronize());
}
