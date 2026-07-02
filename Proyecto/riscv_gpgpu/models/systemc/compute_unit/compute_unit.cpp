// compute_unit.cpp - Compute unit with real RV32I(+C) instruction execution
//
// Functional model: fetches, decodes, and executes RV32I instructions
// from the MemoryHierarchy connected via setMemoryHierarchy().
//
#include "compute_unit.h"
#include "../integration/riscv_isa.h"
#include "../common/logging.h"
#include <sstream>
#include <climits>

namespace riscv_gpgpu {

ComputeUnit::ComputeUnit(sc_core::sc_module_name name, const Config& config)
    : sc_core::sc_module(name), config_(config) {

    warps_.resize(config.max_warps);

    SC_METHOD(clockProcess);
    sensitive << clk.pos();

    SC_METHOD(resetProcess);
    sensitive << reset.neg();
}

ComputeUnit::~ComputeUnit() = default;

void ComputeUnit::setEntryPoint(uint32_t pc) { warps_[0].pc = pc; }

void ComputeUnit::setInitialRegisters(const std::array<uint32_t, 32>& regs) {
    warps_[0].rf = regs;
    warps_[0].rf[0] = 0;
}

void ComputeUnit::launchKernel(BlockID /*block_id*/, uint32_t /*grid_x*/, uint32_t /*grid_y*/) {
    warps_[0].state  = WarpState::READY;
    warps_[0].halted = false;
    is_running_ = true;
}

WarpState ComputeUnit::getWarpState(WarpID warp_id) const {
    if (warp_id < warps_.size()) return warps_[warp_id].state;
    return WarpState::IDLE;
}

uint32_t ComputeUnit::getRegister(WarpID wid, uint8_t reg) const {
    if (wid < warps_.size()) return warps_[wid].rf[reg & 0x1F];
    return 0;
}

bool ComputeUnit::isComplete() const {
    if (!is_running_) return true;
    for (const auto& w : warps_)
        if (w.state == WarpState::RUNNING || w.state == WarpState::READY)
            return false;
    return true;
}

void ComputeUnit::step() {
    if (!is_running_) return;
    total_cycles_++;

    if (total_cycles_ > config_.max_cycles) {
        LOG_INFO("ComputeUnit: max_cycles reached, forcing halt");
        for (auto& w : warps_) w.halted = true;
        is_running_ = false;
        return;
    }

    for (uint32_t i = 0; i < warps_.size(); ++i) {
        auto& w = warps_[i];
        if (w.state == WarpState::READY || w.state == WarpState::RUNNING) {
            w.state = WarpState::RUNNING;
            executeWarp(static_cast<WarpID>(i));
            break;
        }
    }
    if (isComplete()) is_running_ = false;
}

void ComputeUnit::executeWarp(WarpID warp_id) {
    if (!mem_) return;
    WarpContext& ctx = warps_[warp_id];
    if (ctx.halted) { ctx.state = WarpState::COMPLETED; return; }

    if (ctx.pc == return_sentinel_) {
        ctx.halted = true;
        ctx.state  = WarpState::COMPLETED;
        return;
    }

    // Fetch (check for RVC)
    uint32_t raw16 = mem_->fetchInstruction(ctx.pc) & 0xFFFF;
    uint32_t raw32;
    uint32_t instr_size;

    if ((raw16 & 0x3) != 0x3) {
        uint32_t expanded = expandRVC(static_cast<uint16_t>(raw16), ctx.pc);
        raw32      = (expanded != 0) ? expanded : 0x00000013u; // NOP
        instr_size = 2;
    } else {
        raw32      = mem_->fetchInstruction(ctx.pc);
        instr_size = 4;
    }

    uint32_t current_pc = ctx.pc;
    ctx.pc += instr_size;

    decodeAndExecute(ctx, raw32, current_pc);
    total_instructions_++;
}

void ComputeUnit::decodeAndExecute(WarpContext& ctx, uint32_t raw32, uint32_t current_pc) {
    using Op = RV32Instr::Op;
    RV32Instr d   = decodeRV32(raw32);
    auto&     rf  = ctx.rf;
    uint32_t rs1v = rf[d.rs1];
    uint32_t rs2v = rf[d.rs2];
    uint32_t result    = 0;
    bool update_rd     = true;

    switch (d.op) {
    // R-type
    case Op::ADD:    result = rs1v + rs2v; break;
    case Op::SUB:    result = rs1v - rs2v; break;
    case Op::SLL:    result = rs1v << (rs2v & 0x1F); break;
    case Op::SLT:    result = (int32_t)rs1v < (int32_t)rs2v ? 1u : 0u; break;
    case Op::SLTU:   result = rs1v < rs2v ? 1u : 0u; break;
    case Op::XOR:    result = rs1v ^ rs2v; break;
    case Op::SRL:    result = rs1v >> (rs2v & 0x1F); break;
    case Op::SRA:    result = (uint32_t)((int32_t)rs1v >> (rs2v & 0x1F)); break;
    case Op::OR:     result = rs1v | rs2v; break;
    case Op::AND:    result = rs1v & rs2v; break;
    // M extension
    case Op::MUL:    result = (uint32_t)((int64_t)(int32_t)rs1v * (int64_t)(int32_t)rs2v); break;
    case Op::MULH:   result = (uint32_t)(((int64_t)(int32_t)rs1v*(int64_t)(int32_t)rs2v)>>32); break;
    case Op::MULHSU: result = (uint32_t)(((int64_t)(int32_t)rs1v*(uint64_t)rs2v)>>32); break;
    case Op::MULHU:  result = (uint32_t)(((uint64_t)rs1v*(uint64_t)rs2v)>>32); break;
    case Op::DIV:
        result = ((int32_t)rs2v==0) ? ~0u :
                 ((int32_t)rs1v==INT_MIN&&(int32_t)rs2v==-1) ? (uint32_t)INT_MIN :
                 (uint32_t)((int32_t)rs1v/(int32_t)rs2v); break;
    case Op::DIVU:   result = rs2v==0 ? ~0u : rs1v/rs2v; break;
    case Op::REM:
        result = ((int32_t)rs2v==0) ? rs1v :
                 ((int32_t)rs1v==INT_MIN&&(int32_t)rs2v==-1) ? 0 :
                 (uint32_t)((int32_t)rs1v%(int32_t)rs2v); break;
    case Op::REMU:   result = rs2v==0 ? rs1v : rs1v%rs2v; break;
    // I-type ALU
    case Op::ADDI:   result = rs1v + (uint32_t)d.imm; break;
    case Op::SLTI:   result = (int32_t)rs1v < d.imm ? 1u : 0u; break;
    case Op::SLTIU:  result = rs1v < (uint32_t)d.imm ? 1u : 0u; break;
    case Op::XORI:   result = rs1v ^ (uint32_t)d.imm; break;
    case Op::ORI:    result = rs1v | (uint32_t)d.imm; break;
    case Op::ANDI:   result = rs1v & (uint32_t)d.imm; break;
    case Op::SLLI:   result = rs1v << (d.imm & 0x1F); break;
    case Op::SRLI:   result = rs1v >> (d.imm & 0x1F); break;
    case Op::SRAI:   result = (uint32_t)((int32_t)rs1v >> (d.imm & 0x1F)); break;
    // U-type
    case Op::LUI:    result = (uint32_t)d.imm; break;
    case Op::AUIPC:  result = current_pc + (uint32_t)d.imm; break;
    // Loads
    case Op::LW: {
        uint32_t addr = rs1v + (uint32_t)d.imm;
        uint32_t lat = 0;
        mem_->loadWord(addr, result, lat); break;
    }
    case Op::LH: {
        uint16_t h = 0; mem_->loadHalf(rs1v+(uint32_t)d.imm, h);
        result = (uint32_t)(int32_t)(int16_t)h; break;
    }
    case Op::LHU: {
        uint16_t h = 0; mem_->loadHalf(rs1v+(uint32_t)d.imm, h);
        result = h; break;
    }
    case Op::LB: {
        uint8_t b = 0; mem_->loadByte(rs1v+(uint32_t)d.imm, b);
        result = (uint32_t)(int32_t)(int8_t)b; break;
    }
    case Op::LBU: {
        uint8_t b = 0; mem_->loadByte(rs1v+(uint32_t)d.imm, b);
        result = b; break;
    }
    // Stores
    case Op::SW: {
        uint32_t lat = 0;
        mem_->storeWord(rs1v+(uint32_t)d.imm, rs2v, lat);
        update_rd = false; break;
    }
    case Op::SH: {
        mem_->storeHalf(rs1v+(uint32_t)d.imm, (uint16_t)(rs2v&0xFFFF));
        update_rd = false; break;
    }
    case Op::SB: {
        mem_->storeByte(rs1v+(uint32_t)d.imm, (uint8_t)(rs2v&0xFF));
        update_rd = false; break;
    }
    // Branches
    case Op::BEQ:  update_rd=false;
        if (rs1v==rs2v)                        ctx.pc=current_pc+(uint32_t)d.imm; break;
    case Op::BNE:  update_rd=false;
        if (rs1v!=rs2v)                        ctx.pc=current_pc+(uint32_t)d.imm; break;
    case Op::BLT:  update_rd=false;
        if ((int32_t)rs1v<(int32_t)rs2v)      ctx.pc=current_pc+(uint32_t)d.imm; break;
    case Op::BGE:  update_rd=false;
        if ((int32_t)rs1v>=(int32_t)rs2v)     ctx.pc=current_pc+(uint32_t)d.imm; break;
    case Op::BLTU: update_rd=false;
        if (rs1v<rs2v)                         ctx.pc=current_pc+(uint32_t)d.imm; break;
    case Op::BGEU: update_rd=false;
        if (rs1v>=rs2v)                        ctx.pc=current_pc+(uint32_t)d.imm; break;
    // Jumps
    case Op::JAL:
        result = current_pc + 4;
        ctx.pc = current_pc + (uint32_t)d.imm; break;
    case Op::JALR:
        result = current_pc + 4;
        ctx.pc = (rs1v + (uint32_t)d.imm) & ~1u;
        if (d.rd==0 && d.rs1==1 && d.imm==0) {  // RET
            ctx.halted = true;
            ctx.state  = WarpState::COMPLETED;
            return;
        }
        break;
    // System
    case Op::ECALL:
    case Op::EBREAK:
        ctx.halted = true; ctx.state = WarpState::COMPLETED; return;
    case Op::FENCE:
        update_rd = false; break;
    default:
        update_rd = false; break;
    }

    if (update_rd && d.rd != 0) rf[d.rd] = result;
}

void ComputeUnit::clockProcess() {
    while (true) { wait(); if (is_running_) step(); }
}

void ComputeUnit::resetProcess() {
    total_cycles_ = 0; total_instructions_ = 0; is_running_ = false;
    for (auto& w : warps_) w = WarpContext{};
    memory_ready.write(false);
}

} // namespace riscv_gpgpu
