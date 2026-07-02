// test_systemc_integration.cpp
//
// End-to-end integration test: software stack → KernelBridge → SystemC simulation.
//
// Flow:
//   1. Compile a simple RISC-V kernel (inline C source).
//   2. Pack it as a kernel bundle (kernel_loader).
//   3. Allocate device buffers (driver/host_api).
//   4. H2D copy of input data.
//   5. Run on SystemC via KernelBridge.
//   6. D2H copy of results.
//   7. Verify correctness.
//   8. Check performance metrics.
//
// The kernel is compiled with -march=rv32im (no C extension) so the
// RV32I decoder handles all instructions without RVC expansion.
//

#include <gtest/gtest.h>
#include <filesystem>
#include <fstream>
#include <cstring>
#include <array>

#include "../../../driver/src/loader.h"
#include "../../../software/kernel_loader/kernel_loader.h"
#include "../../../software/host_api/host_api.h"

// Hardware simulation
#include "../integration/kernel_bridge.h"
#include "../integration/elf_loader.h"
#include "../memory/memory_hierarchy.h"
#include "../compute_unit/compute_unit.h"

using namespace riscv_gpgpu;
namespace fs = std::filesystem;

// ─── Helper: compile C source to RISC-V ELF (RV32IM, no C extension) ─────────

static bool compileKernel(const std::string& src, const std::string& elf) {
    std::string obj = elf + ".o";
    std::string compile = "clang -target riscv32-unknown-elf"
                          " -march=rv32im -mabi=ilp32"
                          " -O1 -fno-exceptions -fomit-frame-pointer"
                          " -x c -c " + src + " -o " + obj + " 2>&1";
    int r = std::system(compile.c_str());
    if (r != 0) return false;

    std::string link = "clang -target riscv32-unknown-elf"
                       " -march=rv32im -mabi=ilp32"
                       " -fuse-ld=lld -nostdlib -Wl,--entry,0"
                       " -o " + elf + " " + obj + " 2>&1";
    r = std::system(link.c_str());
    return (r == 0) && fs::exists(elf);
}

static bool compileCudaStyleKernel(const std::string& src, const std::string& elf) {
    std::string obj = elf + ".o";
    std::string compile = "clang -target riscv32-unknown-elf"
                          " -march=rv32im -mabi=ilp32"
                          " -O0 -fno-exceptions -fomit-frame-pointer"
                          " -x c -c " + src + " -o " + obj + " 2>&1";
    int r = std::system(compile.c_str());
    if (r != 0) return false;

    std::string link = "clang -target riscv32-unknown-elf"
                       " -march=rv32im -mabi=ilp32"
                       " -fuse-ld=lld -nostdlib -Wl,--entry,0"
                       " -o " + elf + " " + obj + " 2>&1";
    r = std::system(link.c_str());
    return (r == 0) && fs::exists(elf);
}

static bool compileAssemblyKernel(const std::string& src, const std::string& elf) {
    std::string obj = elf + ".o";
    std::string compile = "clang -target riscv32-unknown-elf"
                          " -march=rv32im -mabi=ilp32"
                          " -x assembler -c " + src + " -o " + obj + " 2>&1";
    int r = std::system(compile.c_str());
    if (r != 0) return false;

    std::string link = "clang -target riscv32-unknown-elf"
                       " -march=rv32im -mabi=ilp32"
                       " -fuse-ld=lld -nostdlib -Wl,--entry,0"
                       " -o " + elf + " " + obj + " 2>&1";
    r = std::system(link.c_str());
    return (r == 0) && fs::exists(elf);
}

// ─── Test 1: ELF loader can read and store binary ─────────────────────────────

TEST(SystemCIntegration, ElfLoaderBasic) {
    fs::path src = fs::temp_directory_path() / "test_elf_basic.c";
    fs::path elf = fs::temp_directory_path() / "test_elf_basic.riscv.elf";

    { std::ofstream f(src); ASSERT_TRUE(f.is_open());
      f << "int add(int a, int b) { return a + b; }\n"; }

    ASSERT_TRUE(compileKernel(src.string(), elf.string()))
        << "Kernel compilation failed";

    MemoryHierarchy::Config cfg;
    cfg.shared_mem_size = 4096; cfg.global_mem_size = 0;
    cfg.cache_line_size = 64; cfg.l1_cache_size = 16384; cfg.l2_cache_size = 262144;
    MemoryHierarchy mem("test_mem", cfg);

    ElfLoader loader;
    EXPECT_TRUE(loader.load(elf.string(), mem));
    // Note: e_entry is 0 when compiled with -Wl,--entry,0 (our bare-metal default).
    // Verify the text segment loaded and the symbol table is populated instead.
    EXPECT_NE(loader.getTextBase(), 0xFFFFFFFFu);

    SymbolEntry sym;
    EXPECT_TRUE(loader.findSymbol("add", sym));
    EXPECT_NE(sym.address, 0u);

    fs::remove(src); fs::remove(elf);
    fs::remove(elf.string() + ".o");
}

