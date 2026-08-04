// test_ptx_parser.cpp — Unit tests for the PTX lexer + parser (T056)

#include <gtest/gtest.h>
#include "ptx_parser.h"

using namespace riscv_gpgpu::ptx;

// ── Canonical test kernel ─────────────────────────────────────────────────────

static const char* kVectorAddPtx = R"(
.version 7.0
.target sm_20
.address_size 32

.visible .entry vector_add(
    .param .u32 vector_add_param_0,
    .param .u32 vector_add_param_1,
    .param .u32 vector_add_param_2,
    .param .u32 vector_add_param_3
)
{
    .reg .pred  %p<2>;
    .reg .u32   %r<16>;

    ld.param.u32    %r0, [vector_add_param_0];
    ld.param.u32    %r1, [vector_add_param_1];
    ld.param.u32    %r2, [vector_add_param_2];
    ld.param.u32    %r3, [vector_add_param_3];

    mov.u32         %r4, %tid.x;
    mov.u32         %r5, %ctaid.x;
    mov.u32         %r6, %ntid.x;

    mul.lo.u32      %r7, %r5, %r6;
    add.u32         %r7, %r7, %r4;

    setp.ge.u32     %p0, %r7, %r3;
    @%p0 bra        $L__BB0_end;

    mul.lo.u32      %r8, %r7, 4;

    add.u32         %r9, %r0, %r8;
    ld.global.u32   %r10, [%r9];

    add.u32         %r11, %r1, %r8;
    ld.global.u32   %r12, [%r11];

    add.u32         %r10, %r10, %r12;

    add.u32         %r13, %r2, %r8;
    st.global.u32   [%r13], %r10;

$L__BB0_end:
    ret;
}
)";

static const char* kSaxpyPtx = R"(
.version 7.0
.target sm_20
.address_size 32

