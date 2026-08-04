// ptx_kernels_benchmark.cpp — Full-stack PTX benchmark
//
// Exercises Phases 5d + 5e + 5f end-to-end:
//   Phase 5d: PtxTranspiler::compile()  PTX → RISC-V ELF
//   Phase 5e: RV32F FP in ComputeUnit  (saxpy uses fmadd.s)
//   Phase 5f: THREAD_CTX grid/block/thread mapping (each thread reads %tid.x)
//
// Two workloads:
//   vector_add  – N integer elements, c[i] = a[i] + b[i]
//   saxpy       – N float elements,   z[i] = a * x[i] + y[i]
//
// Each benchmark:
//   1. Compiles PTX → RISC-V ELF via PtxTranspiler
//   2. Allocates device buffers, H2D
//   3. Launches via KernelBridge with grid=(N/BLOCK,1,1), block=(BLOCK,1,1)
//   4. D2H + correctness check
//   5. Prints metrics: cycles, instructions, IPC, L1 hit rate, divergence
//
// Registered with CTest so it runs with `ctest`.
// Requires clang with riscv32 target — skips gracefully if unavailable.

#include <gtest/gtest.h>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <vector>

#include "kernel_bridge.h"
#include "ptx_transpiler.h"
#include "../../software/host_api/host_api.h"
#include "../../driver/src/loader.h"

namespace fs = std::filesystem;
using namespace riscv_gpgpu;
using namespace riscv_gpgpu::ptx;

// ── Helpers ────────────────────────────────────────────────────────────────────

static bool clangAvailable() {
    return std::system("clang --target=riscv32-unknown-elf -march=rv32imf "
                       "-mabi=ilp32f -fuse-ld=lld -nostdlib -x assembler-with-cpp "
                       "/dev/null -o /dev/null 2>/dev/null") == 0;
}

static void printMetrics(const std::string& name, const KernelBridge& bridge,
                         uint32_t N, bool fp) {
    uint64_t cyc  = bridge.lastTotalCycles();
    uint64_t ins  = bridge.lastTotalInstructions();
    double   ipc  = (cyc > 0) ? static_cast<double>(ins) / static_cast<double>(cyc) : 0.0;
    double   hrate = 0.0;
    uint32_t hits  = bridge.lastL1Hits();
    uint32_t miss  = bridge.lastL1Misses();
    if (hits + miss > 0)
        hrate = 100.0 * static_cast<double>(hits) / static_cast<double>(hits + miss);

    std::cout << "\n"
              << "  ┌─ Benchmark: " << name << " (N=" << N << (fp ? ", FP" : ", INT") << ")\n"
              << "  │  Cycles:       " << std::setw(10) << cyc << "\n"
              << "  │  Instructions: " << std::setw(10) << ins << "\n"
              << "  │  IPC:          " << std::fixed << std::setprecision(3) << ipc << "\n"
              << "  │  L1 hit rate:  " << std::fixed << std::setprecision(1) << hrate << "%"
              << "  (" << hits << "/" << (hits+miss) << ")\n"
              << "  │  Divergence:   " << bridge.lastDivergenceEvents() << " events\n"
              << "  │  Grid:  " << bridge.lastGridX() << "×" << bridge.lastGridY() << "×" << bridge.lastGridZ() << "\n"
              << "  │  Block: " << bridge.lastBlockX() << "×" << bridge.lastBlockY() << "×" << bridge.lastBlockZ() << "\n"
              << "  └─ Entry:  " << bridge.lastEntrySymbol() << "\n\n";
}

// ── PTX kernels ────────────────────────────────────────────────────────────────

// Per-thread vector_add: c[i] = a[i] + b[i]
static const char* kVectorAddPtx = R"PTX(
.version 7.0
.target sm_20
.address_size 32

.visible .entry vector_add(
    .param .u32 va_p0,
    .param .u32 va_p1,
    .param .u32 va_p2,
    .param .u32 va_p3
)
{
    .reg .pred  %p<2>;
    .reg .u32   %r<16>;

    ld.param.u32    %r0, [va_p0];
    ld.param.u32    %r1, [va_p1];
    ld.param.u32    %r2, [va_p2];
    ld.param.u32    %r3, [va_p3];

    mov.u32         %r4, %tid.x;
    mov.u32         %r5, %ctaid.x;
    mov.u32         %r6, %ntid.x;

    mul.lo.u32      %r7, %r5, %r6;
    add.u32         %r7, %r7, %r4;

    setp.ge.u32     %p0, %r7, %r3;
    @%p0 bra        $L__va_end;

    mul.lo.u32      %r8, %r7, 4;

    add.u32         %r9, %r0, %r8;
    ld.global.u32   %r10, [%r9];

    add.u32         %r11, %r1, %r8;
    ld.global.u32   %r12, [%r11];

    add.u32         %r10, %r10, %r12;

    add.u32         %r13, %r2, %r8;
    st.global.u32   [%r13], %r10;

$L__va_end:
    ret;
}
)PTX";

