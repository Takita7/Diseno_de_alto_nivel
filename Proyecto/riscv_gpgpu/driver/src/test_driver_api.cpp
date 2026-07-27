#include <cstring>
#include <gtest/gtest.h>
#include "loader.h"

using namespace riscv_gpgpu;

// ─── Binary loading ────────────────────────────────────────────────────────────
TEST(DriverApiTest, LoadKernel) {
    EXPECT_TRUE(loadKernelBinary("/tmp/fake_kernel.bin"));
}

// ─── Device buffer allocation and copy ───────────────────────────────────────
TEST(DriverApiTest, AllocateFreeBuffer) {
    uint64_t ptr = 0;
    EXPECT_TRUE(allocateDeviceBuffer(ptr, 256));
    EXPECT_NE(ptr, 0u);
    EXPECT_TRUE(freeDeviceBuffer(ptr));
}

TEST(DriverApiTest, AllocateZeroFails) {
    uint64_t ptr = 0;
    EXPECT_FALSE(allocateDeviceBuffer(ptr, 0));
}

TEST(DriverApiTest, CopyHostToDeviceAndBack) {
    uint64_t ptr = 0;
    ASSERT_TRUE(allocateDeviceBuffer(ptr, 64));

    uint8_t src[64];
    for (int i = 0; i < 64; ++i) src[i] = static_cast<uint8_t>(i);
    EXPECT_TRUE(copyHostToDevice(ptr, src, 64));

    uint8_t dst[64];
    std::memset(dst, 0, 64);
    EXPECT_TRUE(copyDeviceToHost(dst, ptr, 64));
    EXPECT_EQ(std::memcmp(src, dst, 64), 0);

    EXPECT_TRUE(freeDeviceBuffer(ptr));
}

TEST(DriverApiTest, CopyUnknownDeviceAddrFails) {
    const void* src = "hello";
    EXPECT_FALSE(copyHostToDevice(0xDEADC0DE, src, 5));
}

// ─── Structured launch and completion ────────────────────────────────────────
TEST(DriverApiTest, ConfigureLaunchAndStart) {
    KernelLaunchArgs la;
    la.kernel_name = "test_kernel";
    la.grid_x = 4;  la.grid_y = 1;  la.grid_z = 1;
    la.block_x = 32; la.block_y = 1; la.block_z = 1;
    la.args = {0x10000000ULL, 0x10004000ULL, 128};
    EXPECT_TRUE(configureLaunch(la));
    EXPECT_TRUE(startKernel());
}

TEST(DriverApiTest, PollCompletion) {
    KernelLaunchArgs la;
    la.kernel_name = "test_poll";
    la.args = {};
    EXPECT_TRUE(configureLaunch(la));
    EXPECT_TRUE(startKernel());

    bool completed = false;
    EXPECT_TRUE(pollKernelCompletion(completed));
    EXPECT_TRUE(completed);
}

TEST(DriverApiTest, StatusAfterCompletion) {
    KernelLaunchArgs la;
    la.kernel_name = "test_status";
    EXPECT_TRUE(configureLaunch(la));
    EXPECT_TRUE(startKernel());

    std::string status;
    EXPECT_TRUE(queryKernelStatus(status));
    EXPECT_EQ(status, "COMPLETED");
}

// ─── Legacy shim still works ──────────────────────────────────────────────────
TEST(DriverApiTest, LegacyConfigureKernel) {
    std::vector<uint64_t> args = {1, 2, 3};
    EXPECT_TRUE(configureKernel("legacy_kernel", args));
    EXPECT_TRUE(startKernel());
}
