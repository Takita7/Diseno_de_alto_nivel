// test_thread_mapping.cpp — Phase 5f: Thread mapping integration test (T070)
//
// Verifies that the full grid/block/thread → THREAD_CTX → kernel chain works
// correctly end-to-end:
//   1. PTX tid_printer kernel compiled via PtxTranspiler → RISC-V ELF
//   2. Launched via KernelBridge with specified grid/block
//   3. Each thread t reads %tid.x and %ctaid.x, computes global_tid, writes
//      output[global_tid] = global_tid
//   4. After all threads complete: output[i] == i  ∀ i ∈ [0, N)
//
// Uses the THREAD_CTX injection added in T064 and the computeThreadContexts
// utility added in T068.
//
// Requires: clang with riscv32 target + lld (for PtxTranspiler::compileToFile)
//           SystemC (for KernelBridge / MemoryHierarchy / ComputeUnit)

#include <gtest/gtest.h>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <vector>

#include "kernel_bridge.h"
#include "ptx_transpiler.h"
#include "../../software/host_api/host_api.h"
#include "../../driver/src/loader.h"
#include "../../runtime/src/host_runtime.h"

namespace fs = std::filesystem;
using namespace riscv_gpgpu;
using namespace riscv_gpgpu::ptx;

// ── Helpers ───────────────────────────────────────────────────────────────────

static bool clangAvailable() {
    return std::system("clang --target=riscv32-unknown-elf -march=rv32imf "
                       "-mabi=ilp32f -fuse-ld=lld -nostdlib -x assembler-with-cpp "
                       "/dev/null -o /dev/null 2>/dev/null") == 0;
}

// ── PTX tid_printer kernel ────────────────────────────────────────────────────
//
// Each thread:
//   1. Reads %tid.x, %ctaid.x, %ntid.x from THREAD_CTX (gp-relative)
//   2. Computes global_tid = ctaid.x * ntid.x + tid.x
//   3. Writes output[global_tid] = global_tid

static const char* kTidPrinterPtx = R"(
.version 7.0
.target sm_20
.address_size 32

.visible .entry tid_printer(
    .param .u32 tp_param_0,
    .param .u32 tp_param_1
)
{
    .reg .pred  %p<2>;
    .reg .u32   %r<8>;

    ld.param.u32    %r0, [tp_param_0];
    ld.param.u32    %r1, [tp_param_1];

    mov.u32         %r2, %tid.x;
    mov.u32         %r3, %ctaid.x;
    mov.u32         %r4, %ntid.x;

    mul.lo.u32      %r5, %r3, %r4;
    add.u32         %r5, %r5, %r2;

    setp.ge.u32     %p0, %r5, %r1;
    @%p0 bra        $L__tp_end;

    mul.lo.u32      %r6, %r5, 4;
    add.u32         %r7, %r0, %r6;
    st.global.u32   [%r7], %r5;

$L__tp_end:
    ret;
}
)";

// ── Compile PTX → ELF (shared across tests) ───────────────────────────────────

static fs::path compileTidPrinter() {
    fs::path elf = fs::temp_directory_path() / "tid_printer_test.elf";
    PtxTranspiler tx;
    bool ok = tx.compileToFile(kTidPrinterPtx, elf.string());
    if (!ok) return {};
    return elf;
}

// ── Test 1: 1D launch — N=32 threads, grid=(4,1,1), block=(8,1,1) ─────────────

TEST(ThreadMapping, TidPrinter1D_N32) {
    if (!clangAvailable()) GTEST_SKIP() << "clang riscv32 not available";

    fs::path elf = compileTidPrinter();
    ASSERT_FALSE(elf.empty()) << "PtxTranspiler failed to compile tid_printer";

    constexpr uint32_t N       = 32;
    constexpr uint32_t GRID_X  = 4;
    constexpr uint32_t BLOCK_X = 8;

    // ── Set up device buffer ────────────────────────────────────────────────
    uint64_t out_ptr = 0;
    ASSERT_TRUE(gpgpuMalloc(out_ptr, N * sizeof(uint32_t)));

    // Init output to 0xFFFF to detect un-written slots
    std::vector<uint32_t> host_out(N, 0xFFFFFFFFu);
    ASSERT_TRUE(gpgpuMemcpyH2D(out_ptr, host_out.data(), N * sizeof(uint32_t)));

    // ── Configure launch ────────────────────────────────────────────────────
    KernelLaunchArgs launch_args;
    launch_args.kernel_name   = "tid_printer";
    launch_args.entry_symbol  = "tid_printer";
    launch_args.grid_x  = GRID_X;  launch_args.grid_y  = 1; launch_args.grid_z  = 1;
    launch_args.block_x = BLOCK_X; launch_args.block_y = 1; launch_args.block_z = 1;
    launch_args.args = { out_ptr, static_cast<uint64_t>(N) };
    ASSERT_TRUE(configureLaunch(launch_args));

    // ── Run via KernelBridge (scalar path — one independent CU per thread) ──
    KernelBridge::Config cfg;
    cfg.num_compute_units = 4;        // run up to 4 threads in parallel
    cfg.threads_per_warp  = 1;        // scalar path, no lockstep overhead
    cfg.max_warps_per_cu  = 1;
    cfg.max_sim_cycles    = 100000;
    cfg.print_stats       = false;

    KernelBridge bridge(cfg);
    ASSERT_TRUE(bridge.runOnHardware(
        "tid_printer", elf.string(),
        launch_args.args, { out_ptr }));

    // ── D2H + verify: output[i] == i ────────────────────────────────────────
    ASSERT_TRUE(gpgpuMemcpyD2H(host_out.data(), out_ptr, N * sizeof(uint32_t)));

    for (uint32_t i = 0; i < N; ++i) {
        EXPECT_EQ(host_out[i], i)
            << "output[" << i << "] = " << host_out[i] << ", expected " << i;
    }

    // Stats sanity
    EXPECT_EQ(bridge.lastGridX(),  GRID_X);
    EXPECT_EQ(bridge.lastBlockX(), BLOCK_X);
    EXPECT_GT(bridge.lastTotalCycles(), 0u);

    // Verify ThreadContext utility gives the same mapping
    auto ctxs = computeThreadContexts(GRID_X, 1, 1, BLOCK_X, 1, 1);
    ASSERT_EQ(ctxs.size(), N);
    for (uint32_t i = 0; i < N; ++i) {
        uint32_t expected_global = ctxs[i].ctaid_x * ctxs[i].ntid_x + ctxs[i].tid_x;
        EXPECT_EQ(expected_global, i);
    }

    EXPECT_TRUE(gpgpuFree(out_ptr));
    fs::remove(elf);
}

