// rv32i_codec.h - RV32I + custom-opcode encode/decode (docs/hls/interfaces.md
// SS13)
//
// Bridges the two representations SS13.2 defines: raw_instr_t (the on-chip
// program-store/wire form - real RV32I bits, what CuDispatchUnit::program_[]
// and compute_pipeline's program[] parameter actually hold) and Instruction
// (the decoded, internal form - unchanged, still what executeALU/
// executeVector/executeMemOp/executeBranch/executeJoin all consume). Bit
// positions below are copied directly from SS13.3/13.4/13.5/13.6's tables,
// which are themselves either the real RV32I spec (standard opcodes) or a
// self-consistent scheme modeled on real spec precedent (custom-0/custom-1 -
// see SS13.5's funct7-group/funct3-index note, mirroring how RV32M shares
// OP's major opcode with ADD/SUB via funct7).
//
// header-only, matching cu_dispatch_unit.h/barrier_arbiter.h/mem_arbiter.h's
// existing convention for small, dependency-light HLS components.

#ifndef RISCV_GPGPU_HLS_RV32I_CODEC_H
#define RISCV_GPGPU_HLS_RV32I_CODEC_H

#include <ap_int.h>
#include "../common/hls_types.h"

namespace riscv_gpgpu_hls {

// ── Major opcodes (real RV32I values for the standard group; custom-0/1 are
//    the RISC-V spec's reserved-for-nonstandard-extensions allocations) ─────
namespace rv32i_opcode {
constexpr uint8_t OP        = 0x33;  // ADD/SUB/AND/OR/XOR/SLT
constexpr uint8_t OP_IMM    = 0x13;  // ADDI
constexpr uint8_t LUI_OP    = 0x37;
constexpr uint8_t LOAD_OP   = 0x03;  // LW
constexpr uint8_t STORE_OP  = 0x23;  // SW
constexpr uint8_t BRANCH_OP = 0x63;  // BEQ/BNE
constexpr uint8_t JAL_OP    = 0x6F;
constexpr uint8_t JALR_OP   = 0x67;
constexpr uint8_t CUSTOM0   = 0x0B;  // vector-lane + scalar-float ops (SS13.5)
constexpr uint8_t CUSTOM1   = 0x2B;  // VBRANCH/VJOIN/BARRIER/HALT (SS13.6)
constexpr uint8_t OP_FP     = 0x53;  // RV32F standard: FADD.S/FMUL.S (parity §19)
}  // namespace rv32i_opcode

// ── encodeInstruction (SS13.8) ───────────────────────────────────────────────
// Parameter order/defaults deliberately match makeInstr() (types.h:104) so
// call sites convert by renaming the function, not reordering arguments.
inline raw_instr_t encodeInstruction(Opcode op, uint8_t rd = 0, uint8_t rs1 = 0,
                                      uint8_t rs2 = 0, int32_t imm = 0) {
    using namespace rv32i_opcode;
    raw_instr_t w = 0;
    ap_uint<5> rd5  = rd;
    ap_uint<5> rs15 = rs1;
    ap_uint<5> rs25 = rs2;

    auto packR = [&](uint8_t opcode, ap_uint<3> f3, ap_uint<7> f7) {
        w.range(31, 25) = f7;
        w.range(24, 20) = rs25;
        w.range(19, 15) = rs15;
        w.range(14, 12) = f3;
        w.range(11, 7)  = rd5;
        w.range(6, 0)   = ap_uint<7>(opcode);
    };
    auto packI = [&](uint8_t opcode, ap_uint<3> f3) {
        w.range(31, 20) = ap_int<12>(imm);
        w.range(19, 15) = rs15;
        w.range(14, 12) = f3;
        w.range(11, 7)  = rd5;
        w.range(6, 0)   = ap_uint<7>(opcode);
    };
    auto packS = [&](uint8_t opcode, ap_uint<3> f3) {
        ap_int<12> simm = imm;
        w.range(31, 25) = simm.range(11, 5);
        w.range(24, 20) = rs25;
        w.range(19, 15) = rs15;
        w.range(14, 12) = f3;
        w.range(11, 7)  = simm.range(4, 0);
        w.range(6, 0)   = ap_uint<7>(opcode);
    };
    auto packB = [&](uint8_t opcode, ap_uint<3> f3) {
        ap_int<13> bimm = imm;   // caller passes the real (even) branch offset
        w[31]          = bimm[12];
        w.range(30, 25) = bimm.range(10, 5);
        w.range(24, 20) = rs25;
        w.range(19, 15) = rs15;
        w.range(14, 12) = f3;
        w.range(11, 8)  = bimm.range(4, 1);
        w[7]            = bimm[11];
        w.range(6, 0)   = ap_uint<7>(opcode);
    };
    auto packU = [&](uint8_t opcode) {
        // SS13.6 note: imm here is the raw, unshifted 20-bit value - golden
        // executeALU's `case LUI: regs[t][instr.rd] = b << 12;` does the
        // shift itself, so this encodes exactly what decode must hand back.
        w.range(31, 12) = ap_uint<20>(imm);
        w.range(11, 7)  = rd5;
        w.range(6, 0)   = ap_uint<7>(opcode);
    };
    auto packJ = [&](uint8_t opcode) {
        ap_int<21> jimm = imm;   // caller passes the real (even) jump offset
        w[31]           = jimm[20];
        w.range(30, 21) = jimm.range(10, 1);
        w[20]           = jimm[11];
        w.range(19, 12) = jimm.range(19, 12);
        w.range(11, 7)  = rd5;
        w.range(6, 0)   = ap_uint<7>(opcode);
    };

    switch (op) {
        case Opcode::ADD:  packR(OP, 0b000, 0b0000000); break;
        case Opcode::SUB:  packR(OP, 0b000, 0b0100000); break;
        case Opcode::AND:  packR(OP, 0b111, 0b0000000); break;
        case Opcode::OR:   packR(OP, 0b110, 0b0000000); break;
        case Opcode::XOR:  packR(OP, 0b100, 0b0000000); break;
        case Opcode::SLT:  packR(OP, 0b010, 0b0000000); break;

        case Opcode::ADDI: packI(OP_IMM, 0b000); break;
        case Opcode::LUI:  packU(LUI_OP);        break;
        case Opcode::LW:   packI(LOAD_OP, 0b010); break;
        case Opcode::SW:   packS(STORE_OP, 0b010); break;
        case Opcode::BEQ:  packB(BRANCH_OP, 0b000); break;
        case Opcode::BNE:  packB(BRANCH_OP, 0b001); break;
        case Opcode::JAL:  packJ(JAL_OP);         break;
        case Opcode::JALR: packI(JALR_OP, 0b000); break;

        // custom-0 group 0 (funct7=0000000): vector-lane ops (SS13.5)
        case Opcode::VADD:    packR(CUSTOM0, 0b000, 0b0000000); break;
        case Opcode::VSUB:    packR(CUSTOM0, 0b001, 0b0000000); break;
        case Opcode::VMUL:    packR(CUSTOM0, 0b010, 0b0000000); break;
        case Opcode::VFMADD:  packR(CUSTOM0, 0b011, 0b0000000); break;
        case Opcode::VFADD:   packR(CUSTOM0, 0b100, 0b0000000); break;
        case Opcode::VFSUB:   packR(CUSTOM0, 0b101, 0b0000000); break;
        case Opcode::VFMUL:   packR(CUSTOM0, 0b110, 0b0000000); break;
        case Opcode::VFFMADD: packR(CUSTOM0, 0b111, 0b0000000); break;
        // custom-0 group 1 (funct7=0000001): scalar-float ops (SS13.5)
        case Opcode::FADD: packR(CUSTOM0, 0b000, 0b0000001); break;
        case Opcode::FMUL: packR(CUSTOM0, 0b001, 0b0000001); break;

        // custom-1: control ops (SS13.6)
        case Opcode::VBRANCH: packR(CUSTOM1, 0b000, 0b0000000); break;
        case Opcode::VJOIN:   packR(CUSTOM1, 0b001, 0b0000000); break;
        case Opcode::BARRIER: packI(CUSTOM1, 0b010);            break;
        case Opcode::HALT:    packR(CUSTOM1, 0b011, 0b0000000); break;

        default: break;   // no encoding for opcode space not in SS13.4/13.5/13.6
    }
    return w;
}

// ── encodeInstructionExpanded (SS13.12) ──────────────────────────────────────
// Handles the one case where a single golden Instruction doesn't fit in one
// real RV32I word: ADDI used as a "load 32-bit immediate" idiom (rs1==0,
// |imm| beyond the 12-bit signed range - e.g. kernel_programs.h's
// fpUniformSaxpy() loading a float's raw bit pattern via ADDI). Real RV32I
// has no single instruction for this - the standard toolchain answer is the
// `li rd, imm32` pseudo-op, which expands to LUI+ADDI. Every other
// Opcode/imm combination still emits exactly one word via encodeInstruction()
// above - this function subsumes it, so callers building a program[] array
// should call this instead, tracking a separately-growing output index
// (SS13.12's loadProgram() example).
//
// Returns the number of words written to out[] (1 normally, 2 for the
// expansion case). out[] must have room for 2 elements.
inline int encodeInstructionExpanded(Opcode op, uint8_t rd, uint8_t rs1,
                                      uint8_t rs2, int32_t imm,
                                      raw_instr_t out[2]) {
    bool needsExpansion = (op == Opcode::ADDI) && (rs1 == 0) &&
                           (imm < -2048 || imm > 2047);
    if (!needsExpansion) {
        out[0] = encodeInstruction(op, rd, rs1, rs2, imm);
        return 1;
    }
    // Standard li-pseudo-op split (same formula real assemblers use): round
    // toward the nearest LUI value so the ADDI's sign-extended low 12 bits
    // combine back to the exact original imm regardless of bit 11.
    int32_t hi20 = (imm + 0x800) >> 12;
    int32_t lo12 = imm - (hi20 << 12);
    out[0] = encodeInstruction(Opcode::LUI,  rd, 0,  0,   hi20);
    out[1] = encodeInstruction(Opcode::ADDI, rd, rd, 0,   lo12);
    return 2;
}

// ── decodeInstruction (SS13.8) ───────────────────────────────────────────────
inline Instruction decodeInstruction(raw_instr_t word) {
    using namespace rv32i_opcode;
    Instruction instr;

    uint8_t    opc = word.range(6, 0).to_uint();
    uint8_t    f3  = word.range(14, 12).to_uint();
    uint8_t    f7  = word.range(31, 25).to_uint();
    ap_uint<8> rd5  = word.range(11, 7);
    ap_uint<8> rs15 = word.range(19, 15);
    ap_uint<8> rs25 = word.range(24, 20);

    // Immediates - decoded once regardless of format, unused ones simply
    // aren't assigned to instr.imm below. Sign-extension for I/S/B/J comes
    // from the ap_int<N>-to-int32_t widening conversion (bit-reinterpret,
    // same technique the S-type imm's pack/unpack mirrors).
    int32_t iimm = ap_int<12>(word.range(31, 20));

    ap_uint<12> simm_bits = 0;
    simm_bits.range(11, 5) = word.range(31, 25);
    simm_bits.range(4, 0)  = word.range(11, 7);
    int32_t simm = ap_int<12>(simm_bits);

    ap_uint<13> bimm_bits = 0;
    bimm_bits[12]          = word[31];
    bimm_bits.range(10, 5) = word.range(30, 25);
    bimm_bits.range(4, 1)  = word.range(11, 8);
    bimm_bits[11]           = word[7];
    int32_t bimm = ap_int<13>(bimm_bits);

    int32_t uimm = ap_uint<20>(word.range(31, 12)).to_uint();  // raw, unshifted (see packU note)

    ap_uint<21> jimm_bits = 0;
    jimm_bits[20]           = word[31];
    jimm_bits.range(10, 1)  = word.range(30, 21);
    jimm_bits[11]            = word[20];
    jimm_bits.range(19, 12) = word.range(19, 12);
    int32_t jimm = ap_int<21>(jimm_bits);

    switch (opc) {
        case OP: {
            instr.rs1 = rs15; instr.rs2 = rs25; instr.rd = rd5; instr.imm = 0;
            if      (f3 == 0b000) instr.opcode = (f7 == 0b0100000) ? Opcode::SUB : Opcode::ADD;
            else if (f3 == 0b111) instr.opcode = Opcode::AND;
            else if (f3 == 0b110) instr.opcode = Opcode::OR;
            else if (f3 == 0b100) instr.opcode = Opcode::XOR;
            else                  instr.opcode = Opcode::SLT;   // f3==0b010
            break;
        }
        case OP_IMM: {
            instr.opcode = Opcode::ADDI;
            instr.rs1 = rs15; instr.rd = rd5; instr.rs2 = 0; instr.imm = iimm;
            break;
        }
        case LUI_OP: {
            instr.opcode = Opcode::LUI;
            instr.rd = rd5; instr.rs1 = 0; instr.rs2 = 0; instr.imm = uimm;
            break;
        }
        case LOAD_OP: {
            instr.opcode = Opcode::LW;
            instr.rs1 = rs15; instr.rd = rd5; instr.rs2 = 0; instr.imm = iimm;
            break;
        }
        case STORE_OP: {
            instr.opcode = Opcode::SW;
            instr.rs1 = rs15; instr.rs2 = rs25; instr.rd = 0; instr.imm = simm;
            break;
        }
        case BRANCH_OP: {
            instr.opcode = (f3 == 0b000) ? Opcode::BEQ : Opcode::BNE;
            instr.rs1 = rs15; instr.rs2 = rs25; instr.rd = 0; instr.imm = bimm;
            break;
        }
        case JAL_OP: {
            instr.opcode = Opcode::JAL;
            instr.rd = rd5; instr.rs1 = 0; instr.rs2 = 0; instr.imm = jimm;
            break;
        }
        case JALR_OP: {
            instr.opcode = Opcode::JALR;
            instr.rd = rd5; instr.rs1 = rs15; instr.rs2 = 0; instr.imm = iimm;
            break;
        }
        case OP_FP: {
            // RV32F standard encoding; map to Virtual-ISA FP ops (§19).
            instr.rs1 = rs15; instr.rs2 = rs25; instr.rd = rd5; instr.imm = 0;
            if      (f7 == 0b0000000) instr.opcode = Opcode::FADD;   // FADD.S
            else if (f7 == 0b0001000) instr.opcode = Opcode::FMUL;   // FMUL.S
            else                       instr.opcode = Opcode::HALT;   // FDIV/FSQRT/etc. unsupported
            break;
        }
        case CUSTOM0: {
            instr.rs1 = rs15; instr.rs2 = rs25; instr.rd = rd5; instr.imm = 0;
            if (f7 == 0b0000000) {
                switch (f3) {
                    case 0: instr.opcode = Opcode::VADD;    break;
                    case 1: instr.opcode = Opcode::VSUB;    break;
                    case 2: instr.opcode = Opcode::VMUL;    break;
                    case 3: instr.opcode = Opcode::VFMADD;  break;
                    case 4: instr.opcode = Opcode::VFADD;   break;
                    case 5: instr.opcode = Opcode::VFSUB;   break;
                    case 6: instr.opcode = Opcode::VFMUL;   break;
                    default: instr.opcode = Opcode::VFFMADD; break;  // 7
                }
            } else {   // f7==0b0000001 group (SS13.5)
                instr.opcode = (f3 == 0) ? Opcode::FADD : Opcode::FMUL;
            }
            break;
        }
        case CUSTOM1: {
            switch (f3) {
                case 0:
                    instr.opcode = Opcode::VBRANCH;
                    instr.rs1 = rs15; instr.rs2 = rs25; instr.rd = rd5; instr.imm = 0;
                    break;
                case 1:
                    instr.opcode = Opcode::VJOIN;
                    instr.rs1 = 0; instr.rs2 = 0; instr.rd = 0; instr.imm = 0;
                    break;
                case 2:
                    instr.opcode = Opcode::BARRIER;
                    instr.rs1 = rs15; instr.rd = rd5; instr.rs2 = 0; instr.imm = iimm;
                    break;
                default:   // 3 == HALT
                    instr.opcode = Opcode::HALT;
                    instr.rs1 = 0; instr.rs2 = 0; instr.rd = 0; instr.imm = 0;
                    break;
            }
            break;
        }
        default: {
            // Unrecognized opcode bits - no valid program should ever produce
            // these (every encodeInstruction() call site goes through the
            // table above), so HALT is a safe, inert fallback rather than
            // undefined behavior.
            instr.opcode = Opcode::HALT;
            instr.rs1 = 0; instr.rs2 = 0; instr.rd = 0; instr.imm = 0;
            break;
        }
    }

    // is_vector/is_memory/is_branch: reuse the existing opcode-value-range
    // helpers (hls_types.h) instead of re-deriving this logic - identical to
    // what makeInstr() computes today (types.h:112-114).
    instr.is_vector = isVectorOp(instr.opcode);
    instr.is_memory = isMemoryOp(instr.opcode);
    instr.is_branch = isBranchOp(instr.opcode);
    instr.pc = 0;   // not carried in the raw word - never read downstream
                     // (compute_pipeline.cpp uses the program[] loop index as
                     // PC, not instr.pc; grep-confirmed before this file was
                     // written)
    return instr;
}

}  // namespace riscv_gpgpu_hls

#endif  // RISCV_GPGPU_HLS_RV32I_CODEC_H
