// rodinia_benchmark.cpp — Rodinia-style GPU benchmarks via PTX transpiler
//
// Implements simplified versions of Rodinia benchmark kernels that run on
// our RISC-V GPGPU simulator.  Full Rodinia kernels (with tiling, shared
// memory, and barriers) require Phase 5g.  These are the "naive" variants
// that exercise Phase 5d + 5e + 5f.
//
// Benchmarks included:
//   MatmulNaive   — N×N integer matrix multiply, one thread per output element.
//                   Exercises: inner loop, div/rem, mul, add, global ld/st.
//   Hotspot1D     — 1D thermal stencil (heat diffusion step).
//                   Exercises: FP fadd/fsub/fmadd, per-thread FP accumulation.
//
// Limitations (to be lifted in Phase 5g):
//   - No shared memory → no tiled / cache-friendly versions.
//   - No barriers     → no reduction or synchronisation patterns.
//   - No atomics      → no BFS / sparse-update patterns.

#include <gtest/gtest.h>
#include <cmath>
#include <cstring>
#include <filesystem>
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
                         uint32_t N, bool fp, double ref_gflops = 0.0) {
    uint64_t cyc = bridge.lastTotalCycles();
    uint64_t ins = bridge.lastTotalInstructions();
    double   ipc = (cyc > 0) ? static_cast<double>(ins) / cyc : 0.0;
    uint32_t h = bridge.lastL1Hits(), m = bridge.lastL1Misses();
    double   hr = (h+m > 0) ? 100.0*h/(h+m) : 0.0;

    std::cout << "\n"
              << "  ┌─ Rodinia: " << name << " (N=" << N << (fp ? ", FP" : ", INT") << ")\n"
              << "  │  Cycles:      " << std::setw(10) << cyc << "\n"
              << "  │  Instructions:" << std::setw(10) << ins << "\n"
              << "  │  IPC:         " << std::fixed << std::setprecision(3) << ipc << "\n"
              << "  │  L1 hit rate: " << std::setprecision(1) << hr << "%"
              << "  (" << h << "/" << (h+m) << ")\n"
              << "  │  Divergence:  " << bridge.lastDivergenceEvents() << " events\n"
              << "  │  Grid:  " << bridge.lastGridX() << "×" << bridge.lastGridY() << "\n"
              << "  │  Block: " << bridge.lastBlockX() << "×" << bridge.lastBlockY() << "\n"
              << "  └─ Entry: " << bridge.lastEntrySymbol() << "\n\n";
}

// ── PTX kernels (embedded) ─────────────────────────────────────────────────────

static const char* kMatmulNaivePtx = R"PTX(
.version 7.0
.target sm_20
.address_size 32

.visible .entry matmul_naive(
    .param .u32 mm_p0,
    .param .u32 mm_p1,
    .param .u32 mm_p2,
    .param .u32 mm_p3
)
{
    .reg .pred  %p<2>;
    .reg .u32   %r<17>;

    ld.param.u32    %r0, [mm_p0];
    ld.param.u32    %r1, [mm_p1];
    ld.param.u32    %r2, [mm_p2];
    ld.param.u32    %r3, [mm_p3];

    mov.u32         %r4, %tid.x;
    mov.u32         %r5, %ctaid.x;
    mov.u32         %r6, %ntid.x;
    mul.lo.u32      %r7, %r5, %r6;
    add.u32         %r7, %r7, %r4;

    mul.lo.u32      %r8, %r3, %r3;
    setp.ge.u32     %p0, %r7, %r8;
    @%p0 bra        $L__mm_end;

    div.u32         %r9,  %r7, %r3;
    rem.u32         %r10, %r7, %r3;

    mul.lo.u32      %r11, %r9,  %r3;
    mul.lo.u32      %r11, %r11, 4;
    add.u32         %r11, %r0,  %r11;

    mul.lo.u32      %r12, %r10, 4;

    mov.u32         %r13, 0;
    mov.u32         %r14, 0;

$L__mm_loop:
    mul.lo.u32      %r15, %r14, 4;
    add.u32         %r15, %r11, %r15;
    ld.global.u32   %r15, [%r15];

    mul.lo.u32      %r16, %r14, %r3;
    mul.lo.u32      %r16, %r16, 4;
    add.u32         %r16, %r1,  %r16;
    add.u32         %r16, %r16, %r12;
    ld.global.u32   %r16, [%r16];

    mul.lo.u32      %r15, %r15, %r16;
    add.u32         %r13, %r13, %r15;

    add.u32         %r14, %r14, 1;
    setp.lt.u32     %p1,  %r14, %r3;
    @%p1 bra        $L__mm_loop;

    mul.lo.u32      %r15, %r9,  %r3;
    add.u32         %r15, %r15, %r10;
    mul.lo.u32      %r15, %r15, 4;
    add.u32         %r15, %r2,  %r15;
    st.global.u32   [%r15], %r13;

$L__mm_end:
    ret;
}
)PTX";