// Per-thread SAXPY: z[i] = a * x[i] + y[i]  (uses RV32F fmadd.s)
static const char* kSaxpyPtx = R"PTX(
.version 7.0
.target sm_20
.address_size 32

.visible .entry saxpy(
    .param .f32 saxpy_p0,
    .param .u32 saxpy_p1,
    .param .u32 saxpy_p2,
    .param .u32 saxpy_p3,
    .param .u32 saxpy_p4
)
{
    .reg .pred  %p<2>;
    .reg .u32   %r<12>;
    .reg .f32   %f<4>;

    ld.param.f32    %f0, [saxpy_p0];
    ld.param.u32    %r0, [saxpy_p1];
    ld.param.u32    %r1, [saxpy_p2];
    ld.param.u32    %r2, [saxpy_p3];
    ld.param.u32    %r3, [saxpy_p4];

    mov.u32         %r4, %tid.x;
    mov.u32         %r5, %ctaid.x;
    mov.u32         %r6, %ntid.x;

    mul.lo.u32      %r7, %r5, %r6;
    add.u32         %r7, %r7, %r4;

    setp.ge.u32     %p0, %r7, %r3;
    @%p0 bra        $L__saxpy_end;

    mul.lo.u32      %r8, %r7, 4;

    add.u32         %r9, %r0, %r8;
    ld.global.f32   %f1, [%r9];

    add.u32         %r10, %r1, %r8;
    ld.global.f32   %f2, [%r10];

    fma.rn.f32      %f3, %f0, %f1, %f2;

    add.u32         %r11, %r2, %r8;
    st.global.f32   [%r11], %f3;

$L__saxpy_end:
    ret;
}
)PTX";

// ── Benchmark 1: vector_add (integer, per-thread, PTX) ────────────────────────

TEST(PtxBenchmark, VectorAddPerThread) {
    if (!clangAvailable())
        GTEST_SKIP() << "clang riscv32 target not available";

    // ── 1. Transpile PTX → RISC-V ELF ─────────────────────────────────────
    PtxTranspiler tx;
    fs::path elf_path = fs::temp_directory_path() / "bench_vector_add.elf";
    ASSERT_TRUE(tx.compileToFile(kVectorAddPtx, elf_path.string()))
        << "PTX transpilation failed for vector_add";

    // ── 2. Problem size and launch config ──────────────────────────────────
    constexpr uint32_t N       = 256;
    constexpr uint32_t BLOCK_X = 32;
    constexpr uint32_t GRID_X  = N / BLOCK_X;   // 8 blocks

    // ── 3. Prepare host data ───────────────────────────────────────────────
    std::vector<int32_t> host_a(N), host_b(N), host_c(N, 0);
    for (uint32_t i = 0; i < N; ++i) {
        host_a[i] = static_cast<int32_t>(i * 2);
        host_b[i] = static_cast<int32_t>(i * 3 + 1);
    }

    // ── 4. Device buffers + H2D ────────────────────────────────────────────
    uint64_t a_ptr = 0, b_ptr = 0, c_ptr = 0;
    ASSERT_TRUE(gpgpuMalloc(a_ptr, N * sizeof(int32_t)));
    ASSERT_TRUE(gpgpuMalloc(b_ptr, N * sizeof(int32_t)));
    ASSERT_TRUE(gpgpuMalloc(c_ptr, N * sizeof(int32_t)));

    ASSERT_TRUE(gpgpuMemcpyH2D(a_ptr, host_a.data(), N * sizeof(int32_t)));
    ASSERT_TRUE(gpgpuMemcpyH2D(b_ptr, host_b.data(), N * sizeof(int32_t)));
    ASSERT_TRUE(gpgpuMemcpyH2D(c_ptr, host_c.data(), N * sizeof(int32_t)));

    // ── 5. Configure launch ────────────────────────────────────────────────
    KernelLaunchArgs launch;
    launch.kernel_name   = "vector_add";
    launch.entry_symbol  = "vector_add";
    launch.grid_x  = GRID_X;  launch.grid_y  = 1;  launch.grid_z  = 1;
    launch.block_x = BLOCK_X; launch.block_y = 1;  launch.block_z = 1;
    launch.args    = { a_ptr, b_ptr, c_ptr, static_cast<uint64_t>(N) };
    ASSERT_TRUE(configureLaunch(launch));

    // ── 6. Run via KernelBridge ────────────────────────────────────────────
    KernelBridge::Config cfg;
    cfg.num_compute_units = 8;
    cfg.threads_per_warp  = 1;    // scalar path: one independent CU per thread
    cfg.max_warps_per_cu  = 1;
    cfg.max_sim_cycles    = 500000;
    cfg.print_stats       = false;

    KernelBridge bridge(cfg);
    ASSERT_TRUE(bridge.runOnHardware(
        "vector_add", elf_path.string(),
        launch.args, { a_ptr, b_ptr, c_ptr }));

    // ── 7. D2H + verify ───────────────────────────────────────────────────
    ASSERT_TRUE(gpgpuMemcpyD2H(host_c.data(), c_ptr, N * sizeof(int32_t)));

    uint32_t errors = 0;
    for (uint32_t i = 0; i < N; ++i) {
        int32_t expected = host_a[i] + host_b[i];
        if (host_c[i] != expected) {
            ++errors;
            if (errors <= 5)
                ADD_FAILURE() << "c[" << i << "] = " << host_c[i]
                              << ", expected " << expected;
        }
    }
    EXPECT_EQ(errors, 0u) << errors << " wrong elements out of " << N;

    // ── 8. Metrics report ─────────────────────────────────────────────────
    printMetrics("vector_add (PTX→RV32I)", bridge, N, false);

    EXPECT_EQ(bridge.lastGridX(),  GRID_X);
    EXPECT_EQ(bridge.lastBlockX(), BLOCK_X);
    EXPECT_GT(bridge.lastTotalCycles(), 0u);
    EXPECT_GT(bridge.lastTotalInstructions(), 0u);

    gpgpuFree(a_ptr); gpgpuFree(b_ptr); gpgpuFree(c_ptr);
    fs::remove(elf_path);
}

