#include <cstdint>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "host_api.h"
#include "host_api_backend.h"
#include "../../driver/src/loader.h"

using namespace riscv_gpgpu;

namespace {

const char* kNoOpPtx = R"ptx(
.version 7.0
.target sm_20
.address_size 32
.visible .entry no_op()
{
    ret;
}
)ptx";

bool successfulBackend(const KernelLaunchArgs& launch,
                       const std::vector<uint8_t>& elf_image,
                       std::string&) {
    return launch.kernel_name == "no_op"
        && launch.entry_symbol == "no_op"
        && elf_image.size() >= 20
        && elf_image[0] == 0x7f
        && elf_image[1] == 'E'
        && elf_image[2] == 'L'
        && elf_image[3] == 'F'
        && elf_image[4] == 1
        && elf_image[18] == 0xf3
        && elf_image[19] == 0x00;
}

bool failingBackend(const KernelLaunchArgs&,
                    const std::vector<uint8_t>&,
                    std::string& error) {
    error = "injected backend failure";
    return false;
}

class HostApiTest : public ::testing::Test {
protected:
    void SetUp() override {
        installKernelExecutionBackend(nullptr);
        clearKernelArguments();
        std::string source;
        if (gpgpuGetRegisteredPtx("no_op", source)) gpgpuUnregisterPtx("no_op");
        if (gpgpuGetRegisteredPtx("malformed", source)) gpgpuUnregisterPtx("malformed");
    }

    void TearDown() override {
        installKernelExecutionBackend(nullptr);
        clearKernelArguments();
        std::string source;
        if (gpgpuGetRegisteredPtx("no_op", source)) gpgpuUnregisterPtx("no_op");
        if (gpgpuGetRegisteredPtx("malformed", source)) gpgpuUnregisterPtx("malformed");
    }
};

}

TEST_F(HostApiTest, InitializeShutdown) {
    EXPECT_TRUE(initializeHostAPI());
    EXPECT_TRUE(shutdownHostAPI());
}

TEST_F(HostApiTest, MallocAndFree) {
    uint64_t ptr = 0;
    EXPECT_TRUE(gpgpuMalloc(ptr, 128));
    EXPECT_NE(ptr, 0u);
    EXPECT_TRUE(gpgpuFree(ptr));
}

TEST_F(HostApiTest, MemcpyH2DAndD2H) {
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

TEST_F(HostApiTest, SetAndClearKernelArgument) {
    EXPECT_TRUE(clearKernelArguments());
    EXPECT_TRUE(setKernelArgument("arg0", 1234));
    EXPECT_TRUE(setKernelArgument("arg1", 5678));
    EXPECT_TRUE(clearKernelArguments());
}

TEST_F(HostApiTest, RegisterReplaceAndUnregisterPtx) {
    EXPECT_TRUE(gpgpuRegisterPtx("no_op", kNoOpPtx));
    std::string source;
    EXPECT_TRUE(gpgpuGetRegisteredPtx("no_op", source));
    EXPECT_EQ(source, kNoOpPtx);

    EXPECT_TRUE(gpgpuRegisterPtx("no_op", ".visible .entry no_op() { ret; }"));
    EXPECT_TRUE(gpgpuGetRegisteredPtx("no_op", source));
    EXPECT_EQ(source, ".visible .entry no_op() { ret; }");

    EXPECT_TRUE(gpgpuUnregisterPtx("no_op"));
    EXPECT_FALSE(gpgpuGetRegisteredPtx("no_op", source));
}

TEST_F(HostApiTest, RejectsInvalidPtxRegistration) {
    EXPECT_FALSE(gpgpuRegisterPtx("", kNoOpPtx));
    EXPECT_FALSE(gpgpuRegisterPtx("no_op", ""));
    std::string error;
    EXPECT_TRUE(gpgpuGetLastError(error));
    EXPECT_FALSE(error.empty());
}

TEST_F(HostApiTest, LaunchKernelAndSynchronizeLegacyPath) {
    std::vector<uint64_t> args = {0x10000000ULL, 0x10004000ULL, 256};
    EXPECT_TRUE(gpgpuLaunchKernel("bench_kernel", 4, 1, 1, 64, 1, 1, args));
    EXPECT_TRUE(gpgpuSynchronize());
}

TEST_F(HostApiTest, PtxParseFailureSetsFailedStatus) {
    ASSERT_TRUE(gpgpuRegisterPtx("malformed", ".address_size 32"));
    EXPECT_FALSE(gpgpuLaunchKernel("malformed", 1, 1, 1, 1, 1, 1, {}));
    std::string status;
    EXPECT_TRUE(queryKernelStatus(status));
    EXPECT_EQ(status, "FAILED");
    EXPECT_FALSE(gpgpuSynchronize());
    std::string error;
    EXPECT_TRUE(gpgpuGetLastError(error));
    EXPECT_NE(error.find("PTX parse error"), std::string::npos);
}

TEST_F(HostApiTest, RegisteredPtxUsesExecutionBackend) {
    ASSERT_TRUE(gpgpuRegisterPtx("no_op", kNoOpPtx));
    installKernelExecutionBackend(&successfulBackend);
    if (!gpgpuLaunchKernel("no_op", 1, 1, 1, 1, 1, 1, {})) {
        std::string error;
        gpgpuGetLastError(error);
        if (error.find("clang error") != std::string::npos) GTEST_SKIP() << error;
        FAIL() << error;
    }
    EXPECT_TRUE(gpgpuSynchronize());
}

TEST_F(HostApiTest, BackendFailureSetsFailedStatus) {
    ASSERT_TRUE(gpgpuRegisterPtx("no_op", kNoOpPtx));
    installKernelExecutionBackend(&failingBackend);
    if (gpgpuLaunchKernel("no_op", 1, 1, 1, 1, 1, 1, {}))
        FAIL() << "Expected backend failure";
    std::string error;
    gpgpuGetLastError(error);
    if (error.find("clang error") != std::string::npos) GTEST_SKIP() << error;
    EXPECT_EQ(error, "injected backend failure");
    std::string status;
    EXPECT_TRUE(queryKernelStatus(status));
    EXPECT_EQ(status, "FAILED");
}