static const char* kHotspot1dPtx = R"PTX(
.version 7.0
.target sm_20
.address_size 32

.visible .entry hotspot1d(
    .param .u32 hs_p0,
    .param .u32 hs_p1,
    .param .u32 hs_p2,
    .param .f32 hs_p3
)
{
    .reg .pred  %p<2>;
    .reg .u32   %r<10>;
    .reg .f32   %f<5>;

    ld.param.u32    %r0, [hs_p0];
    ld.param.u32    %r1, [hs_p1];
    ld.param.u32    %r2, [hs_p2];
    ld.param.f32    %f0, [hs_p3];

    mov.u32         %r3, %tid.x;
    mov.u32         %r4, %ctaid.x;
    mov.u32         %r5, %ntid.x;
    mul.lo.u32      %r6, %r4, %r5;
    add.u32         %r6, %r6, %r3;

    setp.ge.u32     %p0, %r6, %r2;
    @%p0 bra        $L__hs_end;

    mul.lo.u32      %r7, %r6, 4;
    add.u32         %r7, %r0, %r7;

    ld.global.f32   %f1, [%r7];

    add.u32         %r8, %r7, 4;
    ld.global.f32   %f2, [%r8];

    add.u32         %r9, %r7, 8;
    ld.global.f32   %f3, [%r9];

    add.f32         %f4, %f1, %f3;
    sub.f32         %f4, %f4, %f2;
    sub.f32         %f4, %f4, %f2;

    fma.rn.f32      %f4, %f0, %f4, %f2;

    mul.lo.u32      %r7, %r6, 4;
    add.u32         %r7, %r1, %r7;
    st.global.f32   [%r7], %f4;

$L__hs_end:
    ret;
}
)PTX";

// ── Rodinia Benchmark 1: Matmul Naive ─────────────────────────────────────────
//
// N×N integer matrix multiply (no tiling, no shared memory).
// Each thread computes one element C[row][col] = dot(A_row, B_col).
// N=16 → 256 threads (manageable in simulation).
//
// Reference: Rodinia Linear Algebra (LUD / SGEMM family)