// ── Benchmark 2: SAXPY (FP, per-thread, PTX) ──────────────────────────────────
// Exercises: Phase 5d (PTX transpiler) + Phase 5e (RV32F: fmv.w.x, flw, fsw, fmadd.s)
// + Phase 5f (THREAD_CTX: %tid.x / %ctaid.x / %ntid.x via gp)

TEST(PtxBenchmark, SaxpyPerThreadFP) {
    if (!clangAvailable())
        GTEST_SKIP() << "clang riscv32 target not available";

    // ── 1. Transpile ───────────────────────────────────────────────────────
    PtxTranspiler tx;
    fs::path elf_path = fs::temp_directory_path() / "bench_saxpy.elf";
    ASSERT_TRUE(tx.compileToFile(kSaxpyPtx, elf_path.string()))
        << "PTX transpilation failed for saxpy";

    // ── 2. Problem size ────────────────────────────────────────────────────
    constexpr uint32_t N       = 256;
    constexpr uint32_t BLOCK_X = 32;
    constexpr uint32_t GRID_X  = N / BLOCK_X;   // 8 blocks

    constexpr float ALPHA = 2.5f;

    // ── 3. Host data ───────────────────────────────────────────────────────
    std::vector<float> host_x(N), host_y(N), host_z(N, 0.0f);
    for (uint32_t i = 0; i < N; ++i) {
        host_x[i] = static_cast<float>(i);
        host_y[i] = 1.0f;
    }

    // ── 4. Device buffers + H2D ────────────────────────────────────────────
    uint64_t x_ptr = 0, y_ptr = 0, z_ptr = 0;
    ASSERT_TRUE(gpgpuMalloc(x_ptr, N * sizeof(float)));
    ASSERT_TRUE(gpgpuMalloc(y_ptr, N * sizeof(float)));
    ASSERT_TRUE(gpgpuMalloc(z_ptr, N * sizeof(float)));

    ASSERT_TRUE(gpgpuMemcpyH2D(x_ptr, host_x.data(), N * sizeof(float)));
    ASSERT_TRUE(gpgpuMemcpyH2D(y_ptr, host_y.data(), N * sizeof(float)));
    ASSERT_TRUE(gpgpuMemcpyH2D(z_ptr, host_z.data(), N * sizeof(float)));

    // Pass alpha as its IEEE-754 bit pattern in an integer arg slot
    uint32_t alpha_bits; std::memcpy(&alpha_bits, &ALPHA, 4);

    // ── 5. Configure launch ────────────────────────────────────────────────
    KernelLaunchArgs launch;
    launch.kernel_name   = "saxpy";
    launch.entry_symbol  = "saxpy";
    launch.grid_x  = GRID_X;  launch.grid_y  = 1;  launch.grid_z  = 1;
    launch.block_x = BLOCK_X; launch.block_y = 1;  launch.block_z = 1;
    // saxpy args: a (float bits), x_ptr, y_ptr, z_ptr, n
    launch.args = {
        static_cast<uint64_t>(alpha_bits),
        x_ptr, y_ptr, z_ptr,
        static_cast<uint64_t>(N)
    };
    ASSERT_TRUE(configureLaunch(launch));

    // ── 6. Run ─────────────────────────────────────────────────────────────
    KernelBridge::Config cfg;
    cfg.num_compute_units = 8;
    cfg.threads_per_warp  = 1;
    cfg.max_warps_per_cu  = 1;
    cfg.max_sim_cycles    = 500000;
    cfg.print_stats       = false;

    KernelBridge bridge(cfg);
    ASSERT_TRUE(bridge.runOnHardware(
        "saxpy", elf_path.string(),
        launch.args, { x_ptr, y_ptr, z_ptr }));

    // ── 7. D2H + verify ───────────────────────────────────────────────────
    ASSERT_TRUE(gpgpuMemcpyD2H(host_z.data(), z_ptr, N * sizeof(float)));

    uint32_t errors = 0;
    for (uint32_t i = 0; i < N; ++i) {
        float expected = ALPHA * host_x[i] + host_y[i];  // 2.5*i + 1.0
        if (std::fabs(host_z[i] - expected) > 1e-4f) {
            ++errors;
            if (errors <= 5)
                ADD_FAILURE() << "z[" << i << "] = " << host_z[i]
                              << ", expected " << expected
                              << " (diff=" << std::fabs(host_z[i]-expected) << ")";
        }
    }
    EXPECT_EQ(errors, 0u) << errors << " wrong elements out of " << N;

    // ── 8. Metrics report ─────────────────────────────────────────────────
    printMetrics("saxpy (PTX→RV32F fmadd.s)", bridge, N, true);

    EXPECT_EQ(bridge.lastGridX(),  GRID_X);
    EXPECT_EQ(bridge.lastBlockX(), BLOCK_X);
    EXPECT_GT(bridge.lastTotalCycles(), 0u);

    gpgpuFree(x_ptr); gpgpuFree(y_ptr); gpgpuFree(z_ptr);
    fs::remove(elf_path);
}

