// compute_unit.cpp – Phase 10: BARRIER instruction support
//
//

#include "compute_unit.h"
#include "../memory/memory_hierarchy.h"
#include "../common/logging.h"
#include "../../integration/riscv_isa.h"
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

// ── External memory + divergence stats ───────────────────────────────────────

void ComputeUnit::setMemory(MemoryHierarchy* mem) {
    ext_memory_ = mem;
    LOG_INFO("ComputeUnit " + std::to_string(unit_id_)
             + ": external memory hierarchy connected");
}

// ── Binary execution API ──────────────────────────────────────────────────────

void ComputeUnit::setMemoryHierarchy(MemoryHierarchy* mem) {
    setMemory(mem);
}

void ComputeUnit::setSIMTController(SIMTController* simt) {
    // The bridge owns its own SIMTController for divergence metrics.
    // Store the pointer for compatibility; binary mode runs single-thread,
    // so we do not use SIMT masking here.
    ext_simt_ = simt;
}

void ComputeUnit::setEntryPoint(uint32_t pc) {
    binary_pc_     = pc;
    binary_mode_   = true;
    binary_halted_ = false;
    LOG_INFO("ComputeUnit " + std::to_string(unit_id_)
             + ": entry point 0x" + [&]{ std::ostringstream s; s << std::hex << pc; return s.str(); }());
}

void ComputeUnit::setInitialRegisters(std::array<uint32_t, 32> regs) {
    binary_regs_ = regs;
    binary_regs_[0] = 0;  // x0 is always zero
}

void ComputeUnit::setReturnSentinel(uint32_t sentinel_pc) {
    binary_return_sentinel_ = sentinel_pc;
}

uint32_t ComputeUnit::getRegister(uint32_t /*warp_id*/, uint32_t reg_id) const {
    if (reg_id >= 32) return 0;
    return binary_regs_[reg_id];
}

uint32_t ComputeUnit::getCurrentPC() const {
    return binary_mode_ ? binary_pc_ : 0u;
}

uint32_t ComputeUnit::getDivergenceEvents() const {
    return simt_ctrl_ ? simt_ctrl_->getTotalDivergenceEvents() : 0;
}

// ── Barrier delegation (Phase 10) ─────────────────────────────────────────────

bool ComputeUnit::allWarpsAtBarrier(uint32_t barrier_id,
                                     uint32_t total_warps) const {
    return simt_ctrl_->allWarpsAtBarrier(barrier_id, total_warps);
}

void ComputeUnit::clearBarrier(uint32_t barrier_id) {
    simt_ctrl_->clearBarrier(barrier_id);
}

// ── executeWarp ───────────────────────────────────────────────────────────────

