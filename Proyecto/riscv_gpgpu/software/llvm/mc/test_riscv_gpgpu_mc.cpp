#include <gtest/gtest.h>
#include <vector>
#include "riscv_gpgpu_mc.h"

using namespace riscv_gpgpu;

TEST(LLVMMCStub, AssembleProducesBinary) {
    std::vector<uint8_t> binary;
    EXPECT_TRUE(assembleKernelASM("addi x1, x2, 4", binary));
    EXPECT_FALSE(binary.empty());
}

TEST(LLVMMCStub, LinkConcatenatesObjects) {
    std::vector<uint8_t> obj1{0xAA, 0xBB};
    std::vector<uint8_t> obj2{0xCC, 0xDD};
    std::vector<std::vector<uint8_t>> objects{obj1, obj2};
    std::vector<uint8_t> executable;
    EXPECT_TRUE(linkKernelObjects(objects, executable));
    EXPECT_EQ(executable.size(), obj1.size() + obj2.size());
    EXPECT_EQ(executable[0], 0xAA);
    EXPECT_EQ(executable[3], 0xDD);
}