TEST(RodiniaBenchmark, MatmulNaive) {
    if (!clangAvailable())
        GTEST_SKIP() << "clang riscv32 target not available";

    // ── 1. Transpile ─────────────────────────────────────────────────────
    PtxTranspiler tx;
    fs::path elf = fs::temp_directory_path() / "rodinia_matmul.elf";
    ASSERT_TRUE(tx.compileToFile(kMatmulNaivePtx, elf.string()))
        << "PTX compilation failed for matmul_naive";

    // ── 2. Problem setup ─────────────────────────────────────────────────
    constexpr uint32_t N       = 16;   // 16×16 matrix
    constexpr uint32_t NTOT    = N * N; // 256 output elements
    constexpr uint32_t BLOCK_X = 16;
    constexpr uint32_t GRID_X  = NTOT / BLOCK_X; // 16 blocks

    std::vector<int32_t> host_A(NTOT), host_B(NTOT), host_C(NTOT, 0);
    // A = identity-like (A[i][i] = i+1, rest 0) → C = B (A * B = B when A = I? No, not exactly)
    // Use simple known values: A[i][j] = i+1, B[i][j] = j+1
    for (uint32_t i = 0; i < N; ++i)
        for (uint32_t j = 0; j < N; ++j) {
            host_A[i*N + j] = static_cast<int32_t>(i + 1);
            host_B[i*N + j] = static_cast<int32_t>(j + 1);
        }

    // Reference: C[i][j] = sum_k A[i][k] * B[k][j] = (i+1) * sum_k(k+1)
    // sum_k(k+1) for k=0..N-1 = N*(N+1)/2 = 16*17/2 = 136
    // C[i][j] = (i+1) * 136  (independent of j since all B columns have same pattern)
    // Wait: B[k][j] = j+1 (col-dependent), but sum_k A[i][k]*B[k][j] = (i+1)*sum_k(j+1)
    //   = (i+1)*(j+1)*N
    // C[i][j] = (i+1)*(j+1)*N = (i+1)*(j+1)*16
    std::vector<int32_t> ref_C(NTOT);
    for (uint32_t i = 0; i < N; ++i)
        for (uint32_t j = 0; j < N; ++j)
            ref_C[i*N + j] = static_cast<int32_t>((i+1) * (j+1) * N);

    // ── 3. Device buffers ─────────────────────────────────────────────────
    uint64_t a_ptr = 0, b_ptr = 0, c_ptr = 0;
    ASSERT_TRUE(gpgpuMalloc(a_ptr, NTOT * sizeof(int32_t)));
    ASSERT_TRUE(gpgpuMalloc(b_ptr, NTOT * sizeof(int32_t)));
    ASSERT_TRUE(gpgpuMalloc(c_ptr, NTOT * sizeof(int32_t)));
    ASSERT_TRUE(gpgpuMemcpyH2D(a_ptr, host_A.data(), NTOT * sizeof(int32_t)));
    ASSERT_TRUE(gpgpuMemcpyH2D(b_ptr, host_B.data(), NTOT * sizeof(int32_t)));
    ASSERT_TRUE(gpgpuMemcpyH2D(c_ptr, host_C.data(), NTOT * sizeof(int32_t)));

    // ── 4. Configure and launch ───────────────────────────────────────────
    KernelLaunchArgs launch;
    launch.kernel_name   = "matmul_naive";
    launch.entry_symbol  = "matmul_naive";
    launch.grid_x  = GRID_X;  launch.grid_y  = 1;  launch.grid_z  = 1;
    launch.block_x = BLOCK_X; launch.block_y = 1;  launch.block_z = 1;
    launch.args    = { a_ptr, b_ptr, c_ptr, static_cast<uint64_t>(N) };
    ASSERT_TRUE(configureLaunch(launch));

    KernelBridge::Config cfg;
    cfg.num_compute_units = 8;
    cfg.threads_per_warp  = 1;
    cfg.max_warps_per_cu  = 1;
    cfg.max_sim_cycles    = 1000000;
    cfg.print_stats       = false;

    KernelBridge bridge(cfg);
    ASSERT_TRUE(bridge.runOnHardware(
        "matmul_naive", elf.string(),
        launch.args, { a_ptr, b_ptr, c_ptr }));

    // ── 5. Verify ─────────────────────────────────────────────────────────
    ASSERT_TRUE(gpgpuMemcpyD2H(host_C.data(), c_ptr, NTOT * sizeof(int32_t)));

    uint32_t errors = 0;
    for (uint32_t i = 0; i < NTOT; ++i) {
        if (host_C[i] != ref_C[i]) {
            ++errors;
            if (errors <= 4)
                ADD_FAILURE() << "C[" << i/N << "][" << i%N << "] = "
                              << host_C[i] << ", expected " << ref_C[i];
        }
    }
    EXPECT_EQ(errors, 0u) << errors << " wrong elements out of " << NTOT;

    printMetrics("matmul_naive (naive NxN = " + std::to_string(N) + ")", bridge, NTOT, false);

    EXPECT_EQ(bridge.lastGridX(),  GRID_X);
    EXPECT_GT(bridge.lastTotalCycles(), 0u);

    gpgpuFree(a_ptr); gpgpuFree(b_ptr); gpgpuFree(c_ptr);
    fs::remove(elf);
}

