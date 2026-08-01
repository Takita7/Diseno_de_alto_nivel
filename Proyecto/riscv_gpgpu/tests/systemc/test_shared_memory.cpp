#include <gtest/gtest.h>

#include <cstdlib>
#include <filesystem>
#include <string>
#include <system_error>
#include <vector>

#include "kernel_bridge.h"
#include "ptx_transpiler.h"
#include "../../software/host_api/host_api.h"
#include "../../driver/src/loader.h"

namespace fs = std::filesystem;
using namespace riscv_gpgpu;
using namespace riscv_gpgpu::ptx;

namespace {

bool clangAvailable() {
    return std::system("clang --target=riscv32-unknown-elf -march=rv32imf "
                       "-mabi=ilp32f -fuse-ld=lld -nostdlib -x assembler-with-cpp "
                       "/dev/null -o /dev/null 2>/dev/null") == 0;
}

KernelBridge::Config bridgeConfig() {
    KernelBridge::Config cfg;
    cfg.num_compute_units = 2;
    cfg.threads_per_warp = 4;
    cfg.max_warps_per_cu = 8;
    cfg.max_sim_cycles = 200000;
    cfg.print_stats = false;
    return cfg;
}

const char* kReductionPtx = R"(
.version 7.0
.target sm_20
.address_size 32

.visible .entry reduce_n32(
    .param .u32 reduce_param_0,
    .param .u32 reduce_param_1
)
{
    .shared .align 4 .b32 sdata[32];
    .reg .pred %p<1>;
    .reg .u32 %r<8>;

    ld.param.u32 %r0, [reduce_param_0];
    ld.param.u32 %r1, [reduce_param_1];
    mov.u32 %r2, %tid.x;
    mul.lo.u32 %r3, %r2, 4;
    add.u32 %r4, %r0, %r3;
    ld.global.u32 %r5, [%r4];
    st.shared.u32 [%r3], %r5;
    bar.sync 0;

    setp.ge.u32 %p0, %r2, 16;
    @%p0 bra $L_reduce_16_done;
    add.u32 %r6, %r3, 64;
    ld.shared.u32 %r7, [%r6];
    add.u32 %r5, %r5, %r7;
    st.shared.u32 [%r3], %r5;
$L_reduce_16_done:
    bar.sync 0;

    setp.ge.u32 %p0, %r2, 8;
    @%p0 bra $L_reduce_8_done;
    add.u32 %r6, %r3, 32;
    ld.shared.u32 %r7, [%r6];
    add.u32 %r5, %r5, %r7;
    st.shared.u32 [%r3], %r5;
$L_reduce_8_done:
    bar.sync 0;

    setp.ge.u32 %p0, %r2, 4;
    @%p0 bra $L_reduce_4_done;
    add.u32 %r6, %r3, 16;
    ld.shared.u32 %r7, [%r6];
    add.u32 %r5, %r5, %r7;
    st.shared.u32 [%r3], %r5;
$L_reduce_4_done:
    bar.sync 0;

    setp.ge.u32 %p0, %r2, 2;
    @%p0 bra $L_reduce_2_done;
    add.u32 %r6, %r3, 8;
    ld.shared.u32 %r7, [%r6];
    add.u32 %r5, %r5, %r7;
    st.shared.u32 [%r3], %r5;
$L_reduce_2_done:
    bar.sync 0;

    setp.ge.u32 %p0, %r2, 1;
    @%p0 bra $L_reduce_1_done;
    add.u32 %r6, %r3, 4;
    ld.shared.u32 %r7, [%r6];
    add.u32 %r5, %r5, %r7;
    st.shared.u32 [%r3], %r5;
$L_reduce_1_done:
    bar.sync 0;

    setp.ne.u32 %p0, %r2, 0;
    @%p0 bra $L_reduce_end;
    st.global.u32 [%r1], %r5;
$L_reduce_end:
    ret;
}
)";

const char* kTransposePtx = R"(
.version 7.0
.target sm_20
.address_size 32

.visible .entry transpose_4x4(
    .param .u32 transpose_param_0,
    .param .u32 transpose_param_1
)
{
    .shared .align 4 .b32 tile[16];
    .reg .u32 %r<12>;

    ld.param.u32 %r0, [transpose_param_0];
    ld.param.u32 %r1, [transpose_param_1];
    mov.u32 %r2, %tid.x;
    mov.u32 %r3, %tid.y;
    mul.lo.u32 %r4, %r3, 4;
    add.u32 %r4, %r4, %r2;
    mul.lo.u32 %r5, %r4, 4;
    add.u32 %r6, %r0, %r5;
    ld.global.u32 %r7, [%r6];
    st.shared.u32 [%r5], %r7;
    bar.sync 0;

    mul.lo.u32 %r8, %r2, 4;
    add.u32 %r8, %r8, %r3;
    mul.lo.u32 %r9, %r8, 4;
    ld.shared.u32 %r10, [%r9];
    add.u32 %r11, %r1, %r5;
    st.global.u32 [%r11], %r10;
    ret;
}
)";

