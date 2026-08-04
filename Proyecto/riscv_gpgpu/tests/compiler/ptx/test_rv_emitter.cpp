// test_rv_emitter.cpp — Unit tests for the PTX→RISC-V assembly emitter (T057)

#include <gtest/gtest.h>
#include <string>
#include "ptx_parser.h"
#include "rv_emitter.h"

using namespace riscv_gpgpu::ptx;

static std::string emitForPtx(const char* ptx) {
    PtxParser p;
    auto k = p.parse(ptx);
    if (k.name.empty()) return "";
    RvEmitter e;
    return e.emit(k);
}

// ── Helpers ───────────────────────────────────────────────────────────────────

static bool contains(const std::string& haystack, const std::string& needle) {
    return haystack.find(needle) != std::string::npos;
}

// ── Tests ─────────────────────────────────────────────────────────────────────

TEST(RvEmitter, LowersPtx64PointerAddressesToRv32) {
    const char* ptx = R"(
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
    std::string asm_out = emitForPtx(ptx);
    EXPECT_FALSE(asm_out.empty()) << asm_out;
    EXPECT_TRUE(contains(asm_out, "slli")) << asm_out;
    EXPECT_TRUE(contains(asm_out, "lw")) << asm_out;
    EXPECT_TRUE(contains(asm_out, "sw")) << asm_out;
}

TEST(RvEmitter, RejectsPtx64DataLoad) {
    const char* ptx = R"(
.address_size 64
.entry k(.param .u64 p0) {
    .reg .b64 %rd<2>;
    ld.param.u64 %rd0, [p0];
    ld.global.u64 %rd1, [%rd0];
    ret;
})";
    PtxParser parser;
    auto kernel = parser.parse(ptx);
    ASSERT_FALSE(kernel.name.empty()) << parser.lastError();
    RvEmitter emitter;
    EXPECT_TRUE(emitter.emit(kernel).empty());
    EXPECT_NE(emitter.lastError().find("64-bit memory"), std::string::npos);
}

TEST(RvEmitter, RejectsUnsupportedOpcode) {
    const char* ptx = R"(
.address_size 64
.entry k() {
    unsupported.u32 %r0, %r1;
    ret;
})";
    PtxParser parser;
    auto kernel = parser.parse(ptx);
    ASSERT_FALSE(kernel.name.empty()) << parser.lastError();
    RvEmitter emitter;
    EXPECT_TRUE(emitter.emit(kernel).empty());
    EXPECT_FALSE(emitter.lastError().empty());
}

TEST(RvEmitter, EmitsKernelLabel) {
    const char* ptx = R"(
.visible .entry my_kernel(.param .u32 p0) {
    .reg .u32 %r<2>;
    ld.param.u32 %r0, [p0];
    ret;
})";
    std::string asm_out = emitForPtx(ptx);
    EXPECT_TRUE(contains(asm_out, "my_kernel:")) << asm_out;
    EXPECT_TRUE(contains(asm_out, ".globl my_kernel")) << asm_out;
}

TEST(RvEmitter, EmitsLdParamAsMove) {
    const char* ptx = R"(
.visible .entry k(.param .u32 p0, .param .u32 p1) {
    .reg .u32 %r<4>;
    ld.param.u32 %r0, [p0];
    ld.param.u32 %r1, [p1];
    ret;
})";
    std::string asm_out = emitForPtx(ptx);
    // Params should map to a0, a1 moves
    EXPECT_TRUE(contains(asm_out, "mv") && contains(asm_out, "a0")) << asm_out;
}

TEST(RvEmitter, EmitsMovTidX) {
    const char* ptx = R"(
.visible .entry k(.param .u32 p0) {
    .reg .u32 %r<2>;
    mov.u32 %r0, %tid.x;
    ret;
})";
    std::string asm_out = emitForPtx(ptx);
    // %tid.x at offset 0 from gp
    EXPECT_TRUE(contains(asm_out, "0(gp)")) << asm_out;
    EXPECT_TRUE(contains(asm_out, "lw")) << asm_out;
}

TEST(RvEmitter, EmitsMovCtaidX) {
    const char* ptx = R"(
.visible .entry k(.param .u32 p0) {
    .reg .u32 %r<2>;
    mov.u32 %r0, %ctaid.x;
    ret;
})";
    std::string asm_out = emitForPtx(ptx);
    // %ctaid.x at offset 12 from gp
    EXPECT_TRUE(contains(asm_out, "12(gp)")) << asm_out;
}

