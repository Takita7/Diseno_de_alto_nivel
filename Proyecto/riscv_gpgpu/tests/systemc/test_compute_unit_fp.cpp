// test_compute_unit_fp.cpp — Phase 5e: RV32F floating-point unit tests
//
// Tests the RV32F (single-precision FP) execution path in ComputeUnit.
// Instructions are hand-encoded and written directly to a MemoryHierarchy.
// The ComputeUnit runs in binary mode (setEntryPoint / step / isComplete).
//
// Sentinel mechanism: ra (x1) = 0x1, setReturnSentinel(0x1).
//   ret = jalr x0, 0(ra): target = (1 + 0) & ~1 = 0 == (1 & ~1) = 0 → halt.

#include <gtest/gtest.h>
#include <cstring>
#include <array>
#include <cmath>
#include <memory>

#include "../../models/systemc/src/compute_unit/compute_unit.h"
#include "../../models/systemc/src/memory/memory_hierarchy.h"

using namespace riscv_gpgpu;

// ── Constants ─────────────────────────────────────────────────────────────────

static constexpr uint32_t CODE_BASE = 0x1000u;
static constexpr uint32_t DATA_BASE = 0x3000u;
static constexpr uint32_t SENTINEL  = 0x0001u;

// ── Memory helpers ────────────────────────────────────────────────────────────

static void storeFloat(MemoryHierarchy& mem, uint32_t addr, float val) {
    uint32_t bits; std::memcpy(&bits, &val, 4);
    uint32_t lat = 0;
    mem.storeWord(addr, bits, lat);
}

static float loadFloat(MemoryHierarchy& mem, uint32_t addr) {
    uint32_t bits = 0, lat = 0;
    mem.loadWord(addr, bits, lat);
    float val; std::memcpy(&val, &bits, 4);
    return val;
}

static void writeProgram(MemoryHierarchy& mem,
                         std::initializer_list<uint32_t> words) {
    uint32_t addr = CODE_BASE;
    for (uint32_t w : words) {
        uint32_t lat = 0;
        mem.storeWord(addr, w, lat);
        addr += 4;
    }
}

static void runToCompletion(ComputeUnit& cu) {
    while (!cu.isComplete()) cu.step();
}

// Set up the standard binary-mode registers (ra=sentinel, rest=provided).
static void setupRegs(ComputeUnit& cu,
                      std::array<uint32_t, 32> regs = {}) {
    regs[1] = SENTINEL;   // ra
    cu.setInitialRegisters(regs);
    cu.setReturnSentinel(SENTINEL);
    cu.setEntryPoint(CODE_BASE);
}

// ─── Instruction encodings ─────────────────────────────────────────────────────
//
// Helper: encode OP-FP instruction
//   funct7(7) | rs2(5) | rs1(5) | funct3(3) | rd(5) | 0x53(7)
static constexpr uint32_t enc_opfp(uint8_t funct7, uint8_t rs2, uint8_t rs1,
                                    uint8_t funct3, uint8_t rd) {
    return (static_cast<uint32_t>(funct7) << 25)
         | (static_cast<uint32_t>(rs2)    << 20)
         | (static_cast<uint32_t>(rs1)    << 15)
         | (static_cast<uint32_t>(funct3) << 12)
         | (static_cast<uint32_t>(rd)     <<  7)
         | 0x53u;
}

// Helper: encode FMADD (0x43) / FMSUB (0x47) / FNMSUB (0x4B) / FNMADD (0x4F)
//   rs3(5) | funct2=0(2) | rs2(5) | rs1(5) | funct3=0(3) | rd(5) | opcode
static constexpr uint32_t enc_r4(uint8_t opcode, uint8_t rs3, uint8_t rs2,
                                  uint8_t rs1, uint8_t rd) {
    return (static_cast<uint32_t>(rs3) << 27)
         | (static_cast<uint32_t>(0)   << 25)   // funct2=0 (single)
         | (static_cast<uint32_t>(rs2) << 20)
         | (static_cast<uint32_t>(rs1) << 15)
         | (static_cast<uint32_t>(0)   << 12)   // funct3=0 (RNE)
         | (static_cast<uint32_t>(rd)  <<  7)
         | static_cast<uint32_t>(opcode);
}