// ─── Test 2: ComputeUnit executes simple arithmetic ───────────────────────────

TEST(SystemCIntegration, ComputeUnitAddsRegisters) {
    fs::path src = fs::temp_directory_path() / "test_add_kernel.c";
    fs::path elf = fs::temp_directory_path() / "test_add_kernel.riscv.elf";

    // int compute(int a, int b) { return a + b; }
    { std::ofstream f(src); ASSERT_TRUE(f.is_open());
      f << "int compute(int a, int b) { return a + b; }\n"; }

    ASSERT_TRUE(compileKernel(src.string(), elf.string()))
        << "Kernel compilation failed";

    MemoryHierarchy::Config mem_cfg;
    mem_cfg.shared_mem_size = 4096; mem_cfg.global_mem_size = 0;
    mem_cfg.cache_line_size = 64; mem_cfg.l1_cache_size = 16384;
    mem_cfg.l2_cache_size = 262144;
    MemoryHierarchy mem("cu_test_mem", mem_cfg);

    ElfLoader loader;
    ASSERT_TRUE(loader.load(elf.string(), mem));

    SymbolEntry sym;
    ASSERT_TRUE(loader.findSymbol("compute", sym)) << "Symbol 'compute' not found";

    // Set up stack area
    const uint32_t STACK_TOP      = 0x20000000u;
    const uint32_t RETURN_SENTINEL = 0x00000001u;
    std::array<uint32_t, 32> regs{};
    regs[2]  = STACK_TOP;      // sp
    regs[1]  = RETURN_SENTINEL; // ra
    regs[10] = 7;   // a0 = 7
    regs[11] = 11;  // a1 = 11

    ComputeUnit::Config cu_cfg;
    cu_cfg.unit_id = 0; cu_cfg.num_threads = 1;
    cu_cfg.threads_per_warp = 1; cu_cfg.max_warps = 1;
    cu_cfg.shared_mem_size = 4096; cu_cfg.max_cycles = 100000;

    ComputeUnit cu("test_cu", cu_cfg);
    cu.setMemoryHierarchy(&mem);
    cu.setEntryPoint(sym.address);
    cu.setInitialRegisters(regs);
    cu.setReturnSentinel(RETURN_SENTINEL);
    cu.launchKernel(0, 1, 1);

    while (!cu.isComplete()) cu.step();

    // Result should be in a0 (x10) = 7 + 11 = 18
    uint32_t result = cu.getRegister(0, 10);
    EXPECT_EQ(result, 18u) << "Expected 7+11=18 in a0, got " << result;
    EXPECT_GT(cu.getTotalInstructions(), 0u);

    fs::remove(src); fs::remove(elf);
    fs::remove(elf.string() + ".o");
}

// ─── Test 3: KernelBridge — vector_add end-to-end ─────────────────────────────

