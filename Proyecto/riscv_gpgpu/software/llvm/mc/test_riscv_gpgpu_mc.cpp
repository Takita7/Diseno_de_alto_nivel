#include <gtest/gtest.h>
#include <vector>
#include "riscv_gpgpu_mc.h"

using namespace riscv_gpgpu;

TEST(LLVMMC, AssembleProducesObject) {
    std::vector<uint8_t> binary;
    EXPECT_TRUE(assembleKernelASM("addi x1, x2, 4\n", binary));
    EXPECT_FALSE(binary.empty());
}

TEST(LLVMMC, LinkProducesExecutable) {
    std::vector<uint8_t> first_object;
    std::vector<uint8_t> second_object;
    EXPECT_TRUE(assembleKernelASM("addi x1, x2, 4\n", first_object));
    EXPECT_TRUE(assembleKernelASM("addi x3, x4, 8\n", second_object));

    std::vector<std::vector<uint8_t>> objects{first_object, second_object};
    std::vector<uint8_t> executable;
    EXPECT_TRUE(linkKernelObjects(objects, executable));
    EXPECT_FALSE(executable.empty());
}
