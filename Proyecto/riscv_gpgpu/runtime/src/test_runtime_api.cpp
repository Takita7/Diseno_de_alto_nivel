#include <gtest/gtest.h>
#include "host_runtime.cpp"

using namespace riscv_gpgpu;

TEST(RuntimeApiTest, UploadLaunchPoll) {
    KernelLaunchInfo info;
    info.name = "test_kernel";
    info.args = {42, 84};
    info.workgroup_x = 4;
    info.workgroup_y = 4;
    info.workgroup_z = 1;

    EXPECT_TRUE(uploadKernel(info));
    EXPECT_TRUE(launchKernel(info));
    std::string status;
    EXPECT_TRUE(pollKernelStatus(status));
    EXPECT_EQ(status, "COMPLETED");
}
