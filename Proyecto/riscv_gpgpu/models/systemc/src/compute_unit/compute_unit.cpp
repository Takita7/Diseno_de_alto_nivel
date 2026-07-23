// compute_unit.cpp – Phase 8: floating-point execution
//

#include "compute_unit.h"
#include "../memory/memory_hierarchy.h"
#include "../common/logging.h"
#include <sstream>
#include <algorithm>

namespace riscv_gpgpu {

// ── Constructor / destructor ──────────────────────────────────────────────────

ComputeUnit::ComputeUnit(sc_core::sc_module_name name, const Config& config)
    : sc_core::sc_module(name), config_(config), unit_id_(config.unit_id)
{
    warp_states_.resize(config_.max_warps, WarpState::IDLE);
    registers_.resize(config_.max_warps,
                      std::vector<uint32_t>(config_.threads_per_warp * 32, 0));
    shared_memory_.resize(config_.shared_mem_size, 0);

    SIMTController::Config simt_cfg;
    simt_ctrl_ = std::make_unique<SIMTController>("simt_ctrl", simt_cfg);
    simt_ctrl_->clk(clk);

    SC_METHOD(clockProcess);
    sensitive << clk.pos();

    LOG_INFO("ComputeUnit " + std::to_string(unit_id_) + " initialized");
}

ComputeUnit::~ComputeUnit() {
    LOG_INFO("ComputeUnit " + std::to_string(unit_id_) + " destroyed");
}

// ── Phase 6: external memory ──────────────────────────────────────────────────

void ComputeUnit::setMemory(MemoryHierarchy* mem) {
    ext_memory_ = mem;
    LOG_INFO("ComputeUnit " + std::to_string(unit_id_)
             + ": external memory hierarchy connected");
}

uint32_t ComputeUnit::getDivergenceEvents() const {
    return simt_ctrl_ ? simt_ctrl_->getTotalDivergenceEvents() : 0;
}

// ── executeWarp ───────────────────────────────────────────────────────────────

void ComputeUnit::executeWarp(WarpContext& ctx) {
    if (ctx.regs.size() < config_.threads_per_warp)
        ctx.regs.resize(config_.threads_per_warp, std::vector<uint32_t>(32, 0));
    for (auto& t_regs : ctx.regs)
        if (t_regs.size() < 32) t_regs.resize(32, 0);

    simt_ctrl_->initializeWarp(ctx.warp_id, config_.threads_per_warp);
    ctx.state = WarpState::RUNNING;
    ctx.pc    = 0;

    while (ctx.pc < static_cast<uint32_t>(ctx.program.size())) {
        const Instruction& instr = ctx.program[ctx.pc];
        uint32_t mask            = simt_ctrl_->getActiveMask(ctx.warp_id);
        auto     op              = static_cast<Opcode>(instr.opcode);

        if (op == Opcode::HALT) {
            ++total_instructions_;
            break;
        }

        if      (op == Opcode::VBRANCH)  { executeBranch(ctx, instr, mask); }
        else if (op == Opcode::VJOIN)    { executeJoin  (ctx, instr, mask); }
        else if (instr.is_memory)        { executeMemOp (ctx, instr, mask); }
        else if (instr.is_vector)        { executeVector(ctx, instr, mask); }
        else                             { executeALU   (ctx, instr, mask); }

        ++total_instructions_;
        ++ctx.pc;
    }

    ctx.state = WarpState::COMPLETE;
    LOG_DEBUG("ComputeUnit " + std::to_string(unit_id_)
              + ": warp " + std::to_string(ctx.warp_id) + " complete");
}

// ── Scalar ALU (integer + FP) ─────────────────────────────────────────────────

void ComputeUnit::executeALU(WarpContext& ctx,
                               const Instruction& instr,
                               uint32_t mask) {
    auto op = static_cast<Opcode>(instr.opcode);
    for (uint32_t t = 0; t < config_.threads_per_warp; ++t) {
        if (!((mask >> t) & 1u)) continue;

        uint32_t a = ctx.regs[t][instr.rs1];
        uint32_t b = (op == Opcode::ADDI || op == Opcode::LUI)
                     ? static_cast<uint32_t>(instr.imm)
                     : ctx.regs[t][instr.rs2];

        switch (op) {
            // ── Integer ───────────────────────────────────────────────────────
            case Opcode::ADD:
            case Opcode::ADDI: ctx.regs[t][instr.rd] = a + b;                                break;
            case Opcode::SUB:  ctx.regs[t][instr.rd] = a - b;                                break;
            case Opcode::AND:  ctx.regs[t][instr.rd] = a & b;                                break;
            case Opcode::OR:   ctx.regs[t][instr.rd] = a | b;                                break;
            case Opcode::XOR:  ctx.regs[t][instr.rd] = a ^ b;                                break;
            case Opcode::SLT:  ctx.regs[t][instr.rd] = (int32_t(a) < int32_t(b)) ? 1u : 0u; break;
            case Opcode::LUI:  ctx.regs[t][instr.rd] = b << 12;                              break;
            // ── Scalar FP (Phase 8) ───────────────────────────────────────────
            case Opcode::FADD:
                ctx.regs[t][instr.rd] = floatAsReg(regAsFloat(a) + regAsFloat(b)); break;
            case Opcode::FMUL:
                ctx.regs[t][instr.rd] = floatAsReg(regAsFloat(a) * regAsFloat(b)); break;
            default: break;
        }
    }
}

// ── Vector (integer + FP) ─────────────────────────────────────────────────────

void ComputeUnit::executeVector(WarpContext& ctx,
                                  const Instruction& instr,
                                  uint32_t mask) {
    auto op = static_cast<Opcode>(instr.opcode);
    for (uint32_t t = 0; t < config_.threads_per_warp; ++t) {
        if (!((mask >> t) & 1u)) continue;

        uint32_t a = ctx.regs[t][instr.rs1];
        uint32_t b = ctx.regs[t][instr.rs2];
        uint32_t c = ctx.regs[t][instr.rd];   // accumulator

        switch (op) {
            // ── Integer vector ─────────────────────────────────────────────────
            case Opcode::VADD:   ctx.regs[t][instr.rd] = a + b;     break;
            case Opcode::VSUB:   ctx.regs[t][instr.rd] = a - b;     break;
            case Opcode::VMUL:   ctx.regs[t][instr.rd] = a * b;     break;
            case Opcode::VFMADD: ctx.regs[t][instr.rd] = a * b + c; break;
            // ── FP vector (Phase 8) ───────────────────────────────────────────
            case Opcode::VFADD:
                ctx.regs[t][instr.rd] = floatAsReg(regAsFloat(a) + regAsFloat(b)); break;
            case Opcode::VFSUB:
                ctx.regs[t][instr.rd] = floatAsReg(regAsFloat(a) - regAsFloat(b)); break;
            case Opcode::VFMUL:
                ctx.regs[t][instr.rd] = floatAsReg(regAsFloat(a) * regAsFloat(b)); break;
            case Opcode::VFFMADD:
                ctx.regs[t][instr.rd] = floatAsReg(
                    regAsFloat(a) * regAsFloat(b) + regAsFloat(c));                break;
            default: break;
        }
    }
}

// ── Memory ────────────────────────────────────────────────────────────────────

void ComputeUnit::executeMemOp(WarpContext& ctx,
                                 const Instruction& instr,
                                 uint32_t mask) {
    auto op = static_cast<Opcode>(instr.opcode);
    for (uint32_t t = 0; t < config_.threads_per_warp; ++t) {
        if (!((mask >> t) & 1u)) continue;

        Address addr = static_cast<Address>(
            static_cast<int64_t>(ctx.regs[t][instr.rs1]) + instr.imm);

        if (op == Opcode::LW) {
            if (ext_memory_) {
                uint32_t lat = 0;
                ext_memory_->loadWord(addr, ctx.regs[t][instr.rd], lat);
            } else {
                auto it = sim_memory_.find(addr);
                ctx.regs[t][instr.rd] = (it != sim_memory_.end()) ? it->second : 0u;
            }
        } else {
            if (ext_memory_) {
                uint32_t lat = 0;
                ext_memory_->storeWord(addr, ctx.regs[t][instr.rs2], lat);
            } else {
                sim_memory_[addr] = ctx.regs[t][instr.rs2];
            }
        }
    }
}

// ── SIMT branch / join ────────────────────────────────────────────────────────

void ComputeUnit::executeBranch(WarpContext& ctx,
                                  const Instruction& instr,
                                  uint32_t mask) {
    bool conditions[32] = {};
    uint32_t tpw = config_.threads_per_warp;
    for (uint32_t t = 0; t < tpw; ++t) {
        if ((mask >> t) & 1u)
            conditions[t] = (ctx.regs[t][instr.rs1] == 0);
    }
    simt_ctrl_->handleBranch(ctx.warp_id, conditions);
}

void ComputeUnit::executeJoin(WarpContext& ctx,
                                const Instruction& /*instr*/,
                                uint32_t /*mask*/) {
    simt_ctrl_->handleJoin(ctx.warp_id);
}

// ── Legacy clock-driven path ──────────────────────────────────────────────────

void ComputeUnit::launchKernel(BlockID block_id, uint32_t grid_x, uint32_t grid_y) {
    std::stringstream ss;
    ss << "ComputeUnit " << unit_id_ << ": launchKernel block=" << block_id
       << " grid=" << grid_x << "x" << grid_y;
    LOG_INFO(ss.str());
    is_running_ = true;
    initializeWarp(0);
    ready_warps_.push(0);
}

WarpState ComputeUnit::getWarpState(WarpID warp_id) const {
    if (warp_id < warp_states_.size()) return warp_states_[warp_id];
    return WarpState::IDLE;
}

void ComputeUnit::step() {
    if (!is_running_) return;
    if (!ready_warps_.empty()) {
        current_executing_warp_ = ready_warps_.front();
        ready_warps_.pop();
        executeInstruction(current_executing_warp_);
        ready_warps_.push(current_executing_warp_);
    }
    ++total_cycles_;
    updateWarpState();
}

bool ComputeUnit::isComplete() const {
    for (const auto& state : warp_states_)
        if (state != WarpState::IDLE && state != WarpState::COMPLETE) return false;
    return true;
}

void ComputeUnit::clockProcess()   { if (is_running_) step(); }
void ComputeUnit::executeProcess() {}

void ComputeUnit::initializeWarp(WarpID warp_id) {
    warp_states_[warp_id] = WarpState::READY;
    std::fill(registers_[warp_id].begin(), registers_[warp_id].end(), 0);
}

void ComputeUnit::finalizeWarp(WarpID warp_id) {
    warp_states_[warp_id] = WarpState::COMPLETE;
    LOG_INFO("ComputeUnit " + std::to_string(unit_id_)
             + ": warp " + std::to_string(warp_id) + " finalized");
}

void ComputeUnit::scheduleWarp() {
    for (size_t i = 0; i < warp_states_.size(); ++i) {
        if (warp_states_[i] == WarpState::READY) {
            current_executing_warp_ = static_cast<WarpID>(i);
            return;
        }
    }
}

void ComputeUnit::executeInstruction(WarpID warp_id) {
    if (warp_states_[warp_id] != WarpState::RUNNING &&
        warp_states_[warp_id] != WarpState::READY) return;
    warp_states_[warp_id] = WarpState::RUNNING;
    ++total_instructions_;
}

bool ComputeUnit::checkMemoryDependencies(WarpID /*warp_id*/) { return true; }

void ComputeUnit::updateWarpState() {
    for (size_t i = 0; i < warp_states_.size(); ++i)
        if (warp_states_[i] == WarpState::RUNNING && total_cycles_ > 100)
            finalizeWarp(static_cast<WarpID>(i));
}

}  // namespace riscv_gpgpu