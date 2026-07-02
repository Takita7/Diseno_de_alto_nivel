#include <gtest/gtest.h>
#include "loader.h"

using namespace riscv_gpgpu;

TEST(DriverApiTest, LoadKernel) {
    EXPECT_TRUE(loadKernelBinary("/tmp/fake_kernel.bin"));
}

TEST(DriverApiTest, ConfigureKernel) {
    std::vector<uint64_t> args = {1, 2, 3};
    EXPECT_TRUE(configureKernel("test", args));
}

TEST(DriverApiTest, StartAndQuery) {
    EXPECT_TRUE(startKernel());
    std::string status;
    EXPECT_TRUE(queryKernelStatus(status));
    EXPECT_EQ(status, "RUNNING");
}
