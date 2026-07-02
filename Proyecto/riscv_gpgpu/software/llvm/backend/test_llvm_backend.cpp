#include <gtest/gtest.h>
#include "llvm_backend.cpp"

using namespace riscv_gpgpu;

TEST(LLVMBackendTest, InitializeAndEmit) {
    EXPECT_TRUE(initializeLLVMBackend());
    EXPECT_TRUE(emitKernelBinary("example_kernel"));
}