TEST(SystemCIntegration, VectorAddEndToEnd) {
    const int N = 8;

    fs::path src = fs::temp_directory_path() / "test_vec_add.c";
    fs::path elf = fs::temp_directory_path() / "test_vec_add.riscv.elf";

    // Simple vector_add kernel with explicit loop
    { std::ofstream f(src); ASSERT_TRUE(f.is_open());
      f << "void vector_add(const int* a, const int* b, int* c, int n) {\n"
        << "    for (int i = 0; i < n; ++i) c[i] = a[i] + b[i];\n"
        << "}\n"; }

    ASSERT_TRUE(compileKernel(src.string(), elf.string()))
        << "Kernel compilation failed";

    // ── Software stack: alloc + H2D ────────────────────────────────────────
    uint64_t a_ptr = 0, b_ptr = 0, c_ptr = 0;
    ASSERT_TRUE(gpgpuMalloc(a_ptr, N * sizeof(int)));
    ASSERT_TRUE(gpgpuMalloc(b_ptr, N * sizeof(int)));
    ASSERT_TRUE(gpgpuMalloc(c_ptr, N * sizeof(int)));

    int host_a[N], host_b[N], host_c[N];
    for (int i = 0; i < N; ++i) {
        host_a[i] = i * 2;
        host_b[i] = i * 3 + 1;
        host_c[i] = 0;
    }

    ASSERT_TRUE(gpgpuMemcpyH2D(a_ptr, host_a, sizeof(host_a)));
    ASSERT_TRUE(gpgpuMemcpyH2D(b_ptr, host_b, sizeof(host_b)));
    ASSERT_TRUE(gpgpuMemcpyH2D(c_ptr, host_c, sizeof(host_c)));

    // ── Run on SystemC via KernelBridge ────────────────────────────────────
    KernelBridge::Config bridge_cfg;
    bridge_cfg.num_compute_units = 1;
    bridge_cfg.threads_per_warp  = 1;
    bridge_cfg.max_warps_per_cu  = 1;
    bridge_cfg.max_sim_cycles    = 500000;
    bridge_cfg.print_stats       = true;

    KernelBridge bridge(bridge_cfg);

    // kernel_args = {a_ptr, b_ptr, c_ptr, N}  (RISC-V calling convention: a0..a3)
    std::vector<uint64_t> k_args = {a_ptr, b_ptr, c_ptr, (uint64_t)N};
    std::vector<uint64_t> d_ptrs = {a_ptr, b_ptr, c_ptr};

    bool ok = bridge.runOnHardware("vector_add", elf.string(), k_args, d_ptrs);
    EXPECT_TRUE(ok) << "KernelBridge::runOnHardware failed";

    // ── D2H + verify ──────────────────────────────────────────────────────
    ASSERT_TRUE(gpgpuMemcpyD2H(host_c, c_ptr, sizeof(host_c)));

    for (int i = 0; i < N; ++i) {
        int expected = host_a[i] + host_b[i];
        EXPECT_EQ(host_c[i], expected)
            << "c[" << i << "] = " << host_c[i]
            << ", expected " << expected;
    }

    // Performance sanity checks
    EXPECT_GT(bridge.lastTotalCycles(),       0u);
    EXPECT_GT(bridge.lastTotalInstructions(), 0u);

    // ── Cleanup ───────────────────────────────────────────────────────────
    EXPECT_TRUE(gpgpuFree(a_ptr));
    EXPECT_TRUE(gpgpuFree(b_ptr));
    EXPECT_TRUE(gpgpuFree(c_ptr));

    fs::remove(src); fs::remove(elf);
    fs::remove(elf.string() + ".o");
}

// ─── Test 4: KernelBridge — scalar multiply ───────────────────────────────────

TEST(SystemCIntegration, ScalarMultiply) {
    fs::path src = fs::temp_directory_path() / "test_scalar_mul.c";
    fs::path elf = fs::temp_directory_path() / "test_scalar_mul.riscv.elf";

    { std::ofstream f(src); ASSERT_TRUE(f.is_open());
      f << "void scalar_mul(int* arr, int scalar, int n) {\n"
        << "    for (int i = 0; i < n; ++i) arr[i] *= scalar;\n"
        << "}\n"; }

    ASSERT_TRUE(compileKernel(src.string(), elf.string()))
        << "Kernel compilation failed";

    const int N = 4;
    uint64_t arr_ptr = 0;
    ASSERT_TRUE(gpgpuMalloc(arr_ptr, N * sizeof(int)));

    int host_arr[N] = {1, 2, 3, 4};
    ASSERT_TRUE(gpgpuMemcpyH2D(arr_ptr, host_arr, sizeof(host_arr)));

    KernelBridge bridge;
    std::vector<uint64_t> k_args = {arr_ptr, 5u, (uint64_t)N};
    EXPECT_TRUE(bridge.runOnHardware("scalar_mul", elf.string(), k_args, {arr_ptr}));

    ASSERT_TRUE(gpgpuMemcpyD2H(host_arr, arr_ptr, sizeof(host_arr)));
    int expected[] = {5, 10, 15, 20};
    for (int i = 0; i < N; ++i)
        EXPECT_EQ(host_arr[i], expected[i]) << "arr[" << i << "]";

    EXPECT_TRUE(gpgpuFree(arr_ptr));
    fs::remove(src); fs::remove(elf);
    fs::remove(elf.string() + ".o");
}

// ─── Test 5: CUDA-style multi-unit SIMT end-to-end ───────────────────────────