// ── Test 2: 1D launch — N=64 threads, grid=(8,1,1), block=(8,1,1) ─────────────

TEST(ThreadMapping, TidPrinter1D_N64) {
    if (!clangAvailable()) GTEST_SKIP() << "clang riscv32 not available";

    fs::path elf = compileTidPrinter();
    ASSERT_FALSE(elf.empty());

    constexpr uint32_t N       = 64;
    constexpr uint32_t GRID_X  = 8;
    constexpr uint32_t BLOCK_X = 8;

    uint64_t out_ptr = 0;
    ASSERT_TRUE(gpgpuMalloc(out_ptr, N * sizeof(uint32_t)));

    std::vector<uint32_t> host_out(N, 0xFFFFFFFFu);
    ASSERT_TRUE(gpgpuMemcpyH2D(out_ptr, host_out.data(), N * sizeof(uint32_t)));

    KernelLaunchArgs launch_args;
    launch_args.kernel_name  = "tid_printer";
    launch_args.entry_symbol = "tid_printer";
    launch_args.grid_x  = GRID_X;  launch_args.grid_y  = 1; launch_args.grid_z  = 1;
    launch_args.block_x = BLOCK_X; launch_args.block_y = 1; launch_args.block_z = 1;
    launch_args.args = { out_ptr, static_cast<uint64_t>(N) };
    ASSERT_TRUE(configureLaunch(launch_args));

    KernelBridge::Config cfg;
    cfg.num_compute_units = 8;
    cfg.threads_per_warp  = 1;
    cfg.max_warps_per_cu  = 1;
    cfg.max_sim_cycles    = 100000;
    cfg.print_stats       = false;

    KernelBridge bridge(cfg);
    ASSERT_TRUE(bridge.runOnHardware(
        "tid_printer", elf.string(),
        launch_args.args, { out_ptr }));

    ASSERT_TRUE(gpgpuMemcpyD2H(host_out.data(), out_ptr, N * sizeof(uint32_t)));

    for (uint32_t i = 0; i < N; ++i) {
        EXPECT_EQ(host_out[i], i)
            << "output[" << i << "] = " << host_out[i];
    }

    EXPECT_TRUE(gpgpuFree(out_ptr));
    fs::remove(elf);
}

// ── Test 3: Thread count > num_compute_units (time-multiplexing) ──────────────
//
// Verifies T069: when totalThreads > numCUs, threads are scheduled in rounds.
// Uses 16 threads but only 4 CUs → 4 rounds of 4 threads each.

TEST(ThreadMapping, TidPrinterTimeMultiplexed) {
    if (!clangAvailable()) GTEST_SKIP() << "clang riscv32 not available";

    fs::path elf = compileTidPrinter();
    ASSERT_FALSE(elf.empty());

    constexpr uint32_t N       = 16;
    constexpr uint32_t GRID_X  = 4;
    constexpr uint32_t BLOCK_X = 4;

    uint64_t out_ptr = 0;
    ASSERT_TRUE(gpgpuMalloc(out_ptr, N * sizeof(uint32_t)));

    std::vector<uint32_t> host_out(N, 0xFFFFFFFFu);
    ASSERT_TRUE(gpgpuMemcpyH2D(out_ptr, host_out.data(), N * sizeof(uint32_t)));

    KernelLaunchArgs launch_args;
    launch_args.kernel_name  = "tid_printer";
    launch_args.entry_symbol = "tid_printer";
    launch_args.grid_x  = GRID_X;  launch_args.grid_y  = 1; launch_args.grid_z  = 1;
    launch_args.block_x = BLOCK_X; launch_args.block_y = 1; launch_args.block_z = 1;
    launch_args.args = { out_ptr, static_cast<uint64_t>(N) };
    ASSERT_TRUE(configureLaunch(launch_args));

    KernelBridge::Config cfg;
    cfg.num_compute_units = 2;   // only 2 CUs → 8 rounds of 2 threads
    cfg.threads_per_warp  = 1;
    cfg.max_warps_per_cu  = 1;
    cfg.max_sim_cycles    = 100000;
    cfg.print_stats       = false;

    KernelBridge bridge(cfg);
    ASSERT_TRUE(bridge.runOnHardware(
        "tid_printer", elf.string(),
        launch_args.args, { out_ptr }));

    ASSERT_TRUE(gpgpuMemcpyD2H(host_out.data(), out_ptr, N * sizeof(uint32_t)));

    for (uint32_t i = 0; i < N; ++i) {
        EXPECT_EQ(host_out[i], i)
            << "output[" << i << "] = " << host_out[i];
    }

    EXPECT_TRUE(gpgpuFree(out_ptr));
    fs::remove(elf);
}
