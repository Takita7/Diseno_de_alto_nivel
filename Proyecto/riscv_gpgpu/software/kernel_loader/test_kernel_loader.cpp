#include <gtest/gtest.h>
#include "kernel_loader.cpp"
#include <cstdio>

using namespace riscv_gpgpu;

TEST(KernelLoaderTest, PackAndLoad) {
    const std::string manifest = "/tmp/test_kernel_manifest.json";
    EXPECT_TRUE(packKernelBundle("example", "/tmp/example.bin", manifest));
    EXPECT_TRUE(loadKernelBundle(manifest));
    std::remove(manifest.c_str());
}