const char* kBarrierOrderingPtx = R"(
.version 7.0
.target sm_20
.address_size 32

.visible .entry barrier_ordering(
    .param .u32 ordering_param_0
)
{
    .shared .align 4 .b32 values[8];
    .reg .pred %p<1>;
    .reg .u32 %r<11>;

    ld.param.u32 %r0, [ordering_param_0];
    mov.u32 %r1, %tid.x;
    mov.u32 %r2, %ctaid.x;
    mul.lo.u32 %r3, %r1, 4;
    mul.lo.u32 %r4, %r2, 8;
    add.u32 %r4, %r4, %r1;
    mov.u32 %r5, 1000;
    mul.lo.u32 %r6, %r2, 256;
    add.u32 %r5, %r5, %r6;
    add.u32 %r5, %r5, %r1;
    setp.ne.u32 %p0, %r1, 7;
    @%p0 bra $L_ordering_write;
    add.u32 %r5, %r5, 0;
    add.u32 %r5, %r5, 0;
    add.u32 %r5, %r5, 0;
    add.u32 %r5, %r5, 0;
    add.u32 %r5, %r5, 0;
    add.u32 %r5, %r5, 0;
    add.u32 %r5, %r5, 0;
    add.u32 %r5, %r5, 0;
$L_ordering_write:
    st.shared.u32 [%r3], %r5;
    bar.sync 0;
    mov.u32 %r7, 28;
    ld.shared.u32 %r8, [%r7];
    mul.lo.u32 %r9, %r4, 4;
    add.u32 %r10, %r0, %r9;
    st.global.u32 [%r10], %r8;
    ret;
}
)";

class SharedMemoryTest : public ::testing::Test {
protected:
    fs::path compilePtx(const std::string& name, const char* source) {
        fs::path elf = fs::temp_directory_path() / ("riscv_gpgpu_" + name + ".elf");
        PtxTranspiler transpiler;
        if (!transpiler.compileToFile(source, elf.string())) return {};
        files_.push_back(elf);
        return elf;
    }

    void track(uint64_t ptr) {
        allocations_.push_back(ptr);
    }

    void TearDown() override {
        for (uint64_t ptr : allocations_) EXPECT_TRUE(gpgpuFree(ptr));
        for (const auto& path : files_) {
            std::error_code error;
            fs::remove(path, error);
        }
    }

private:
    std::vector<uint64_t> allocations_;
    std::vector<fs::path> files_;
};

TEST_F(SharedMemoryTest, ParallelReductionN32) {
    if (!clangAvailable()) GTEST_SKIP() << "clang riscv32 not available";

    fs::path elf = compilePtx("reduce_n32", kReductionPtx);
    ASSERT_FALSE(elf.empty());

    constexpr uint32_t n = 32;
    std::vector<uint32_t> input(n);
    for (uint32_t i = 0; i < n; ++i) input[i] = i + 1;
    uint32_t output = 0xFFFFFFFFu;

    uint64_t input_ptr = 0;
    uint64_t output_ptr = 0;
    ASSERT_TRUE(gpgpuMalloc(input_ptr, input.size() * sizeof(uint32_t)));
    track(input_ptr);
    ASSERT_TRUE(gpgpuMalloc(output_ptr, sizeof(output)));
    track(output_ptr);
    ASSERT_TRUE(gpgpuMemcpyH2D(input_ptr, input.data(), input.size() * sizeof(uint32_t)));
    ASSERT_TRUE(gpgpuMemcpyH2D(output_ptr, &output, sizeof(output)));

    KernelLaunchArgs launch;
    launch.kernel_name = "reduce_n32";
    launch.entry_symbol = "reduce_n32";
    launch.grid_x = 1;
    launch.grid_y = 1;
    launch.grid_z = 1;
    launch.block_x = n;
    launch.block_y = 1;
    launch.block_z = 1;
    launch.shared_mem_bytes = n * sizeof(uint32_t);
    launch.args = {input_ptr, output_ptr};
    ASSERT_TRUE(configureLaunch(launch));

    KernelBridge bridge(bridgeConfig());
    ASSERT_TRUE(bridge.runOnHardware(launch.kernel_name, elf.string(), launch.args,
                                     {input_ptr, output_ptr}));
    ASSERT_TRUE(gpgpuMemcpyD2H(&output, output_ptr, sizeof(output)));

    EXPECT_EQ(output, 528u);
    EXPECT_EQ(bridge.lastGridX(), 1u);
    EXPECT_EQ(bridge.lastBlockX(), n);
    EXPECT_GT(bridge.lastTotalCycles(), 0u);
    EXPECT_GT(bridge.lastTotalInstructions(), 0u);
}