// Helper: encode FLW  fd, imm(rs1)   opcode=0x07, funct3=2
static constexpr uint32_t enc_flw(uint8_t fd, uint8_t rs1, int16_t imm) {
    return (static_cast<uint32_t>(imm & 0xFFF) << 20)
         | (static_cast<uint32_t>(rs1)          << 15)
         | (2u                                  << 12)
         | (static_cast<uint32_t>(fd)           <<  7)
         | 0x07u;
}

// Helper: encode FSW  fs2, imm(rs1)  opcode=0x27, funct3=2
static constexpr uint32_t enc_fsw(uint8_t fs2, uint8_t rs1, int16_t imm) {
    uint32_t uimm = static_cast<uint32_t>(imm) & 0xFFF;
    return ((uimm >> 5)                << 25)
         | (static_cast<uint32_t>(fs2) << 20)
         | (static_cast<uint32_t>(rs1) << 15)
         | (2u                         << 12)
         | ((uimm & 0x1F)              <<  7)
         | 0x27u;
}

static constexpr uint32_t RET = 0x00008067u;   // jalr x0, 0(ra)

// Register ABI aliases
static constexpr uint8_t FA0 = 10, FA1 = 11, FA2 = 12, FA3 = 13;
static constexpr uint8_t T0  =  5, T1  =  6;
static constexpr uint8_t A0  = 10, A1  = 11;

// ─── Test fixture ─────────────────────────────────────────────────────────────
//
// Allocates MemoryHierarchy and ComputeUnit on the heap (sc_modules are
// non-copyable, so the factory-return-by-value pattern is not usable).

struct FPFixture {
    MemoryHierarchy::Config mem_cfg;
    ComputeUnit::Config     cu_cfg;

    std::unique_ptr<MemoryHierarchy> mem;
    std::unique_ptr<ComputeUnit>     cu;

    explicit FPFixture(const std::string& name) {
        cu_cfg.unit_id    = 0;
        cu_cfg.max_cycles = 200;
        mem = std::make_unique<MemoryHierarchy>(
            sc_core::sc_module_name((name + "_mem").c_str()), mem_cfg);
        cu  = std::make_unique<ComputeUnit>(
            sc_core::sc_module_name((name + "_cu").c_str()), cu_cfg);
        cu->setMemoryHierarchy(mem.get());
    }

    void load(std::initializer_list<uint32_t> words) {
        writeProgram(*mem, words);
    }

    void initRegs(std::array<uint32_t, 32> regs = {}) {
        setupRegs(*cu, regs);
    }

    void initFRegs(std::array<float, 32> fr) {
        cu->setInitialFloatRegisters(fr);
    }

    void run() { runToCompletion(*cu); }

    uint32_t ireg(uint8_t n) const { return cu->getRegister(0, n); }
    float    freg(uint8_t n) const { return cu->getFloatRegister(n); }
};

// ─── Tests ────────────────────────────────────────────────────────────────────

TEST(ComputeUnitFP, FaddS) {
    FPFixture f("fp_fadd");
    f.load({ enc_opfp(0x00, FA2, FA1, 0, FA0), RET });
    f.initRegs();
    std::array<float, 32> fr{}; fr[FA1] = 1.5f; fr[FA2] = 2.5f;
    f.initFRegs(fr);
    f.run();
    EXPECT_FLOAT_EQ(f.freg(FA0), 4.0f);
}

TEST(ComputeUnitFP, FsubS) {
    FPFixture f("fp_fsub");
    f.load({ enc_opfp(0x04, FA2, FA1, 0, FA0), RET });
    f.initRegs();
    std::array<float, 32> fr{}; fr[FA1] = 5.0f; fr[FA2] = 1.5f;
    f.initFRegs(fr);
    f.run();
    EXPECT_FLOAT_EQ(f.freg(FA0), 3.5f);
}

TEST(ComputeUnitFP, FmulS) {
    FPFixture f("fp_fmul");
    f.load({ enc_opfp(0x08, FA2, FA1, 0, FA0), RET });
    f.initRegs();
    std::array<float, 32> fr{}; fr[FA1] = 2.0f; fr[FA2] = 3.0f;
    f.initFRegs(fr);
    f.run();
    EXPECT_FLOAT_EQ(f.freg(FA0), 6.0f);
}