// ── Rodinia Benchmark 2: Hotspot 1D ───────────────────────────────────────────
//
// One time-step of 1D heat equation (Hotspot-inspired, no shared memory).
// T_new[i] = T[i] + alpha * (T[i-1] - 2*T[i] + T[i+1])
//
// The host passes T_padded[N+2] with ghost cells to avoid boundary logic.
// Each thread computes one output cell.
//
// Reference: Rodinia Hotspot (2D version uses shared memory for tiling).

TEST(RodiniaBenchmark, Hotspot1D) {
    if (!clangAvailable())
        GTEST_SKIP() << "clang riscv32 target not available";

    // ── 1. Transpile ─────────────────────────────────────────────────────
    PtxTranspiler tx;
    fs::path elf = fs::temp_directory_path() / "rodinia_hotspot1d.elf";
    ASSERT_TRUE(tx.compileToFile(kHotspot1dPtx, elf.string()))
        << "PTX compilation failed for hotspot1d";

    // ── 2. Problem setup ─────────────────────────────────────────────────
    constexpr uint32_t N       = 256;
    constexpr uint32_t BLOCK_X = 32;
    constexpr uint32_t GRID_X  = N / BLOCK_X; // 8 blocks
    constexpr float    ALPHA   = 0.1f;   // thermal diffusivity, < 0.5 for stability

    // Initial temperature: sine wave (easy to verify)
    // T[i] = sin(2*pi*i/N)
    std::vector<float> T_orig(N), T_result(N, 0.0f);
    for (uint32_t i = 0; i < N; ++i)
        T_orig[i] = std::sin(2.0f * 3.14159265f * static_cast<float>(i) / N);

    // Padded input (ghost cells at boundaries)
    std::vector<float> T_padded(N + 2);
    T_padded[0] = T_orig[0];        // left ghost = T[0]
    for (uint32_t i = 0; i < N; ++i)
        T_padded[i + 1] = T_orig[i];
    T_padded[N + 1] = T_orig[N - 1]; // right ghost = T[N-1]

    // Reference computation (CPU)
    std::vector<float> ref_out(N);
    for (uint32_t i = 0; i < N; ++i) {
        float left   = T_padded[i];
        float center = T_padded[i + 1];
        float right  = T_padded[i + 2];
        ref_out[i] = center + ALPHA * (left - 2.0f * center + right);
    }

    // ── 3. Device buffers ─────────────────────────────────────────────────
    uint64_t tin_ptr = 0, tout_ptr = 0;
    ASSERT_TRUE(gpgpuMalloc(tin_ptr,  (N + 2) * sizeof(float)));
    ASSERT_TRUE(gpgpuMalloc(tout_ptr, N       * sizeof(float)));
    ASSERT_TRUE(gpgpuMemcpyH2D(tin_ptr,  T_padded.data(), (N+2) * sizeof(float)));
    ASSERT_TRUE(gpgpuMemcpyH2D(tout_ptr, T_result.data(), N     * sizeof(float)));

    // ── 4. Configure and launch ───────────────────────────────────────────
    uint32_t alpha_bits; std::memcpy(&alpha_bits, &ALPHA, 4);

    KernelLaunchArgs launch;
    launch.kernel_name   = "hotspot1d";
    launch.entry_symbol  = "hotspot1d";
    launch.grid_x  = GRID_X;  launch.grid_y  = 1;  launch.grid_z  = 1;
    launch.block_x = BLOCK_X; launch.block_y = 1;  launch.block_z = 1;
    // hotspot1d args: T_padded, T_out, N, alpha (float bits)
    launch.args = {
        tin_ptr, tout_ptr,
        static_cast<uint64_t>(N),
        static_cast<uint64_t>(alpha_bits)
    };
    ASSERT_TRUE(configureLaunch(launch));

    KernelBridge::Config cfg;
    cfg.num_compute_units = 8;
    cfg.threads_per_warp  = 1;
    cfg.max_warps_per_cu  = 1;
    cfg.max_sim_cycles    = 500000;
    cfg.print_stats       = false;

    KernelBridge bridge(cfg);
    ASSERT_TRUE(bridge.runOnHardware(
        "hotspot1d", elf.string(),
        launch.args, { tin_ptr, tout_ptr }));

    // ── 5. Verify ─────────────────────────────────────────────────────────
    ASSERT_TRUE(gpgpuMemcpyD2H(T_result.data(), tout_ptr, N * sizeof(float)));

    uint32_t errors = 0;
    for (uint32_t i = 0; i < N; ++i) {
        if (std::fabs(T_result[i] - ref_out[i]) > 1e-5f) {
            ++errors;
            if (errors <= 4)
                ADD_FAILURE() << "T_out[" << i << "] = " << T_result[i]
                              << ", expected " << ref_out[i]
                              << " (diff=" << std::fabs(T_result[i]-ref_out[i]) << ")";
        }
    }
    EXPECT_EQ(errors, 0u) << errors << " wrong cells out of " << N;

    printMetrics("hotspot1d (FP stencil N=" + std::to_string(N) + ")", bridge, N, true);

    EXPECT_EQ(bridge.lastGridX(),  GRID_X);
    EXPECT_GT(bridge.lastTotalCycles(), 0u);

    gpgpuFree(tin_ptr); gpgpuFree(tout_ptr);
    fs::remove(elf);
}

