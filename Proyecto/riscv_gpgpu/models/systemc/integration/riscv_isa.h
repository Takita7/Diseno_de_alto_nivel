// riscv_isa.h - RV32I (+ RV32M) instruction decoder
//
// Header-only decoder. Parses a 32-bit instruction word into an
// RV32Instr descriptor. Used by the ComputeUnit fetch-decode-execute loop.
//
// Supports: RV32I base ISA + M extension (MUL/DIV/REM).
// Does NOT decode RVC (compressed) instructions — compile with -march=rv32im.
//

#ifndef RISCV_GPGPU_RISCV_ISA_H
#define RISCV_GPGPU_RISCV_ISA_H

#include <cstdint>
#include <string>

namespace riscv_gpgpu {

// ─── Decoded instruction ───────────────────────────────────────────────────────
struct RV32Instr {
    enum class Op : uint8_t {
        // R-type
        ADD, SUB, SLL, SLT, SLTU, XOR, SRL, SRA, OR, AND,
        MUL, MULH, MULHSU, MULHU, DIV, DIVU, REM, REMU,
        // I-type ALU
        ADDI, SLTI, SLTIU, XORI, ORI, ANDI, SLLI, SRLI, SRAI,
        // I-type load
        LB, LH, LW, LBU, LHU,
        // I-type control
        JALR,
        // S-type
        SB, SH, SW,
        // B-type
        BEQ, BNE, BLT, BGE, BLTU, BGEU,
        // U-type
        LUI, AUIPC,
        // J-type
        JAL,
        // System
        ECALL, EBREAK, FENCE,
        // Unknown / illegal
        UNKNOWN
    };

