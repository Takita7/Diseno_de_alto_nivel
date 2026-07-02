#include <gtest/gtest.h>
#include "host_api.cpp"

using namespace riscv_gpgpu;

TEST(HostApiTest, InitializeShutdown) {
    EXPECT_TRUE(initializeHostAPI());
    EXPECT_TRUE(shutdownHostAPI());
}

TEST(HostApiTest, SetKernelArgument) {
    EXPECT_TRUE(setKernelArgument("arg0", 1234));
}