void ComputeUnit::executeWarp(WarpContext& ctx, uint32_t* barrier_id_out) {
    if (ctx.regs.size() < config_.threads_per_warp)
        ctx.regs.resize(config_.threads_per_warp, std::vector<uint32_t>(32, 0));
    for (auto& t_regs : ctx.regs)
        if (t_regs.size() < 32) t_regs.resize(32, 0);

    // Re-initialise SIMT mask (all threads active, divergence stacks cleared).
    // This is correct for both fresh starts and barrier resumes – after a
    // barrier all threads are synchronised.
    simt_ctrl_->initializeWarp(ctx.warp_id, config_.threads_per_warp);
    ctx.state = WarpState::RUNNING;
    // ctx.pc is NOT reset here – callers control the start PC:
    //   buildWarpContext() sets pc = 0 for fresh warps.
    //   simulationProcess() passes stalled contexts with pc already advanced
    //   past the BARRIER instruction.

    while (ctx.pc < static_cast<uint32_t>(ctx.program.size())) {
        const Instruction& instr = ctx.program[ctx.pc];
        uint32_t mask            = simt_ctrl_->getActiveMask(ctx.warp_id);
        auto     op              = static_cast<Opcode>(instr.opcode);

        if (op == Opcode::HALT) {
            ++total_instructions_;
            break;
        }

        if (op == Opcode::BARRIER) {
            // Record this warp's arrival at the barrier
            uint32_t bid = static_cast<uint32_t>(instr.imm);
            simt_ctrl_->threadHitBarrier(ctx.warp_id, bid);

            if (barrier_id_out) *barrier_id_out = bid;

            // Advance past the BARRIER instruction so that when this warp is
            // resumed it continues with the next instruction.
            ++ctx.pc;
            ++total_instructions_;
            ctx.state = WarpState::STALLED;

            LOG_DEBUG("ComputeUnit " + std::to_string(unit_id_)
                      + ": warp " + std::to_string(ctx.warp_id)
                      + " stalled at barrier " + std::to_string(bid));
            return;   // yield – simulationProcess handles re-scheduling
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
                               const Instruction& instr, uint32_t mask) {
    auto op = static_cast<Opcode>(instr.opcode);
    for (uint32_t t = 0; t < config_.threads_per_warp; ++t) {
        if (!((mask >> t) & 1u)) continue;
        uint32_t a = ctx.regs[t][instr.rs1];
        uint32_t b = (op == Opcode::ADDI || op == Opcode::LUI)
                     ? static_cast<uint32_t>(instr.imm)
                     : ctx.regs[t][instr.rs2];
        switch (op) {
            case Opcode::ADD:
            case Opcode::ADDI: ctx.regs[t][instr.rd] = a + b;                                break;
            case Opcode::SUB:  ctx.regs[t][instr.rd] = a - b;                                break;
            case Opcode::AND:  ctx.regs[t][instr.rd] = a & b;                                break;
            case Opcode::OR:   ctx.regs[t][instr.rd] = a | b;                                break;
            case Opcode::XOR:  ctx.regs[t][instr.rd] = a ^ b;                                break;
            case Opcode::SLT:  ctx.regs[t][instr.rd] = (int32_t(a)<int32_t(b)) ? 1u : 0u;  break;
            case Opcode::LUI:  ctx.regs[t][instr.rd] = b << 12;                              break;
            case Opcode::FADD: ctx.regs[t][instr.rd] = floatAsReg(regAsFloat(a)+regAsFloat(b)); break;
            case Opcode::FMUL: ctx.regs[t][instr.rd] = floatAsReg(regAsFloat(a)*regAsFloat(b)); break;
            default: break;
        }
    }
}

// ── Vector (integer + FP) ─────────────────────────────────────────────────────

void ComputeUnit::executeVector(WarpContext& ctx,
                                  const Instruction& instr, uint32_t mask) {
    auto op = static_cast<Opcode>(instr.opcode);
    for (uint32_t t = 0; t < config_.threads_per_warp; ++t) {
        if (!((mask >> t) & 1u)) continue;
        uint32_t a = ctx.regs[t][instr.rs1];
        uint32_t b = ctx.regs[t][instr.rs2];
        uint32_t c = ctx.regs[t][instr.rd];
        switch (op) {
            case Opcode::VADD:    ctx.regs[t][instr.rd] = a + b;     break;
            case Opcode::VSUB:    ctx.regs[t][instr.rd] = a - b;     break;
            case Opcode::VMUL:    ctx.regs[t][instr.rd] = a * b;     break;
            case Opcode::VFMADD: ctx.regs[t][instr.rd] = a * b + c; break;
            case Opcode::VFADD:   ctx.regs[t][instr.rd] = floatAsReg(regAsFloat(a)+regAsFloat(b)); break;
            case Opcode::VFSUB:   ctx.regs[t][instr.rd] = floatAsReg(regAsFloat(a)-regAsFloat(b)); break;
            case Opcode::VFMUL:   ctx.regs[t][instr.rd] = floatAsReg(regAsFloat(a)*regAsFloat(b)); break;
            case Opcode::VFFMADD: ctx.regs[t][instr.rd] = floatAsReg(regAsFloat(a)*regAsFloat(b)+regAsFloat(c)); break;
            default: break;
        }
    }
}

// ── Memory ────────────────────────────────────────────────────────────────────

void ComputeUnit::executeMemOp(WarpContext& ctx,
                                 const Instruction& instr, uint32_t mask) {
    auto op = static_cast<Opcode>(instr.opcode);
    for (uint32_t t = 0; t < config_.threads_per_warp; ++t) {
        if (!((mask >> t) & 1u)) continue;
        Address addr = static_cast<Address>(
            static_cast<int64_t>(ctx.regs[t][instr.rs1]) + instr.imm);
        if (op == Opcode::LW) {
            if (ext_memory_) { uint32_t lat=0; ext_memory_->loadWord(addr,ctx.regs[t][instr.rd],lat); }
            else { auto it=sim_memory_.find(addr); ctx.regs[t][instr.rd]=(it!=sim_memory_.end())?it->second:0u; }
        } else {
            if (ext_memory_) { uint32_t lat=0; ext_memory_->storeWord(addr,ctx.regs[t][instr.rs2],lat); }
            else { sim_memory_[addr]=ctx.regs[t][instr.rs2]; }
        }
    }
}

// ── SIMT branch / join ────────────────────────────────────────────────────────

void ComputeUnit::executeBranch(WarpContext& ctx,
                                  const Instruction& instr, uint32_t mask) {
    bool conditions[32] = {};
    for (uint32_t t = 0; t < config_.threads_per_warp; ++t)
        if ((mask >> t) & 1u)
            conditions[t] = (ctx.regs[t][instr.rs1] == 0);
    simt_ctrl_->handleBranch(ctx.warp_id, conditions);
}

void ComputeUnit::executeJoin(WarpContext& ctx,
                                const Instruction&, uint32_t) {
    simt_ctrl_->handleJoin(ctx.warp_id);
}

// ── Legacy clock-driven path ──────────────────────────────────────────────────

void ComputeUnit::launchKernel(BlockID block_id, uint32_t grid_x, uint32_t grid_y) {
    std::stringstream ss;
    ss << "ComputeUnit " << unit_id_ << ": launchKernel block=" << block_id
       << " grid=" << grid_x << "x" << grid_y;
    LOG_INFO(ss.str());
    is_running_ = true;
    // In binary mode the entry point and registers are already set by the
    // bridge via setEntryPoint/setInitialRegisters/setReturnSentinel.
    // Do NOT overwrite binary state or push warp 0 into the legacy queue.
    if (!binary_mode_) {
        initializeWarp(0);
        ready_warps_.push(0);
    }
}

WarpState ComputeUnit::getWarpState(WarpID warp_id) const {
    if (warp_id < warp_states_.size()) return warp_states_[warp_id];
    return WarpState::IDLE;
}

// ── Binary RV32I fetch-decode-execute (file-local helper) ─────────────────────
//
// Executes one decoded RV32Instr against the binary register file.
// next_pc is passed in as (cur_pc + instr_len) and may be modified by
// control-flow instructions.  Returns true if execution should halt.

static bool executeRV32(const RV32Instr& instr, uint32_t cur_pc,
                        uint32_t& next_pc,
                        std::array<uint32_t, 32>& r,
                        uint32_t sentinel,
                        MemoryHierarchy* mem) {
    using Op = RV32Instr::Op;
    r[0] = 0;  // x0 is always 0

    switch (instr.op) {
    // ── ALU R-type ────────────────────────────────────────────────────────────
    case Op::ADD:  r[instr.rd] = r[instr.rs1] + r[instr.rs2]; break;
    case Op::SUB:  r[instr.rd] = r[instr.rs1] - r[instr.rs2]; break;
    case Op::AND:  r[instr.rd] = r[instr.rs1] & r[instr.rs2]; break;
    case Op::OR:   r[instr.rd] = r[instr.rs1] | r[instr.rs2]; break;
    case Op::XOR:  r[instr.rd] = r[instr.rs1] ^ r[instr.rs2]; break;
    case Op::SLL:  r[instr.rd] = r[instr.rs1] << (r[instr.rs2] & 0x1Fu); break;
    case Op::SRL:  r[instr.rd] = r[instr.rs1] >> (r[instr.rs2] & 0x1Fu); break;
    case Op::SRA:  r[instr.rd] = static_cast<uint32_t>(static_cast<int32_t>(r[instr.rs1]) >> (r[instr.rs2] & 0x1Fu)); break;
    case Op::SLT:  r[instr.rd] = (static_cast<int32_t>(r[instr.rs1]) < static_cast<int32_t>(r[instr.rs2])) ? 1u : 0u; break;
    case Op::SLTU: r[instr.rd] = (r[instr.rs1] < r[instr.rs2]) ? 1u : 0u; break;
    // M-extension
    case Op::MUL:  r[instr.rd] = static_cast<uint32_t>(static_cast<int64_t>(static_cast<int32_t>(r[instr.rs1])) * static_cast<int64_t>(static_cast<int32_t>(r[instr.rs2]))); break;
    case Op::MULHU: r[instr.rd] = static_cast<uint32_t>((static_cast<uint64_t>(r[instr.rs1]) * static_cast<uint64_t>(r[instr.rs2])) >> 32); break;
    case Op::DIV:  { int32_t b = static_cast<int32_t>(r[instr.rs2]); r[instr.rd] = b ? static_cast<uint32_t>(static_cast<int32_t>(r[instr.rs1]) / b) : 0xFFFFFFFFu; break; }
    case Op::DIVU: r[instr.rd] = r[instr.rs2] ? r[instr.rs1] / r[instr.rs2] : 0xFFFFFFFFu; break;
    case Op::REM:  { int32_t a = static_cast<int32_t>(r[instr.rs1]), b = static_cast<int32_t>(r[instr.rs2]); r[instr.rd] = b ? static_cast<uint32_t>(a % b) : static_cast<uint32_t>(a); break; }
    case Op::REMU: r[instr.rd] = r[instr.rs2] ? r[instr.rs1] % r[instr.rs2] : r[instr.rs1]; break;
    // ── ALU I-type ────────────────────────────────────────────────────────────
    case Op::ADDI:  r[instr.rd] = r[instr.rs1] + static_cast<uint32_t>(instr.imm); break;
    case Op::SLTI:  r[instr.rd] = (static_cast<int32_t>(r[instr.rs1]) < instr.imm) ? 1u : 0u; break;
    case Op::SLTIU: r[instr.rd] = (r[instr.rs1] < static_cast<uint32_t>(instr.imm)) ? 1u : 0u; break;
    case Op::XORI:  r[instr.rd] = r[instr.rs1] ^ static_cast<uint32_t>(instr.imm); break;
    case Op::ORI:   r[instr.rd] = r[instr.rs1] | static_cast<uint32_t>(instr.imm); break;
    case Op::ANDI:  r[instr.rd] = r[instr.rs1] & static_cast<uint32_t>(instr.imm); break;
    case Op::SLLI:  r[instr.rd] = r[instr.rs1] << (instr.imm & 0x1F); break;
    case Op::SRLI:  r[instr.rd] = r[instr.rs1] >> (instr.imm & 0x1F); break;
    case Op::SRAI:  r[instr.rd] = static_cast<uint32_t>(static_cast<int32_t>(r[instr.rs1]) >> (instr.imm & 0x1F)); break;
    // ── U-type ────────────────────────────────────────────────────────────────
    case Op::LUI:   r[instr.rd] = static_cast<uint32_t>(instr.imm); break;
    case Op::AUIPC: r[instr.rd] = cur_pc + static_cast<uint32_t>(instr.imm); break;
    // ── Load ──────────────────────────────────────────────────────────────────
    case Op::LW: {
        Address addr = static_cast<Address>(static_cast<int32_t>(r[instr.rs1]) + instr.imm);
        uint32_t val = 0, lat = 0;
        if (mem) mem->loadWord(addr, val, lat);
        r[instr.rd] = val;
        break;
    }
    case Op::LB: {
        Address addr = static_cast<Address>(static_cast<int32_t>(r[instr.rs1]) + instr.imm);
        uint8_t b = 0;
        if (mem) mem->readBytes(addr, &b, 1);
        r[instr.rd] = static_cast<uint32_t>(static_cast<int8_t>(b));
        break;
    }
    case Op::LBU: {
        Address addr = static_cast<Address>(static_cast<int32_t>(r[instr.rs1]) + instr.imm);
        uint8_t b = 0;
        if (mem) mem->readBytes(addr, &b, 1);
        r[instr.rd] = b;
        break;
    }
    case Op::LH: {
        Address addr = static_cast<Address>(static_cast<int32_t>(r[instr.rs1]) + instr.imm);
        uint8_t bytes[2] = {};
        if (mem) mem->readBytes(addr, bytes, 2);
        r[instr.rd] = static_cast<uint32_t>(static_cast<int16_t>(bytes[0] | (static_cast<uint16_t>(bytes[1]) << 8)));
        break;
    }
    case Op::LHU: {
        Address addr = static_cast<Address>(static_cast<int32_t>(r[instr.rs1]) + instr.imm);
        uint8_t bytes[2] = {};
        if (mem) mem->readBytes(addr, bytes, 2);
        r[instr.rd] = static_cast<uint32_t>(bytes[0]) | (static_cast<uint32_t>(bytes[1]) << 8);
        break;
    }
    // ── Store ─────────────────────────────────────────────────────────────────
    case Op::SW: {
        Address addr = static_cast<Address>(static_cast<int32_t>(r[instr.rs1]) + instr.imm);
        uint32_t lat = 0;
        if (mem) mem->storeWord(addr, r[instr.rs2], lat);
        break;
    }
    case Op::SB: {
        Address addr = static_cast<Address>(static_cast<int32_t>(r[instr.rs1]) + instr.imm);
        uint8_t b = static_cast<uint8_t>(r[instr.rs2]);
        if (mem) mem->writeBytes(addr, &b, 1);
        break;
    }
    case Op::SH: {
        Address addr = static_cast<Address>(static_cast<int32_t>(r[instr.rs1]) + instr.imm);
        uint8_t bytes[2] = { static_cast<uint8_t>(r[instr.rs2]), static_cast<uint8_t>(r[instr.rs2] >> 8) };
        if (mem) mem->writeBytes(addr, bytes, 2);
        break;
    }
    // ── Control flow ──────────────────────────────────────────────────────────
    case Op::JAL:
        r[instr.rd] = next_pc;
        next_pc = cur_pc + static_cast<uint32_t>(instr.imm);
        break;
    case Op::JALR: {
        uint32_t target = (r[instr.rs1] + static_cast<uint32_t>(instr.imm)) & ~1u;
        r[instr.rd] = next_pc;
        next_pc = target;
        // Detect kernel completion: JALR computes target = (ra + imm) & ~1.
        // The sentinel (0x00000001) becomes 0x00000000 after masking, so compare
        // target against the masked sentinel to correctly detect `ret` to sentinel.
        if (next_pc == (sentinel & ~1u)) { r[0] = 0; return true; }
        break;
    }
    case Op::BEQ:  if (r[instr.rs1] == r[instr.rs2]) next_pc = cur_pc + static_cast<uint32_t>(instr.imm); break;
    case Op::BNE:  if (r[instr.rs1] != r[instr.rs2]) next_pc = cur_pc + static_cast<uint32_t>(instr.imm); break;
    case Op::BLT:  if (static_cast<int32_t>(r[instr.rs1]) <  static_cast<int32_t>(r[instr.rs2])) next_pc = cur_pc + static_cast<uint32_t>(instr.imm); break;
    case Op::BGE:  if (static_cast<int32_t>(r[instr.rs1]) >= static_cast<int32_t>(r[instr.rs2])) next_pc = cur_pc + static_cast<uint32_t>(instr.imm); break;
    case Op::BLTU: if (r[instr.rs1] <  r[instr.rs2]) next_pc = cur_pc + static_cast<uint32_t>(instr.imm); break;
    case Op::BGEU: if (r[instr.rs1] >= r[instr.rs2]) next_pc = cur_pc + static_cast<uint32_t>(instr.imm); break;
    // ── System ────────────────────────────────────────────────────────────────
    case Op::ECALL:
    case Op::EBREAK:
        r[0] = 0;
        return true;  // treat as halt
    case Op::FENCE:
    case Op::UNKNOWN:
    default:
        break;
    }
    r[0] = 0;  // x0 always 0
    return false;
}

void ComputeUnit::step() {
    if (binary_mode_) {
        if (binary_halted_) return;
        // Timeout guard: maps to FPGA watchdog timer
        if (config_.max_cycles > 0 && total_cycles_ >= config_.max_cycles) {
            binary_halted_ = true;
            LOG_WARNING("ComputeUnit " + std::to_string(unit_id_)
                        + ": max_cycles reached, forcing halt");
            return;
        }
        if (!ext_memory_) { binary_halted_ = true; return; }

        // ── Fetch ─────────────────────────────────────────────────────────────
        uint32_t raw = 0, lat = 0;
        ext_memory_->loadWord(binary_pc_, raw, lat);

        // ── Decode (handle 16-bit compressed instructions) ────────────────────
        RV32Instr instr;
        uint32_t  instr_len;
        if ((raw & 0x3u) != 0x3u) {
            // RVC: expand to 32-bit equivalent
            uint32_t expanded = expandRVC(static_cast<uint16_t>(raw & 0xFFFFu), binary_pc_);
            instr     = (expanded != 0) ? decodeRV32(expanded) : RV32Instr{};
            instr_len = 2;
        } else {
            instr     = decodeRV32(raw);
            instr_len = 4;
        }

        uint32_t next_pc = binary_pc_ + instr_len;

        // ── Execute ───────────────────────────────────────────────────────────
        bool halt = executeRV32(instr, binary_pc_, next_pc,
                                binary_regs_, binary_return_sentinel_, ext_memory_);
        binary_pc_ = next_pc;
        ++total_cycles_;
        ++total_instructions_;
        if (halt) binary_halted_ = true;
        return;
    }

    // ── Legacy stub path (virtual ISA / clock-driven) ─────────────────────────
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
    if (binary_mode_) return binary_halted_;
    for (const auto& s : warp_states_)
        if (s != WarpState::IDLE && s != WarpState::COMPLETE) return false;
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
    for (size_t i = 0; i < warp_states_.size(); ++i)
        if (warp_states_[i] == WarpState::READY)
            { current_executing_warp_ = static_cast<WarpID>(i); return; }
}

void ComputeUnit::executeInstruction(WarpID warp_id) {
    if (warp_states_[warp_id] != WarpState::RUNNING &&
        warp_states_[warp_id] != WarpState::READY) return;
    warp_states_[warp_id] = WarpState::RUNNING;
    ++total_instructions_;
}

bool ComputeUnit::checkMemoryDependencies(WarpID) { return true; }

void ComputeUnit::updateWarpState() {
    for (size_t i = 0; i < warp_states_.size(); ++i)
        if (warp_states_[i] == WarpState::RUNNING && total_cycles_ > 100)
            finalizeWarp(static_cast<WarpID>(i));
}

}  // namespace riscv_gpgpu