TEST(ComputeUnitFP, FmaddS) {
    // fmadd.s fa0, fa1, fa2, fa3  →  fa0 = fa1*fa2 + fa3
    FPFixture f("fp_fmadd");
    f.load({ enc_r4(0x43, FA3, FA2, FA1, FA0), RET });
    f.initRegs();
    std::array<float, 32> fr{}; fr[FA1]=2.0f; fr[FA2]=3.0f; fr[FA3]=1.0f;
    f.initFRegs(fr);
    f.run();
    EXPECT_FLOAT_EQ(f.freg(FA0), 7.0f);  // 2*3+1
}

TEST(ComputeUnitFP, FmsubS) {
    // fmsub.s fa0, fa1, fa2, fa3  →  fa0 = fa1*fa2 - fa3
    FPFixture f("fp_fmsub");
    f.load({ enc_r4(0x47, FA3, FA2, FA1, FA0), RET });
    f.initRegs();
    std::array<float, 32> fr{}; fr[FA1]=2.0f; fr[FA2]=3.0f; fr[FA3]=1.0f;
    f.initFRegs(fr);
    f.run();
    EXPECT_FLOAT_EQ(f.freg(FA0), 5.0f);  // 2*3-1
}

TEST(ComputeUnitFP, FsqrtS) {
    FPFixture f("fp_fsqrt");
    f.load({ enc_opfp(0x2C, 0, FA1, 0, FA0), RET });
    f.initRegs();
    std::array<float, 32> fr{}; fr[FA1] = 9.0f;
    f.initFRegs(fr);
    f.run();
    EXPECT_NEAR(f.freg(FA0), 3.0f, 1e-6f);
}

TEST(ComputeUnitFP, FltSTrue) {
    // flt.s t0, fa1, fa2  →  t0 = (fa1 < fa2) ? 1 : 0
    FPFixture f("fp_flt");
    f.load({ enc_opfp(0x50, FA2, FA1, 1, T0), RET });
    f.initRegs();
    std::array<float, 32> fr{}; fr[FA1] = 1.0f; fr[FA2] = 2.0f;
    f.initFRegs(fr);
    f.run();
    EXPECT_EQ(f.ireg(T0), 1u);
}

TEST(ComputeUnitFP, FltSFalse) {
    FPFixture f("fp_flt2");
    f.load({ enc_opfp(0x50, FA2, FA1, 1, T0), RET });
    f.initRegs();
    std::array<float, 32> fr{}; fr[FA1] = 2.0f; fr[FA2] = 1.0f;
    f.initFRegs(fr);
    f.run();
    EXPECT_EQ(f.ireg(T0), 0u);
}

TEST(ComputeUnitFP, FeqS) {
    FPFixture f("fp_feq");
    f.load({ enc_opfp(0x50, FA2, FA1, 2, T0), RET });
    f.initRegs();
    std::array<float, 32> fr{}; fr[FA1] = 3.14f; fr[FA2] = 3.14f;
    f.initFRegs(fr);
    f.run();
    EXPECT_EQ(f.ireg(T0), 1u);
}

TEST(ComputeUnitFP, FlwFswRoundTrip) {
    // flw fa0, 0(a0)  →  fadd.s fa0, fa0, fa1  →  fsw fa0, 0(a1)
    FPFixture f("fp_ldst");
    f.load({
        enc_flw(FA0, A0, 0),                   // flw  fa0, 0(a0)
        enc_opfp(0x00, FA1, FA0, 0, FA0),      // fadd.s fa0, fa0, fa1
        enc_fsw(FA0, A1, 0),                   // fsw  fa0, 0(a1)
        RET
    });

    storeFloat(*f.mem, DATA_BASE,     2.0f);   // x[0] = 2.0

    std::array<uint32_t, 32> regs{};
    regs[A0] = DATA_BASE;
    regs[A1] = DATA_BASE + 4;
    f.initRegs(regs);

    std::array<float, 32> fr{}; fr[FA1] = 1.0f;
    f.initFRegs(fr);
    f.run();

    EXPECT_FLOAT_EQ(loadFloat(*f.mem, DATA_BASE + 4), 3.0f);  // 2.0 + 1.0
}