    Op      op   = Op::UNKNOWN;
    uint8_t rd   = 0;   // destination register
    uint8_t rs1  = 0;   // source register 1
    uint8_t rs2  = 0;   // source register 2
    int32_t imm  = 0;   // sign-extended immediate
};

// ─── Decode function ───────────────────────────────────────────────────────────
inline RV32Instr decodeRV32(uint32_t instr) {
    using Op = RV32Instr::Op;
    RV32Instr d;

    if (instr == 0) { d.op = Op::UNKNOWN; return d; }

    uint8_t opcode = instr & 0x7F;
    d.rd  = (instr >>  7) & 0x1F;
    d.rs1 = (instr >> 15) & 0x1F;
    d.rs2 = (instr >> 20) & 0x1F;
    uint8_t funct3 = (instr >> 12) & 0x7;
    uint8_t funct7 = (instr >> 25) & 0x7F;

    // Sign-extend helpers
    auto signExtend12 = [](uint32_t v) -> int32_t {
        return (v & 0x800) ? static_cast<int32_t>(v | 0xFFFFF000) : static_cast<int32_t>(v);
    };
    auto signExtend13 = [](uint32_t v) -> int32_t {
        return (v & 0x1000) ? static_cast<int32_t>(v | 0xFFFFE000) : static_cast<int32_t>(v);
    };
    auto signExtend21 = [](uint32_t v) -> int32_t {
        return (v & 0x100000) ? static_cast<int32_t>(v | 0xFFE00000) : static_cast<int32_t>(v);
    };

    switch (opcode) {
    // ── R-type ────────────────────────────────────────────────────────────────
    case 0x33:  // OP
        d.imm = 0;
        if (funct7 == 0x01) {  // M extension
            switch (funct3) {
            case 0: d.op = Op::MUL;    break;
            case 1: d.op = Op::MULH;   break;
            case 2: d.op = Op::MULHSU; break;
            case 3: d.op = Op::MULHU;  break;
            case 4: d.op = Op::DIV;    break;
            case 5: d.op = Op::DIVU;   break;
            case 6: d.op = Op::REM;    break;
            case 7: d.op = Op::REMU;   break;
            default: d.op = Op::UNKNOWN;
            }
        } else {
            switch (funct3) {
            case 0: d.op = (funct7 == 0x20) ? Op::SUB : Op::ADD; break;
            case 1: d.op = Op::SLL;  break;
            case 2: d.op = Op::SLT;  break;
            case 3: d.op = Op::SLTU; break;
            case 4: d.op = Op::XOR;  break;
            case 5: d.op = (funct7 == 0x20) ? Op::SRA : Op::SRL; break;
            case 6: d.op = Op::OR;   break;
            case 7: d.op = Op::AND;  break;
            default: d.op = Op::UNKNOWN;
            }
        }
        break;

    // ── I-type ALU ────────────────────────────────────────────────────────────
    case 0x13:  // OP-IMM
        d.imm = signExtend12((instr >> 20) & 0xFFF);
        switch (funct3) {
        case 0: d.op = Op::ADDI;  break;
        case 1: d.op = Op::SLLI;  d.imm = (instr >> 20) & 0x1F; break;
        case 2: d.op = Op::SLTI;  break;
        case 3: d.op = Op::SLTIU; break;
        case 4: d.op = Op::XORI;  break;
        case 5:
            d.imm = (instr >> 20) & 0x1F;
            d.op  = (funct7 == 0x20) ? Op::SRAI : Op::SRLI;
            break;
        case 6: d.op = Op::ORI;   break;
        case 7: d.op = Op::ANDI;  break;
        default: d.op = Op::UNKNOWN;
        }
        break;

    // ── I-type load ───────────────────────────────────────────────────────────
    case 0x03:  // LOAD
        d.imm = signExtend12((instr >> 20) & 0xFFF);
        switch (funct3) {
        case 0: d.op = Op::LB;  break;
        case 1: d.op = Op::LH;  break;
        case 2: d.op = Op::LW;  break;
        case 4: d.op = Op::LBU; break;
        case 5: d.op = Op::LHU; break;
        default: d.op = Op::UNKNOWN;
        }
        break;

    // ── I-type JALR ───────────────────────────────────────────────────────────
    case 0x67:
        d.imm = signExtend12((instr >> 20) & 0xFFF);
        d.op  = Op::JALR;
        break;

    // ── S-type ────────────────────────────────────────────────────────────────
    case 0x23:  // STORE
        d.imm = signExtend12(((instr >> 25) << 5) | ((instr >> 7) & 0x1F));
        switch (funct3) {
        case 0: d.op = Op::SB; break;
        case 1: d.op = Op::SH; break;
        case 2: d.op = Op::SW; break;
        default: d.op = Op::UNKNOWN;
        }
        break;

    // ── B-type ────────────────────────────────────────────────────────────────
    case 0x63:  // BRANCH
        d.imm = signExtend13(
            ((instr >> 31) & 1) << 12 |
            ((instr >>  7) & 1) << 11 |
            ((instr >> 25) & 0x3F) << 5 |
            ((instr >>  8) & 0xF)  << 1);
        switch (funct3) {
        case 0: d.op = Op::BEQ;  break;
        case 1: d.op = Op::BNE;  break;
        case 4: d.op = Op::BLT;  break;
        case 5: d.op = Op::BGE;  break;
        case 6: d.op = Op::BLTU; break;
        case 7: d.op = Op::BGEU; break;
        default: d.op = Op::UNKNOWN;
        }
        break;

    // ── U-type ────────────────────────────────────────────────────────────────
    case 0x37:  // LUI
        d.imm = static_cast<int32_t>(instr & 0xFFFFF000);
        d.op  = Op::LUI;
        break;

    case 0x17:  // AUIPC
        d.imm = static_cast<int32_t>(instr & 0xFFFFF000);
        d.op  = Op::AUIPC;
        break;

    // ── J-type ────────────────────────────────────────────────────────────────
    case 0x6F:  // JAL
        d.imm = signExtend21(
            ((instr >> 31) & 1) << 20 |
            ((instr >> 12) & 0xFF) << 12 |
            ((instr >> 20) & 1) << 11 |
            ((instr >> 21) & 0x3FF) << 1);
        d.op = Op::JAL;
        break;

    // ── System ────────────────────────────────────────────────────────────────
    case 0x73:
        switch (instr >> 20) {
        case 0: d.op = Op::ECALL;  break;
        case 1: d.op = Op::EBREAK; break;
        default: d.op = Op::FENCE;
        }
        break;

    case 0x0F:  // FENCE
        d.op = Op::FENCE;
        break;

    default:
        d.op = Op::UNKNOWN;
    }

    return d;
}

// ─── RVC (C extension) 16-bit decoder ─────────────────────────────────────────
// Returns a 32-bit equivalent instruction or 0 on unsupported encoding.
// Call only when instr[1:0] != 0b11.
inline uint32_t expandRVC(uint16_t cinstr, uint32_t pc) {
    // Most common compressed instructions emitted by clang for simple loops:
    uint8_t op  = cinstr & 0x3;
    uint8_t funct3 = (cinstr >> 13) & 0x7;

    auto creg = [](uint8_t r3) -> uint8_t { return 8 + (r3 & 0x7); };

    if (op == 0x1) {  // Quadrant 1
        if (funct3 == 0x0) {  // C.ADDI / C.NOP
            uint8_t rd = (cinstr >> 7) & 0x1F;
            int32_t imm = (int32_t)(((cinstr >> 12) & 1) ? 0xFFFFFFC0 : 0) |
                          ((cinstr >> 2) & 0x1F);
            if (rd == 0) return 0x00000013;  // NOP
            // ADDI rd, rd, imm
            return (static_cast<uint32_t>((imm & 0xFFF) << 20)) |
                   (static_cast<uint32_t>(rd) << 15) |
                   (static_cast<uint32_t>(rd) << 7)  | 0x13;
        }
        if (funct3 == 0x5) {  // C.J
            int32_t imm = (int32_t)(
                (((cinstr >> 12) & 1) << 11) |
                (((cinstr >> 11) & 1) << 4)  |
                (((cinstr >>  9) & 3) << 8)  |
                (((cinstr >>  8) & 1) << 10) |
                (((cinstr >>  7) & 1) << 6)  |
                (((cinstr >>  6) & 1) << 7)  |
                (((cinstr >>  3) & 7) << 1)  |
                (((cinstr >>  2) & 1) << 5));
            if (imm & 0x800) imm |= (int32_t)0xFFFFF000;
            uint32_t uimm = static_cast<uint32_t>(imm);
            return ((uimm & 0x100000) << 11) |
                   ((uimm & 0x7FE) << 20)    |
                   ((uimm & 0x800) << 9)     |
                   ((uimm & 0xFF000))        | 0x6F;  // JAL x0, imm
        }
        if (funct3 == 0x4) {  // C.LI / C.ADDI16SP / others
            uint8_t rd = (cinstr >> 7) & 0x1F;
            int32_t imm = (int32_t)(((cinstr >> 12) & 1) ? 0xFFFFFFC0 : 0) |
                          ((cinstr >> 2) & 0x1F);
            if (rd != 0) {
                // C.LI: ADDI rd, x0, imm
                return (static_cast<uint32_t>((imm & 0xFFF) << 20)) |
                       (static_cast<uint32_t>(rd) << 7) | 0x13;
            }
        }
        if (funct3 == 0x6 || funct3 == 0x7) {  // C.BEQZ / C.BNEZ
            uint8_t rs1r = creg((cinstr >> 7) & 0x7);
            int32_t imm = (int32_t)(
                (((cinstr >> 12) & 1) << 8) |
                (((cinstr >> 10) & 3) << 3) |
                (((cinstr >>  5) & 3) << 6) |
                (((cinstr >>  3) & 3) << 1) |
                (((cinstr >>  2) & 1) << 5));
            if (imm & 0x100) imm |= (int32_t)0xFFFFFE00;
            uint32_t uimm = static_cast<uint32_t>(imm);
            uint32_t f3 = (funct3 == 0x6) ? 0 : 1;  // BEQ vs BNE
            return ((uimm & 0x1000) << 19) |
                   ((uimm & 0x7E0) << 20)  |
                   (static_cast<uint32_t>(rs1r) << 15) |
                   (f3 << 12) |
                   ((uimm & 0x1E) << 7)    |
                   ((uimm & 0x800) >> 4)   | 0x63;
        }
    }
    if (op == 0x2) {  // Quadrant 2
        if (funct3 == 0x4) {  // C.MV / C.ADD / C.JR / C.JALR
            uint8_t rd  = (cinstr >> 7) & 0x1F;
            uint8_t rs2 = (cinstr >> 2) & 0x1F;
            if ((cinstr >> 12) & 1) {
                if (rs2 == 0) {
                    // C.JALR: JALR x1, rd, 0  (if rd != 0) OR C.EBREAK
                    if (rd != 0)
                        return (static_cast<uint32_t>(rd) << 15) | (1 << 7) | 0x67;
                } else {
                    // C.ADD: ADD rd, rd, rs2
                    return (static_cast<uint32_t>(rs2) << 20) |
                           (static_cast<uint32_t>(rd) << 15)  |
                           (static_cast<uint32_t>(rd) << 7)   | 0x33;
                }
            } else {
                if (rs2 == 0 && rd != 0) {
                    // C.JR: JALR x0, rd, 0
                    return (static_cast<uint32_t>(rd) << 15) | 0x67;
                } else if (rs2 != 0) {
                    // C.MV: ADD rd, x0, rs2
                    return (static_cast<uint32_t>(rs2) << 20) |
                           (static_cast<uint32_t>(rd) << 7)   | 0x33;
                }
            }
        }
        if (funct3 == 0x1 || funct3 == 0x3) {  // C.FLDSP / C.LDSP (skip FP)
        }
        if (funct3 == 0x2) {  // C.LWSP
            uint8_t rd = (cinstr >> 7) & 0x1F;
            uint32_t imm = (((cinstr >> 12) & 1) << 5) |
                           (((cinstr >>  4) & 0x7) << 2) |
                           (((cinstr >>  2) & 0x3) << 6);
            return (imm << 20) | (2 << 15) | (2 << 12) |
                   (static_cast<uint32_t>(rd) << 7) | 0x03;  // LW rd, imm(sp)
        }
        if (funct3 == 0x6) {  // C.SWSP
            uint32_t rs2v = (cinstr >> 2) & 0x1F;
            uint32_t imm = (((cinstr >> 9) & 0xF) << 2) | (((cinstr >> 7) & 0x3) << 6);
            return ((imm & 0xFE0) << 20) | (rs2v << 20) | (2 << 15) | (2 << 12) |
                   ((imm & 0x1F) << 7) | 0x23;  // SW rs2, imm(sp)
        }
    }
    if (op == 0x0) {  // Quadrant 0
        if (funct3 == 0x2) {  // C.LW
            uint8_t rs1r = creg((cinstr >>  7) & 0x7);
            uint8_t rdr  = creg((cinstr >>  2) & 0x7);
            uint32_t imm = (((cinstr >>  5) & 1) << 6) |
                           (((cinstr >> 10) & 0x7) << 3) |
                           (((cinstr >>  6) & 1) << 2);
            return (imm << 20) | (static_cast<uint32_t>(rs1r) << 15) |
                   (2 << 12) | (static_cast<uint32_t>(rdr) << 7) | 0x03;
        }
        if (funct3 == 0x6) {  // C.SW
            uint8_t rs1r = creg((cinstr >>  7) & 0x7);
            uint8_t rs2r = creg((cinstr >>  2) & 0x7);
            uint32_t imm = (((cinstr >>  5) & 1) << 6) |
                           (((cinstr >> 10) & 0x7) << 3) |
                           (((cinstr >>  6) & 1) << 2);
            return ((imm & 0xFE0) << 20) | (static_cast<uint32_t>(rs2r) << 20) |
                   (static_cast<uint32_t>(rs1r) << 15) | (2 << 12) |
                   ((imm & 0x1F) << 7) | 0x23;
        }
        if (funct3 == 0x0 && cinstr != 0) {  // C.ADDI4SPN
            uint8_t rdr = creg((cinstr >> 2) & 0x7);
            uint32_t imm = (((cinstr >>  6) & 1) << 2) |
                           (((cinstr >>  5) & 1) << 3) |
                           (((cinstr >> 11) & 0x3) << 4) |
                           (((cinstr >>  7) & 0xF) << 6);
            return (imm << 20) | (2 << 15) | (static_cast<uint32_t>(rdr) << 7) | 0x13;
        }
    }
    return 0;  // Unsupported — will be treated as NOP/UNKNOWN
}

}  // namespace riscv_gpgpu

#endif  // RISCV_GPGPU_RISCV_ISA_H