TEST(RvEmitter, EmitsMovNtidX) {
    const char* ptx = R"(
.visible .entry k(.param .u32 p0) {
    .reg .u32 %r<2>;
    mov.u32 %r0, %ntid.x;
    ret;
})";
    std::string asm_out = emitForPtx(ptx);
    // %ntid.x at offset 24 from gp
    EXPECT_TRUE(contains(asm_out, "24(gp)")) << asm_out;
}

TEST(RvEmitter, EmitsAddU32) {
    const char* ptx = R"(
.visible .entry k(.param .u32 p0) {
    .reg .u32 %r<4>;
    ld.param.u32 %r0, [p0];
    add.u32 %r1, %r0, %r0;
    ret;
})";
    std::string asm_out = emitForPtx(ptx);
    EXPECT_TRUE(contains(asm_out, "add")) << asm_out;
}

TEST(RvEmitter, EmitsMulLo4AsShift) {
    // mul.lo.u32 %r, %r, 4  → slli r, r, 2
    const char* ptx = R"(
.visible .entry k(.param .u32 p0) {
    .reg .u32 %r<4>;
    ld.param.u32 %r0, [p0];
    mul.lo.u32 %r1, %r0, 4;
    ret;
})";
    std::string asm_out = emitForPtx(ptx);
    EXPECT_TRUE(contains(asm_out, "slli") && contains(asm_out, ", 2")) << asm_out;
}

TEST(RvEmitter, EmitsMulLo3AsMul) {
    // mul.lo.u32 %r, %r, 3  → (not power of 2) uses mul
    const char* ptx = R"(
.visible .entry k(.param .u32 p0) {
    .reg .u32 %r<4>;
    ld.param.u32 %r0, [p0];
    mul.lo.u32 %r1, %r0, 3;
    ret;
})";
    std::string asm_out = emitForPtx(ptx);
    EXPECT_TRUE(contains(asm_out, "mul")) << asm_out;
}

TEST(RvEmitter, EmitsSetpGe) {
    const char* ptx = R"(
.visible .entry k(.param .u32 p0, .param .u32 p1) {
    .reg .pred %p<2>;
    .reg .u32 %r<4>;
    ld.param.u32 %r0, [p0];
    ld.param.u32 %r1, [p1];
    setp.ge.u32 %p0, %r0, %r1;
    ret;
})";
    std::string asm_out = emitForPtx(ptx);
    // setp.ge = sltu + xori
    EXPECT_TRUE(contains(asm_out, "sltu") || contains(asm_out, "slt")) << asm_out;
    EXPECT_TRUE(contains(asm_out, "xori")) << asm_out;
}

TEST(RvEmitter, EmitsUnsignedSetpImmediate) {
    const char* ptx = R"(
.visible .entry k() {
    .reg .pred %p<1>;
    .reg .u32 %r<1>;
    mov.u32 %r0, 31;
    setp.ge.u32 %p0, %r0, 16;
    ret;
})";
    std::string asm_out = emitForPtx(ptx);
    EXPECT_TRUE(contains(asm_out, "sltiu")) << asm_out;
}

TEST(RvEmitter, EmitsPredicatedBranch) {
    const char* ptx = R"(
.visible .entry k(.param .u32 p0) {
    .reg .pred %p<2>;
    .reg .u32 %r<2>;
    ld.param.u32 %r0, [p0];
    setp.ge.u32 %p0, %r0, %r0;
    @%p0 bra $L__end;
$L__end:
    ret;
})";
    std::string asm_out = emitForPtx(ptx);
    EXPECT_TRUE(contains(asm_out, "bnez") || contains(asm_out, "bne")) << asm_out;
    EXPECT_TRUE(contains(asm_out, "L__end")) << asm_out;
}

TEST(RvEmitter, EmitsLdGlobal) {
    const char* ptx = R"(
.visible .entry k(.param .u32 p0) {
    .reg .u32 %r<4>;
    ld.param.u32 %r0, [p0];
    ld.global.u32 %r1, [%r0];
    ret;
})";
    std::string asm_out = emitForPtx(ptx);
    EXPECT_TRUE(contains(asm_out, "lw")) << asm_out;
    EXPECT_TRUE(contains(asm_out, "0(")) << asm_out;
}

TEST(RvEmitter, EmitsStGlobal) {
    const char* ptx = R"(
.visible .entry k(.param .u32 p0, .param .u32 p1) {
    .reg .u32 %r<4>;
    ld.param.u32 %r0, [p0];
    ld.param.u32 %r1, [p1];
    st.global.u32 [%r0], %r1;
    ret;
})";
    std::string asm_out = emitForPtx(ptx);
    EXPECT_TRUE(contains(asm_out, "sw")) << asm_out;
}