// ── Assembly coverage: verify PTX transpilation of Rodinia kernels ─────────────

TEST(RodiniaBenchmark, MatmulNaiveAssembly) {
    PtxTranspiler tx;
    std::string name;
    std::string asm_text = tx.toAssembly(kMatmulNaivePtx, name);
    EXPECT_EQ(name, "matmul_naive");
    EXPECT_FALSE(asm_text.empty());
    // div/rem for row = global_tid / N, col = global_tid % N
    EXPECT_NE(asm_text.find("divu"), std::string::npos) << "Missing divu for row calc";
    EXPECT_NE(asm_text.find("remu"), std::string::npos) << "Missing remu for col calc";
    // inner loop
    EXPECT_NE(asm_text.find("L__mm_loop"), std::string::npos) << "Missing loop label";
    EXPECT_NE(asm_text.find("bnez"), std::string::npos) << "Missing loop branch";
    // multiply and accumulate
    EXPECT_NE(asm_text.find("mul "), std::string::npos) << "Missing mul";
    EXPECT_NE(asm_text.find("add "), std::string::npos) << "Missing add";
    // load and store
    EXPECT_NE(asm_text.find("lw"), std::string::npos) << "Missing lw";
    EXPECT_NE(asm_text.find("sw"), std::string::npos) << "Missing sw";
}

TEST(RodiniaBenchmark, Hotspot1DAssembly) {
    PtxTranspiler tx;
    std::string name;
    std::string asm_text = tx.toAssembly(kHotspot1dPtx, name);
    EXPECT_EQ(name, "hotspot1d");
    EXPECT_FALSE(asm_text.empty());
    // FP param (alpha) → fmv.w.x
    EXPECT_NE(asm_text.find("fmv.w.x"), std::string::npos) << "Missing fmv.w.x for alpha";
    // FP loads
    EXPECT_NE(asm_text.find("flw"), std::string::npos) << "Missing flw";
    // FP arithmetic: add, sub, fmadd
    EXPECT_NE(asm_text.find("fadd.s"), std::string::npos) << "Missing fadd.s";
    EXPECT_NE(asm_text.find("fsub.s"), std::string::npos) << "Missing fsub.s";
    EXPECT_NE(asm_text.find("fmadd.s"), std::string::npos) << "Missing fmadd.s";
    // FP store
    EXPECT_NE(asm_text.find("fsw"), std::string::npos) << "Missing fsw";
    // THREAD_CTX reads
    EXPECT_NE(asm_text.find("0(gp)"), std::string::npos) << "Missing tid.x load";
}