// ── Benchmark 3: PTX transpiler assembly coverage ─────────────────────────────
// Verifies the emitter produces correct assembly landmarks for both kernels
// without invoking clang (fast, no external dependency).

TEST(PtxBenchmark, TranspilerAssemblyVectorAdd) {
    PtxTranspiler tx;
    std::string kernel_name;
    std::string asm_text = tx.toAssembly(kVectorAddPtx, kernel_name);

    EXPECT_EQ(kernel_name, "vector_add");
    EXPECT_FALSE(asm_text.empty());

    // Must have kernel label
    EXPECT_NE(asm_text.find("vector_add:"), std::string::npos)    << asm_text;
    // Must load tid.x from THREAD_CTX via gp
    EXPECT_NE(asm_text.find("0(gp)"), std::string::npos)          << asm_text;
    // Must have bounds check (setp.ge → sltu+xori)
    EXPECT_NE(asm_text.find("sltu"), std::string::npos)           << asm_text;
    EXPECT_NE(asm_text.find("xori"), std::string::npos)           << asm_text;
    // Must have conditional branch
    EXPECT_NE(asm_text.find("bnez"), std::string::npos)           << asm_text;
    // Must have byte-offset shift (mul by 4 → slli 2)
    EXPECT_NE(asm_text.find("slli"), std::string::npos)           << asm_text;
    // Must have global load and store
    EXPECT_NE(asm_text.find("lw"), std::string::npos)             << asm_text;
    EXPECT_NE(asm_text.find("sw"), std::string::npos)             << asm_text;
    EXPECT_NE(asm_text.find("ret"), std::string::npos)            << asm_text;
}

TEST(PtxBenchmark, TranspilerAssemblySaxpy) {
    PtxTranspiler tx;
    std::string kernel_name;
    std::string asm_text = tx.toAssembly(kSaxpyPtx, kernel_name);

    EXPECT_EQ(kernel_name, "saxpy");
    EXPECT_FALSE(asm_text.empty());

    EXPECT_NE(asm_text.find("saxpy:"), std::string::npos)         << asm_text;
    // Float param → fmv.w.x (Phase 5d: ld.param.f32 handling)
    EXPECT_NE(asm_text.find("fmv.w.x"), std::string::npos)        << asm_text;
    // tid.x from THREAD_CTX
    EXPECT_NE(asm_text.find("0(gp)"), std::string::npos)          << asm_text;
    // FP loads and stores (Phase 5d: ld.global.f32 → flw, st.global.f32 → fsw)
    EXPECT_NE(asm_text.find("flw"), std::string::npos)             << asm_text;
    EXPECT_NE(asm_text.find("fsw"), std::string::npos)             << asm_text;
    // Fused multiply-add (Phase 5e: fma.rn.f32 → fmadd.s)
    EXPECT_NE(asm_text.find("fmadd.s"), std::string::npos)         << asm_text;
    EXPECT_NE(asm_text.find("ret"), std::string::npos)             << asm_text;
}