TEST(RvEmitter, EmitsFpFmadd) {
    const char* ptx = R"(
.visible .entry k(.param .f32 pa, .param .u32 px, .param .u32 py, .param .u32 pz) {
    .reg .f32 %f<4>;
    .reg .u32 %r<4>;
    ld.param.f32 %f0, [pa];
    ld.param.u32 %r0, [px];
    ld.global.f32 %f1, [%r0];
    ld.param.u32 %r1, [py];
    ld.global.f32 %f2, [%r1];
    fma.rn.f32 %f3, %f0, %f1, %f2;
    ret;
})";
    std::string asm_out = emitForPtx(ptx);
    EXPECT_TRUE(contains(asm_out, "fmadd.s")) << asm_out;
    EXPECT_TRUE(contains(asm_out, "flw")) << asm_out;
    EXPECT_TRUE(contains(asm_out, "fmv.w.x")) << asm_out;
}

TEST(RvEmitter, EmitsSharedAccessWithSharedBase) {
    const char* ptx = R"(
.visible .entry k() {
    .reg .u32 %r<3>;
    mov.u32 %r0, 4;
    mov.u32 %r1, 7;
    st.shared.u32 [%r0], %r1;
    ld.shared.u32 %r2, [%r0];
    ret;
})";
    std::string asm_out = emitForPtx(ptx);
    EXPECT_TRUE(contains(asm_out, "li t6, 0x00400000")) << asm_out;
    EXPECT_TRUE(contains(asm_out, "sw")) << asm_out;
    EXPECT_TRUE(contains(asm_out, "lw")) << asm_out;
}

TEST(RvEmitter, EmitsBarSyncCustomInstruction) {
    const char* ptx = R"(
.visible .entry k() {
    bar.sync 0;
    ret;
})";
    std::string asm_out = emitForPtx(ptx);
    EXPECT_TRUE(contains(asm_out, ".4byte 0xb")) << asm_out;
    EXPECT_FALSE(contains(asm_out, "fence  # bar.sync")) << asm_out;
}

TEST(RvEmitter, EmitsRet) {
    const char* ptx = R"(
.visible .entry k(.param .u32 p0) {
    .reg .u32 %r<1>;
    ret;
})";
    std::string asm_out = emitForPtx(ptx);
    EXPECT_TRUE(contains(asm_out, "ret")) << asm_out;
}

TEST(RvEmitter, EmitsValidAsmForVectorAdd) {
    // Full vector_add: check that the output contains all key elements
    const char* ptx = R"(
.visible .entry vector_add(
    .param .u32 va_p0,
    .param .u32 va_p1,
    .param .u32 va_p2,
    .param .u32 va_p3
) {
    .reg .pred %p<2>;
    .reg .u32  %r<16>;
    ld.param.u32  %r0, [va_p0];
    ld.param.u32  %r1, [va_p1];
    ld.param.u32  %r2, [va_p2];
    ld.param.u32  %r3, [va_p3];
    mov.u32       %r4, %tid.x;
    mov.u32       %r5, %ctaid.x;
    mov.u32       %r6, %ntid.x;
    mul.lo.u32    %r7, %r5, %r6;
    add.u32       %r7, %r7, %r4;
    setp.ge.u32   %p0, %r7, %r3;
    @%p0 bra      $L__end;
    mul.lo.u32    %r8, %r7, 4;
    add.u32       %r9, %r0, %r8;
    ld.global.u32 %r10, [%r9];
    add.u32       %r11, %r1, %r8;
    ld.global.u32 %r12, [%r11];
    add.u32       %r10, %r10, %r12;
    add.u32       %r13, %r2, %r8;
    st.global.u32 [%r13], %r10;
$L__end:
    ret;
})";
    std::string asm_out = emitForPtx(ptx);
    EXPECT_FALSE(asm_out.empty()) << "Emitter returned empty string";
    EXPECT_TRUE(contains(asm_out, "vector_add:"))     << asm_out;
    EXPECT_TRUE(contains(asm_out, "0(gp)"))           << "Missing tid.x load from gp";
    EXPECT_TRUE(contains(asm_out, "mul"))             << "Missing mul/slli for address calc";
    EXPECT_TRUE(contains(asm_out, "lw"))              << "Missing lw for ld.global";
    EXPECT_TRUE(contains(asm_out, "sw"))              << "Missing sw for st.global";
    EXPECT_TRUE(contains(asm_out, "bnez") || contains(asm_out, "bne")) << "Missing conditional branch";
    EXPECT_TRUE(contains(asm_out, "L__end"))          << "Missing label";
    EXPECT_TRUE(contains(asm_out, "ret"))             << "Missing ret";
}