TEST_F(SharedMemoryTest, MatrixTranspose4x4) {
    if (!clangAvailable()) GTEST_SKIP() << "clang riscv32 not available";

    fs::path elf = compilePtx("transpose_4x4", kTransposePtx);
    ASSERT_FALSE(elf.empty());

    constexpr uint32_t width = 4;
    std::vector<uint32_t> input = {
        0, 1, 2, 3,
        10, 11, 12, 13,
        20, 21, 22, 23,
        30, 31, 32, 33
    };
    std::vector<uint32_t> output(input.size(), 0xFFFFFFFFu);

    uint64_t input_ptr = 0;
    uint64_t output_ptr = 0;
    ASSERT_TRUE(gpgpuMalloc(input_ptr, input.size() * sizeof(uint32_t)));
    track(input_ptr);
    ASSERT_TRUE(gpgpuMalloc(output_ptr, output.size() * sizeof(uint32_t)));
    track(output_ptr);
    ASSERT_TRUE(gpgpuMemcpyH2D(input_ptr, input.data(), input.size() * sizeof(uint32_t)));
    ASSERT_TRUE(gpgpuMemcpyH2D(output_ptr, output.data(), output.size() * sizeof(uint32_t)));

    KernelLaunchArgs launch;
    launch.kernel_name = "transpose_4x4";
    launch.entry_symbol = "transpose_4x4";
    launch.grid_x = 1;
    launch.grid_y = 1;
    launch.grid_z = 1;
    launch.block_x = width;
    launch.block_y = width;
    launch.block_z = 1;
    launch.shared_mem_bytes = input.size() * sizeof(uint32_t);
    launch.args = {input_ptr, output_ptr};
    ASSERT_TRUE(configureLaunch(launch));

    KernelBridge bridge(bridgeConfig());
    ASSERT_TRUE(bridge.runOnHardware(launch.kernel_name, elf.string(), launch.args,
                                     {input_ptr, output_ptr}));
    ASSERT_TRUE(gpgpuMemcpyD2H(output.data(), output_ptr,
                               output.size() * sizeof(uint32_t)));

    for (uint32_t y = 0; y < width; ++y) {
        for (uint32_t x = 0; x < width; ++x) {
            EXPECT_EQ(output[y * width + x], input[x * width + y]);
        }
    }
    EXPECT_EQ(bridge.lastBlockX(), width);
    EXPECT_EQ(bridge.lastBlockY(), width);
}

TEST_F(SharedMemoryTest, BarrierOrderingPerBlock) {
    if (!clangAvailable()) GTEST_SKIP() << "clang riscv32 not available";

    fs::path elf = compilePtx("barrier_ordering", kBarrierOrderingPtx);
    ASSERT_FALSE(elf.empty());

    constexpr uint32_t grid_x = 2;
    constexpr uint32_t block_x = 8;
    constexpr uint32_t n = grid_x * block_x;
    std::vector<uint32_t> output(n, 0xFFFFFFFFu);

    uint64_t output_ptr = 0;
    ASSERT_TRUE(gpgpuMalloc(output_ptr, output.size() * sizeof(uint32_t)));
    track(output_ptr);
    ASSERT_TRUE(gpgpuMemcpyH2D(output_ptr, output.data(), output.size() * sizeof(uint32_t)));

    KernelLaunchArgs launch;
    launch.kernel_name = "barrier_ordering";
    launch.entry_symbol = "barrier_ordering";
    launch.grid_x = grid_x;
    launch.grid_y = 1;
    launch.grid_z = 1;
    launch.block_x = block_x;
    launch.block_y = 1;
    launch.block_z = 1;
    launch.shared_mem_bytes = block_x * sizeof(uint32_t);
    launch.args = {output_ptr};
    ASSERT_TRUE(configureLaunch(launch));

    KernelBridge bridge(bridgeConfig());
    ASSERT_TRUE(bridge.runOnHardware(launch.kernel_name, elf.string(), launch.args,
                                     {output_ptr}));
    ASSERT_TRUE(gpgpuMemcpyD2H(output.data(), output_ptr,
                               output.size() * sizeof(uint32_t)));

    for (uint32_t block = 0; block < grid_x; ++block) {
        const uint32_t expected = 1000u + block * 256u + 7u;
        for (uint32_t thread = 0; thread < block_x; ++thread) {
            EXPECT_EQ(output[block * block_x + thread], expected);
        }
    }
}

}
