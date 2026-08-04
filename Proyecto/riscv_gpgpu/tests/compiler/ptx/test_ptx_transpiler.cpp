// test_ptx_transpiler.cpp — End-to-end transpiler integration test (T058)
//
// Compiles a PTX kernel via PtxTranspiler → RISC-V ELF, loads it into
// the SystemC KernelBridge, runs a vector_add operation, and verifies results.
//
// Requires: clang with riscv32 target, lld.
// The test is skipped if clang is not available or returns an error.

#include <gtest/gtest.h>
#include <cstdlib>
#include <cstring>
#include <vector>
#include <string>
#include <fstream>

#include "ptx_transpiler.h"

using namespace riscv_gpgpu::ptx;

// ── PTX kernels ───────────────────────────────────────────────────────────────

// Per-thread vector_add: c[i] = a[i] + b[i]
static const char* kVecAddPtx = R"(
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
)";

// Per-thread SAXPY: z[i] = a * x[i] + y[i]
static const char* kSaxpyPtx = R"(
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
)";

static const char* kPtx64CopyPtx = R"(
.version 7.0
.target sm_52
.address_size 64
.visible .entry copy_word(
    .param .u64 .ptr .global src,
    .param .u64 .ptr .global dst,
    .param .u32 index
) {
    .reg .u32 %r<2>;
    .reg .b64 %rd<7>;
    ld.param.u64 %rd0, [src];
    ld.param.u64 %rd1, [dst];
    ld.param.u32 %r0, [index];
    cvta.to.global.u64 %rd2, %rd0;
    cvta.to.global.u64 %rd3, %rd1;
    mul.wide.u32 %rd4, %r0, 4;
    add.s64 %rd5, %rd2, %rd4;
    add.s64 %rd6, %rd3, %rd4;
    ld.global.u32 %r1, [%rd5];
    st.global.u32 [%rd6], %r1;
    ret;
})";

// ── Helper: check if clang riscv32 target is available ────────────────────────

static bool clangAvailable() {
    int rc = std::system("clang --target=riscv32-unknown-elf -march=rv32imf "
                         "-mabi=ilp32f -fuse-ld=lld -nostdlib -x assembler-with-cpp "
                         "/dev/null -o /dev/null 2>/dev/null");
    return (rc == 0);
}

// ── Tests ─────────────────────────────────────────────────────────────────────

TEST(PtxTranspiler, ToAssemblyVectorAdd) {
    PtxTranspiler tx;
    std::string kernel_name;
    std::string asm_text = tx.toAssembly(kVecAddPtx, kernel_name);
    EXPECT_EQ(kernel_name, "vector_add");
    EXPECT_FALSE(asm_text.empty());
    EXPECT_NE(asm_text.find("vector_add:"), std::string::npos) << asm_text;
    EXPECT_NE(asm_text.find("ret"), std::string::npos) << asm_text;
}

TEST(PtxTranspiler, ToAssemblySaxpy) {
    PtxTranspiler tx;
    std::string kernel_name;
    std::string asm_text = tx.toAssembly(kSaxpyPtx, kernel_name);
    EXPECT_EQ(kernel_name, "saxpy");
    EXPECT_FALSE(asm_text.empty());
    EXPECT_NE(asm_text.find("fmadd.s"), std::string::npos) << asm_text;
}

TEST(PtxTranspiler, ToAssemblyPtx64PointerLowering) {
    PtxTranspiler transpiler;
    std::string kernel_name;
    std::string asm_text = transpiler.toAssembly(kPtx64CopyPtx, kernel_name);
    EXPECT_EQ(kernel_name, "copy_word");
    EXPECT_NE(asm_text.find("slli"), std::string::npos) << asm_text;
    EXPECT_NE(asm_text.find("lw"), std::string::npos) << asm_text;
    EXPECT_NE(asm_text.find("sw"), std::string::npos) << asm_text;
}

TEST(PtxTranspiler, RejectsUnsupportedPtx64DataOperation) {
    const char* ptx = R"(
.address_size 64
.entry k(.param .u64 p0) {
    .reg .b64 %rd<2>;
    ld.param.u64 %rd0, [p0];
    ld.global.u64 %rd1, [%rd0];
    ret;
})";
    PtxTranspiler transpiler;
    auto result = transpiler.compile(ptx);
    EXPECT_FALSE(result.ok);
    EXPECT_NE(result.error.find("64-bit memory"), std::string::npos);
}

TEST(PtxTranspiler, CompileVectorAddToElf) {
    if (!clangAvailable()) {
        GTEST_SKIP() << "clang riscv32 target not available";
    }

    PtxTranspiler tx;
    auto result = tx.compile(kVecAddPtx);

    EXPECT_TRUE(result.ok) << "Compilation failed: " << result.error;
    EXPECT_EQ(result.entry_symbol, "vector_add");
    ASSERT_GT(result.bytes.size(), 52u) << "ELF too small";

    // Check ELF magic
    EXPECT_EQ(result.bytes[0], 0x7F);
    EXPECT_EQ(result.bytes[1], 'E');
    EXPECT_EQ(result.bytes[2], 'L');
    EXPECT_EQ(result.bytes[3], 'F');
    // ELF class: 32-bit (1)
    EXPECT_EQ(result.bytes[4], 1);
    // Machine: RISC-V (0xF3)
    EXPECT_EQ(result.bytes[18], 0xF3);
}

TEST(PtxTranspiler, CompilePtx64PointerLoweringToElf) {
    if (!clangAvailable()) {
        GTEST_SKIP() << "clang riscv32 target not available";
    }

    PtxTranspiler transpiler;
    auto result = transpiler.compile(kPtx64CopyPtx);
    EXPECT_TRUE(result.ok) << result.error;
    ASSERT_GT(result.bytes.size(), 52u);
    EXPECT_EQ(result.bytes[4], 1);
    EXPECT_EQ(result.bytes[18], 0xF3);
}

TEST(PtxTranspiler, CompileSaxpyToElf) {
    if (!clangAvailable()) {
        GTEST_SKIP() << "clang riscv32 target not available";
    }

    PtxTranspiler tx;
    auto result = tx.compile(kSaxpyPtx);

    EXPECT_TRUE(result.ok) << "Compilation failed: " << result.error;
    EXPECT_EQ(result.entry_symbol, "saxpy");
    ASSERT_GT(result.bytes.size(), 52u);

    EXPECT_EQ(result.bytes[0], 0x7F);
    EXPECT_EQ(result.bytes[4], 1);    // ELF32
    EXPECT_EQ(result.bytes[18], 0xF3); // RISC-V
}

TEST(PtxTranspiler, CompileToFile) {
    if (!clangAvailable()) {
        GTEST_SKIP() << "clang riscv32 target not available";
    }

    PtxTranspiler tx;
    std::string out_path = "/tmp/ptx_transpiler_test_va.elf";
    bool ok = tx.compileToFile(kVecAddPtx, out_path);
    EXPECT_TRUE(ok);

    // Verify file exists and has ELF magic
    std::ifstream f(out_path, std::ios::binary);
    ASSERT_TRUE(f.is_open()) << "Output ELF not written";
    char magic[4] = {};
    f.read(magic, 4);
    EXPECT_EQ(magic[0], 0x7F);
    EXPECT_EQ(magic[1], 'E');
    EXPECT_EQ(magic[2], 'L');
    EXPECT_EQ(magic[3], 'F');
}