TEST(SystemCIntegration, CudaMultiUnitSimtEndToEnd) {
    const int N = 16;

    fs::path src = fs::temp_directory_path() / "test_cuda_multi_unit_simt.c";
    fs::path elf = fs::temp_directory_path() / "test_cuda_multi_unit_simt.riscv.elf";
    fs::path manifest = fs::temp_directory_path() / "test_cuda_multi_unit_simt.json";

    {
        std::ofstream f(src);
        ASSERT_TRUE(f.is_open());
                f << ".text\n"
                    << ".globl cuda_multi_unit_simt\n"
                    << "cuda_multi_unit_simt:\n"
                    << "    slli t0, a4, 3\n"
                    << "    add t0, t0, a5\n"
                    << "    andi t1, a5, 1\n"
                    << "    slli t0, t0, 2\n"
                    << "    add t2, a2, t0\n"
                    << "    add t3, a0, t0\n"
                    << "    add t4, a1, t0\n"
                    << "    bne t1, zero, 1f\n"
                    << "0:\n"
                    << "    lw t5, 0(t3)\n"
                    << "    lw t6, 0(t4)\n"
                    << "    add t5, t5, t6\n"
                    << "    sw t5, 0(t2)\n"
                    << "    ret\n"
                    << "1:\n"
                    << "    lw t5, 0(t3)\n"
                    << "    lw t6, 0(t4)\n"
                    << "    sub t5, t5, t6\n"
                    << "    sw t5, 0(t2)\n"
                    << "    ret\n";
    }

        ASSERT_TRUE(compileAssemblyKernel(src.string(), elf.string()))
        << "Kernel compilation failed";

    ASSERT_TRUE(packKernelBundle("cuda_multi_unit_simt", elf.string(), manifest.string(), 8, 1, 1, 0));
    KernelBundleInfo bundle_info;
    ASSERT_TRUE(inspectKernelBundleDetails(manifest.string(), bundle_info));

    uint64_t a_ptr = 0, b_ptr = 0, c_ptr = 0;
    ASSERT_TRUE(gpgpuMalloc(a_ptr, N * sizeof(int)));
    ASSERT_TRUE(gpgpuMalloc(b_ptr, N * sizeof(int)));
    ASSERT_TRUE(gpgpuMalloc(c_ptr, N * sizeof(int)));

    int host_a[N], host_b[N], host_c[N];
    for (int i = 0; i < N; ++i) {
        host_a[i] = i * 4 + 1;
        host_b[i] = i * 3 + 2;
        host_c[i] = 0;
    }

    ASSERT_TRUE(gpgpuMemcpyH2D(a_ptr, host_a, sizeof(host_a)));
    ASSERT_TRUE(gpgpuMemcpyH2D(b_ptr, host_b, sizeof(host_b)));
    ASSERT_TRUE(gpgpuMemcpyH2D(c_ptr, host_c, sizeof(host_c)));

    KernelLaunchArgs launch_args;
    launch_args.kernel_name = bundle_info.kernel_name;
    launch_args.entry_symbol = bundle_info.entry_symbol;
    launch_args.grid_x = 2;
    launch_args.grid_y = 1;
    launch_args.grid_z = 1;
    launch_args.block_x = bundle_info.workgroup_x;
    launch_args.block_y = bundle_info.workgroup_y;
    launch_args.block_z = bundle_info.workgroup_z;
    launch_args.shared_mem_bytes = bundle_info.shared_mem_bytes;
    launch_args.args = {a_ptr, b_ptr, c_ptr, static_cast<uint64_t>(N), 0, 0};

    ASSERT_TRUE(configureLaunch(launch_args));

    KernelBridge::Config bridge_cfg;
    bridge_cfg.num_compute_units = 2;
    bridge_cfg.threads_per_warp = 8;
    bridge_cfg.max_warps_per_cu = 1;
    bridge_cfg.max_sim_cycles = 500000;
    bridge_cfg.print_stats = true;

    KernelBridge bridge(bridge_cfg);
    ASSERT_TRUE(bridge.runOnHardware("cuda_multi_unit_simt", elf.string(), launch_args.args, {a_ptr, b_ptr, c_ptr}));

    ASSERT_TRUE(gpgpuMemcpyD2H(host_c, c_ptr, sizeof(host_c)));

    for (int i = 0; i < N; ++i) {
        int expected = (i & 1) ? (host_a[i] - host_b[i]) : (host_a[i] + host_b[i]);
        EXPECT_EQ(host_c[i], expected)
            << "c[" << i << "] = " << host_c[i]
            << ", expected " << expected;
    }

    EXPECT_GT(bridge.lastTotalCycles(), 0u);
    EXPECT_GT(bridge.lastTotalInstructions(), 0u);
    EXPECT_EQ(bridge.lastGridX(), 2u);
    EXPECT_EQ(bridge.lastBlockX(), 8u);
    EXPECT_GT(bridge.lastDivergenceEvents(), 0u);

    EXPECT_TRUE(gpgpuFree(a_ptr));
    EXPECT_TRUE(gpgpuFree(b_ptr));
    EXPECT_TRUE(gpgpuFree(c_ptr));

    fs::remove(src);
    fs::remove(elf);
    fs::remove(elf.string() + ".o");
    fs::remove(manifest);
}