TEST(ComputeUnitFP, FcvtWS) {
    // fcvt.w.s t0, fa1  →  t0 = (int32_t)fa1
    FPFixture f("fp_cvtwf");
    f.load({ enc_opfp(0x60, 0, FA1, 0, T0), RET });
    f.initRegs();
    std::array<float, 32> fr{}; fr[FA1] = 3.0f;
    f.initFRegs(fr);
    f.run();
    EXPECT_EQ(f.ireg(T0), 3u);
}

TEST(ComputeUnitFP, FcvtSW) {
    // fcvt.s.w fa0, t1  →  fa0 = (float)t1
    FPFixture f("fp_cvtfw");
    f.load({ enc_opfp(0x68, 0, T1, 0, FA0), RET });
    std::array<uint32_t, 32> regs{}; regs[T1] = 7;
    f.initRegs(regs);
    f.run();
    EXPECT_FLOAT_EQ(f.freg(FA0), 7.0f);
}

TEST(ComputeUnitFP, FmvXW) {
    // fmv.x.w t0, fa1  →  t0 = float_bits(1.0f) = 0x3F800000
    FPFixture f("fp_fmvxw");
    f.load({ enc_opfp(0x70, 0, FA1, 0, T0), RET });
    f.initRegs();
    std::array<float, 32> fr{}; fr[FA1] = 1.0f;
    f.initFRegs(fr);
    f.run();
    EXPECT_EQ(f.ireg(T0), 0x3F800000u);
}

TEST(ComputeUnitFP, FmvWX) {
    // fmv.w.x fa0, t1  →  fa0 = bits_as_float(0x40000000) = 2.0f
    FPFixture f("fp_fmvwx");
    f.load({ enc_opfp(0x78, 0, T1, 0, FA0), RET });
    std::array<uint32_t, 32> regs{}; regs[T1] = 0x40000000u;
    f.initRegs(regs);
    f.run();
    EXPECT_FLOAT_EQ(f.freg(FA0), 2.0f);
}

TEST(ComputeUnitFP, SaxpyOneElement) {
    // Simulates one SAXPY element: result = a * x + y
    //   flw  fa0, 0(a0)           ; fa0 = x
    //   flw  fa1, 0(a1)           ; fa1 = y
    //   fmadd.s fa0, fa2, fa0, fa1 ; fa0 = a*x + y  (fa2=a, fa0=x, fa1=y)
    //   fsw  fa0, 0(a2)            ; result = fa0
    //   ret
    static constexpr uint8_t A2 = 12;
    FPFixture f("fp_saxpy");
    f.load({
        enc_flw(FA0, A0, 0),                      // flw  fa0, 0(a0)
        enc_flw(FA1, A1, 0),                      // flw  fa1, 0(a1)
        enc_r4(0x43, FA1, FA0, FA2, FA0),          // fmadd.s fa0, fa2, fa0, fa1
        enc_fsw(FA0, A2, 0),                      // fsw  fa0, 0(a2)
        RET
    });

    storeFloat(*f.mem, DATA_BASE,     3.0f);   // x
    storeFloat(*f.mem, DATA_BASE + 4, 2.0f);   // y

    std::array<uint32_t, 32> regs{};
    regs[A0] = DATA_BASE;
    regs[A1] = DATA_BASE + 4;
    regs[A2] = DATA_BASE + 8;
    f.initRegs(regs);

    std::array<float, 32> fr{}; fr[FA2] = 2.5f;   // a = 2.5
    f.initFRegs(fr);
    f.run();

    // expected: 2.5 * 3.0 + 2.0 = 9.5
    EXPECT_NEAR(loadFloat(*f.mem, DATA_BASE + 8), 9.5f, 1e-6f);
}

//
// Tests the RV32F (single-precision FP) execution path in ComputeUnit.
// Instructions are hand-encoded and written directly to a MemoryHierarchy.
// The ComputeUnit runs in binary mode (setEntryPoint / step / isComplete).
//
// Sentinel mechanism: ra (x1) = 0x1, setReturnSentinel(0x1).
//   ret = jalr x0, 0(ra): target = (1 + 0) & ~1 = 0 == (1 & ~1) = 0 → halt.

#include <gtest/gtest.h>
#include <cstring>
#include <array>
#include <cmath>

#include "../../models/systemc/src/compute_unit/compute_unit.h"
#include "../../models/systemc/src/memory/memory_hierarchy.h"

using namespace riscv_gpgpu;