.visible .entry saxpy(
    .param .f32 saxpy_param_0,
    .param .u32 saxpy_param_1,
    .param .u32 saxpy_param_2,
    .param .u32 saxpy_param_3,
    .param .u32 saxpy_param_4
)
{
    .reg .pred  %p<2>;
    .reg .u32   %r<12>;
    .reg .f32   %f<4>;

    ld.param.f32    %f0, [saxpy_param_0];
    ld.param.u32    %r0, [saxpy_param_1];
    ld.param.u32    %r1, [saxpy_param_2];
    ld.param.u32    %r2, [saxpy_param_3];
    ld.param.u32    %r3, [saxpy_param_4];

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

// ── Tests ─────────────────────────────────────────────────────────────────────

TEST(PtxParser, ParsesKernelName) {
    PtxParser p;
    auto k = p.parse(kVectorAddPtx);
    EXPECT_EQ(k.name, "vector_add") << p.lastError();
}

TEST(PtxParser, ParsesParameterCount) {
    PtxParser p;
    auto k = p.parse(kVectorAddPtx);
    ASSERT_EQ(k.params.size(), 4u);
    EXPECT_EQ(k.params[0].name, "vector_add_param_0");
    EXPECT_EQ(k.params[0].space, ".u32");
    EXPECT_EQ(k.params[0].index, 0u);
    EXPECT_EQ(k.params[3].index, 3u);
}

TEST(PtxParser, ParsesRegisterDeclarations) {
    PtxParser p;
    auto k = p.parse(kVectorAddPtx);
    ASSERT_GE(k.reg_decls.size(), 2u);

    bool has_pred = false, has_u32 = false;
    for (const auto& rd : k.reg_decls) {
        if (rd.type == "pred" && rd.count == 2) has_pred = true;
        if (rd.type == "u32"  && rd.count == 16) has_u32  = true;
    }
    EXPECT_TRUE(has_pred) << "Missing .reg .pred %p<2>";
    EXPECT_TRUE(has_u32)  << "Missing .reg .u32  %r<16>";
}

TEST(PtxParser, ParsesLdParamInstructions) {
    PtxParser p;
    auto k = p.parse(kVectorAddPtx);
    // First 4 instructions should be ld.param
    int count = 0;
    for (const auto& instr : k.body) {
        if (!instr.label.empty()) continue;
        if (instr.op.find("ld.param") != std::string::npos) ++count;
    }
    EXPECT_EQ(count, 4);
}

TEST(PtxParser, ParsesMovWithSpecialReg) {
    PtxParser p;
    auto k = p.parse(kVectorAddPtx);
    bool found_tid = false;
    for (const auto& instr : k.body) {
        if (instr.op == "mov.u32" && !instr.operands.empty()) {
            for (const auto& op : instr.operands) {
                if (op.kind == PtxOperand::Kind::SpecialReg && op.name == "tid.x")
                    found_tid = true;
            }
        }
    }
    EXPECT_TRUE(found_tid) << "Expected mov.u32 %r, %tid.x";
}

TEST(PtxParser, ParsesSetpWithPredDst) {
    PtxParser p;
    auto k = p.parse(kVectorAddPtx);
    bool found_setp = false;
    for (const auto& instr : k.body) {
        if (instr.op.find("setp.ge") != std::string::npos) {
            found_setp = true;
            ASSERT_GE(instr.operands.size(), 3u);
            EXPECT_EQ(instr.operands[0].name, "p0");
        }
    }
    EXPECT_TRUE(found_setp);
}

TEST(PtxParser, ParsesPredicatedBranch) {
    PtxParser p;
    auto k = p.parse(kVectorAddPtx);
    bool found = false;
    for (const auto& instr : k.body) {
        if (instr.op == "bra" && !instr.pred.empty()) {
            found = true;
            EXPECT_EQ(instr.pred, "p0");
            EXPECT_FALSE(instr.pred_not);
            ASSERT_FALSE(instr.operands.empty());
            EXPECT_EQ(instr.operands[0].name, "L__BB0_end");
        }
    }
    EXPECT_TRUE(found) << "Expected @%p0 bra $L__BB0_end";
}

TEST(PtxParser, ParsesLabel) {
    PtxParser p;
    auto k = p.parse(kVectorAddPtx);
    bool found = false;
    for (const auto& instr : k.body) {
        if (instr.label == "L__BB0_end") { found = true; break; }
    }
    EXPECT_TRUE(found) << "Expected label L__BB0_end";
}

TEST(PtxParser, ParsesLdGlobalMemRef) {
    PtxParser p;
    auto k = p.parse(kVectorAddPtx);
    bool found = false;
    for (const auto& instr : k.body) {
        if (instr.op == "ld.global.u32") {
            found = true;
            ASSERT_GE(instr.operands.size(), 2u);
            EXPECT_EQ(instr.operands[1].kind, PtxOperand::Kind::MemRef);
        }
    }
    EXPECT_TRUE(found);
}

TEST(PtxParser, ParsesStGlobal) {
    PtxParser p;
    auto k = p.parse(kVectorAddPtx);
    bool found = false;
    for (const auto& instr : k.body) {
        if (instr.op == "st.global.u32") {
            found = true;
            ASSERT_GE(instr.operands.size(), 2u);
            EXPECT_EQ(instr.operands[0].kind, PtxOperand::Kind::MemRef);
        }
    }
    EXPECT_TRUE(found);
}

TEST(PtxParser, ParsesRet) {
    PtxParser p;
    auto k = p.parse(kVectorAddPtx);
    bool found = false;
    for (const auto& instr : k.body) {
        if (instr.op == "ret") { found = true; break; }
    }
    EXPECT_TRUE(found);
}

TEST(PtxParser, ParsesSaxpyFpKernel) {
    PtxParser p;
    auto k = p.parse(kSaxpyPtx);
    EXPECT_EQ(k.name, "saxpy") << p.lastError();
    ASSERT_EQ(k.params.size(), 5u);
    EXPECT_EQ(k.params[0].space, ".f32");  // first param is float

    // Check fma.rn.f32 is parsed
    bool found_fma = false;
    for (const auto& instr : k.body) {
        if (instr.op == "fma.rn.f32") { found_fma = true; break; }
    }
    EXPECT_TRUE(found_fma);
}

TEST(PtxParser, ParsesMulImmediate) {
    PtxParser p;
    auto k = p.parse(kVectorAddPtx);
    bool found = false;
    for (const auto& instr : k.body) {
        if (instr.op == "mul.lo.u32" && instr.operands.size() >= 3) {
            const auto& third = instr.operands[2];
            if (third.kind == PtxOperand::Kind::IntImm && third.int_val == 4) {
                found = true;
            }
        }
    }
    EXPECT_TRUE(found) << "Expected mul.lo.u32 %rX, %rY, 4";
}

TEST(PtxParser, EmptyOrInvalidPtx) {
    PtxParser p;
    auto k = p.parse("// no kernel here\n.version 7.0\n");
    EXPECT_TRUE(k.name.empty());
    EXPECT_FALSE(p.lastError().empty());
}
