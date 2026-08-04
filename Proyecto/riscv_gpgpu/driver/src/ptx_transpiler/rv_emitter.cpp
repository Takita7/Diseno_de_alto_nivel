// rv_emitter.cpp — PTX AST → RV32IMF assembly text

#include "rv_emitter.h"

#include <algorithm>
#include <cassert>
#include <cstdint>
#include <sstream>
#include <stdexcept>

namespace riscv_gpgpu {
namespace ptx {

// ── Register pools ─────────────────────────────────────────────────────────────

// Integer register pool (excluding a0-a7 which carry parameters).
// Caller-saved first (t), then callee-saved (s) so we don't need a prologue
// for typical kernels that don't call subroutines.
static const char* kIRegs[] = {
    "t0","t1","t2","t3","t4","t5",
    "s0","s1","s2","s3","s4","s5","s6","s7","s8","s9","s10","s11"
};
static constexpr size_t kNIRegs = sizeof(kIRegs) / sizeof(kIRegs[0]);

// Float register pool (fa then ft then fs).
static const char* kFRegs[] = {
    "fa0","fa1","fa2","fa3","fa4","fa5","fa6","fa7",
    "ft0","ft1","ft2","ft3","ft4","ft5","ft6","ft7","ft8","ft9","ft10","ft11"
};
static constexpr size_t kNFRegs = sizeof(kFRegs) / sizeof(kFRegs[0]);

// Parameter registers (integer ABI: a0-a7)
static const char* kParamRegs[] = {
    "a0","a1","a2","a3","a4","a5","a6","a7"
};

// THREAD_CTX offsets from gp (= THREAD_CTX_BASE, see kernel_bridge.cpp)
static const std::string kSpecialRegOffsets[] = {
    "0",   // %tid.x
    "4",   // %tid.y
    "8",   // %tid.z
    "12",  // %ctaid.x
    "16",  // %ctaid.y
    "20",  // %ctaid.z
    "24",  // %ntid.x
    "28",  // %ntid.y
    "32",  // %ntid.z
};

static int specialRegOffset(const std::string& name) {
    if (name == "tid.x")    return  0;
    if (name == "tid.y")    return  4;
    if (name == "tid.z")    return  8;
    if (name == "ctaid.x")  return 12;
    if (name == "ctaid.y")  return 16;
    if (name == "ctaid.z")  return 20;
    if (name == "ntid.x")   return 24;
    if (name == "ntid.y")   return 28;
    if (name == "ntid.z")   return 32;
    return -1;
}

// ── Build register maps ────────────────────────────────────────────────────────

void RvEmitter::buildRegMaps(const PtxKernel& k) {
    ireg_map_.clear();
    freg_map_.clear();
    wide_regs_.clear();
    next_ireg_ = 0;
    next_freg_ = 0;
    address_size_ = k.address_size;
    params_ = k.params;
    for (const auto& decl : k.reg_decls) {
        if (decl.type != "b64") continue;
        std::string prefix = decl.prefix;
        if (!prefix.empty() && prefix[0] == '%') prefix.erase(0, 1);
        for (uint32_t index = 0; index < decl.count; ++index)
            wide_regs_.insert(prefix + std::to_string(index));
    }

    // Pre-assign parameter registers: %rN / %rdN that are first loaded from params
    // will be mapped lazily on first ireg()/freg() call, but we scan the body to
    // find `ld.param.*` and pre-map those registers to ensure they use the ireg pool,
    // not a-regs (which we'll access directly via the param index).
    // (No pre-assignment needed; the `ireg()` call is triggered naturally.)
}

std::string RvEmitter::ireg(const std::string& name) {
    auto it = ireg_map_.find(name);
    if (it != ireg_map_.end()) return it->second;
    if (next_ireg_ >= kNIRegs) {
        error_ = "Ran out of integer registers (needed for '" + name + "')";
        return "t0";  // fallback
    }
    std::string rv = kIRegs[next_ireg_++];
    ireg_map_[name] = rv;
    return rv;
}

std::string RvEmitter::freg(const std::string& name) {
    auto it = freg_map_.find(name);
    if (it != freg_map_.end()) return it->second;
    if (next_freg_ >= kNFRegs) {
        error_ = "Ran out of float registers (needed for '" + name + "')";
        return "fa0";  // fallback
    }
    std::string rv = kFRegs[next_freg_++];
    freg_map_[name] = rv;
    return rv;
}

// ── Operand resolution ─────────────────────────────────────────────────────────

std::string RvEmitter::resolveReg(const PtxOperand& op) {
    if (op.kind != PtxOperand::Kind::Reg && op.kind != PtxOperand::Kind::SpecialReg) {
        error_ = "resolveReg called on non-register operand";
        return "t0";
    }
    return ireg(op.name);
}

std::string RvEmitter::resolveFreg(const PtxOperand& op) {
    if (op.kind != PtxOperand::Kind::Reg) {
        error_ = "resolveFreg called on non-register operand";
        return "fa0";
    }
    return freg(op.name);
}

std::string RvEmitter::resolveImm(const PtxOperand& op) {
    if (op.kind == PtxOperand::Kind::IntImm)
        return std::to_string(op.int_val);
    if (op.kind == PtxOperand::Kind::FltImm)
        return std::to_string(op.flt_val);
    return "0";
}

void RvEmitter::reject(const std::string& message) {
    if (error_.empty()) error_ = message;
}

bool RvEmitter::isWideRegister(const std::string& name) const {
    return wide_regs_.count(name) != 0;
}

bool RvEmitter::validateInstruction(const PtxInstr& instr) {
    if (!instr.pred.empty() && instr.op != "bra") {
        reject("predication is only supported for bra: " + instr.op);
        return false;
    }
    const std::string& op = instr.op;
    const bool wide_memory = op.find(".u64") != std::string::npos
                          || op.find(".s64") != std::string::npos
                          || op.find(".b64") != std::string::npos
                          || op.find(".f64") != std::string::npos;
    if ((startsWith(op, "ld.global") || startsWith(op, "st.global")
         || startsWith(op, "ld.shared") || startsWith(op, "st.shared"))
        && wide_memory) {
        reject("64-bit memory elements are not supported on RV32: " + op);
        return false;
    }
    if (startsWith(op, "mad.wide") || startsWith(op, "mul.lo.u64")
        || startsWith(op, "mul.lo.s64") || startsWith(op, "div.u64")
        || startsWith(op, "div.s64") || startsWith(op, "rem.u64")
        || startsWith(op, "rem.s64")) {
        reject("general 64-bit arithmetic is not supported on RV32: " + op);
        return false;
    }
    return true;
}

bool RvEmitter::validateKernel(const PtxKernel& kernel) {
    if (kernel.address_size != 32 && kernel.address_size != 64) {
        reject("unsupported PTX address size");
        return false;
    }
    for (const auto& instr : kernel.body)
        if (!instr.label.empty() || instr.op.empty()) continue;
        else if (!validateInstruction(instr)) return false;
    return true;
}

// ── Main emit ─────────────────────────────────────────────────────────────────

std::string RvEmitter::emit(const PtxKernel& kernel) {
    error_.clear();
    buildRegMaps(kernel);
    if (!validateKernel(kernel)) return "";

    std::ostringstream out;

    out << "    .section .text\n";
    out << "    .globl " << kernel.name << "\n";
    out << "    .type " << kernel.name << ", @function\n";
    out << kernel.name << ":\n";
    out << "    # RV32IMF kernel generated from PTX by RvEmitter\n";
    out << "    # gp = THREAD_CTX_BASE (set by KernelBridge before entry)\n";

    for (const auto& instr : kernel.body) {
        // Label definition
        if (!instr.label.empty()) {
            out << instr.label << ":\n";
            continue;
        }
        if (instr.op.empty()) continue;
        emitInstr(instr, out);
        if (!error_.empty()) return "";
    }

    out << "    .size " << kernel.name << ", . - " << kernel.name << "\n";
    return error_.empty() ? out.str() : "";
}

// ── Instruction dispatch ──────────────────────────────────────────────────────

void RvEmitter::emitInstr(const PtxInstr& instr, std::ostringstream& out) {
    const std::string& op = instr.op;

    if (startsWith(op, "ld.param"))   { emitLdParam(instr, out);  return; }
    if (startsWith(op, "ld.global"))  { emitLdGlobal(instr, out); return; }
    if (startsWith(op, "st.global"))  { emitStGlobal(instr, out); return; }
    if (startsWith(op, "st.shared"))  { emitStShared(instr, out); return; }
    if (startsWith(op, "ld.shared"))  { emitLdShared(instr, out); return; }
    if (startsWith(op, "mov"))        { emitMov(instr, out);       return; }
    if (startsWith(op, "cvta") || startsWith(op, "cvt")) { emitCvt(instr, out); return; }
    if (startsWith(op, "setp"))       { emitSetp(instr, out);      return; }
    if (op == "bar.sync")             { emitBarSync(instr, out);   return; }
    if (op == "bra" || op == "ret")   { emitBra(instr, out);       return; }

    // Arithmetic and other ops
    emitArith(instr, out);
}

// ── ld.param ──────────────────────────────────────────────────────────────────

void RvEmitter::emitLdParam(const PtxInstr& instr, std::ostringstream& out) {
    if (instr.operands.size() != 2 || instr.operands[0].kind != PtxOperand::Kind::Reg
        || instr.operands[1].kind != PtxOperand::Kind::MemRef) {
        reject("ld.param requires a register and a parameter reference");
        return;
    }
    const auto& dst  = instr.operands[0];
    const auto& src  = instr.operands[1];

    int param_idx = -1;
    const PtxParam* param = nullptr;
    if (!src.param_name.empty()) {
        for (const auto& candidate : params_) {
            if (candidate.name == src.param_name) {
                param_idx = static_cast<int>(candidate.index);
                param = &candidate;
                break;
            }
        }
    }
    if (param_idx < 0 || param_idx >= 8 || param == nullptr) {
        reject("ld.param references an unknown or unsupported parameter: " + src.param_name);
        return;
    }
    if (src.mem_offset != 0) {
        reject("ld.param offsets are not supported by the RV32 argument ABI");
        return;
    }
    if (instr.op.find(".u64") != std::string::npos
        || instr.op.find(".b64") != std::string::npos) {
        if (address_size_ != 64 || !isWideRegister(dst.name)
            || (param->space != ".u64" && param->space != ".b64")) {
            reject("ld.param.u64 requires PTX64 pointer lowering into a declared wide register");
            return;
        }
    }

    const char* param_rv = kParamRegs[param_idx];

    // Determine if this is a float param
    bool is_float = (dst.kind == PtxOperand::Kind::Reg &&
                     (dst.name[0] == 'f' || freg_map_.count(dst.name) > 0));
    // Check opcode for type hint
    if (instr.op.find(".f32") != std::string::npos || instr.op.find(".f64") != std::string::npos)
        is_float = true;
    else if (instr.op.find(".u32") != std::string::npos || instr.op.find(".u64") != std::string::npos
             || instr.op.find(".b32") != std::string::npos || instr.op.find(".b64") != std::string::npos
             || instr.op.find(".s32") != std::string::npos)
        is_float = false;

    // Distinguish dst register type by prefix
    char pfx = dst.name.empty() ? 'r' : dst.name[0];
    if (pfx == 'f') is_float = true;

    if (is_float) {
        std::string frd = freg(dst.name);
        out << "    fmv.w.x " << frd << ", " << param_rv
            << "  # ld.param.f32 " << dst.name << " = " << src.param_name << "\n";
    } else {
        std::string ird = ireg(dst.name);
        out << "    mv " << ird << ", " << param_rv
            << "  # ld.param " << dst.name << " = " << src.param_name << "\n";
    }
}

// ── ld.global / ld.shared ─────────────────────────────────────────────────────

void RvEmitter::emitLdGlobal(const PtxInstr& instr, std::ostringstream& out) {
    if (instr.operands.size() != 2 || instr.operands[0].kind != PtxOperand::Kind::Reg
        || instr.operands[1].kind != PtxOperand::Kind::MemRef
        || instr.operands[1].mem_base.empty()) {
        reject("ld.global requires a destination register and register-based address");
        return;
    }
    const auto& dst = instr.operands[0];
    const auto& src = instr.operands[1];

    std::string base_rv;
    std::string offset_str = "0";
    if (src.kind == PtxOperand::Kind::MemRef) {
        if (!src.mem_base.empty()) base_rv = ireg(src.mem_base);
        if (src.mem_offset != 0)   offset_str = std::to_string(src.mem_offset);
    }

    bool is_float = (instr.op.find(".f32") != std::string::npos);
    if (is_float) {
        out << "    flw " << freg(dst.name) << ", " << offset_str
            << "(" << base_rv << ")  # " << instr.op << "\n";
    } else {
        out << "    lw " << ireg(dst.name) << ", " << offset_str
            << "(" << base_rv << ")  # " << instr.op << "\n";
    }
}

// ── st.global / st.shared ─────────────────────────────────────────────────────

void RvEmitter::emitStGlobal(const PtxInstr& instr, std::ostringstream& out) {
    if (instr.operands.size() != 2 || instr.operands[0].kind != PtxOperand::Kind::MemRef
        || instr.operands[0].mem_base.empty()
        || instr.operands[1].kind != PtxOperand::Kind::Reg) {
        reject("st.global requires a register-based address and source register");
        return;
    }
    const auto& addr_op = instr.operands[0];
    const auto& val_op  = instr.operands[1];

    std::string base_rv;
    std::string offset_str = "0";
    if (addr_op.kind == PtxOperand::Kind::MemRef) {
        if (!addr_op.mem_base.empty()) base_rv = ireg(addr_op.mem_base);
        if (addr_op.mem_offset != 0)   offset_str = std::to_string(addr_op.mem_offset);
    }

    bool is_float = (instr.op.find(".f32") != std::string::npos);
    if (is_float) {
        out << "    fsw " << freg(val_op.name) << ", " << offset_str
            << "(" << base_rv << ")  # " << instr.op << "\n";
    } else {
        out << "    sw " << ireg(val_op.name) << ", " << offset_str
            << "(" << base_rv << ")  # " << instr.op << "\n";
    }
}

void RvEmitter::emitLdShared(const PtxInstr& instr, std::ostringstream& out) {
    if (instr.operands.size() != 2 || instr.operands[0].kind != PtxOperand::Kind::Reg
        || instr.operands[1].kind != PtxOperand::Kind::MemRef) {
        reject("ld.shared requires a destination register and shared-memory reference");
        return;
    }
    const auto& dst = instr.operands[0];
    const auto& src = instr.operands[1];
    std::string base = "t6";
    out << "    li " << base << ", 0x00400000\n";
    if (!src.mem_base.empty()) {
        const std::string offset_reg = ireg(src.mem_base);
        out << "    add " << base << ", " << base << ", " << offset_reg << "\n";
    }
    const std::string offset = std::to_string(src.mem_offset);
    if (instr.op.find(".f32") != std::string::npos)
        out << "    flw " << freg(dst.name) << ", " << offset << "(" << base << ")\n";
    else
        out << "    lw " << ireg(dst.name) << ", " << offset << "(" << base << ")\n";
}

void RvEmitter::emitStShared(const PtxInstr& instr, std::ostringstream& out) {
    if (instr.operands.size() != 2 || instr.operands[0].kind != PtxOperand::Kind::MemRef
        || instr.operands[1].kind != PtxOperand::Kind::Reg) {
        reject("st.shared requires a shared-memory reference and source register");
        return;
    }
    const auto& dst = instr.operands[0];
    const auto& src = instr.operands[1];
    std::string base = "t6";
    out << "    li " << base << ", 0x00400000\n";
    if (!dst.mem_base.empty()) {
        const std::string offset_reg = ireg(dst.mem_base);
        out << "    add " << base << ", " << base << ", " << offset_reg << "\n";
    }
    const std::string offset = std::to_string(dst.mem_offset);
    if (instr.op.find(".f32") != std::string::npos)
        out << "    fsw " << freg(src.name) << ", " << offset << "(" << base << ")\n";
    else
        out << "    sw " << ireg(src.name) << ", " << offset << "(" << base << ")\n";
}

// ── mov ───────────────────────────────────────────────────────────────────────

void RvEmitter::emitMov(const PtxInstr& instr, std::ostringstream& out) {
    if (instr.operands.size() != 2 || instr.operands[0].kind != PtxOperand::Kind::Reg) {
        reject("mov requires a destination register and one source operand");
        return;
    }
    const auto& dst = instr.operands[0];
    const auto& src = instr.operands[1];

    if (src.kind == PtxOperand::Kind::SpecialReg) {
        // mov.u32 %rx, %tid.x → lw {ireg}, OFFSET(gp)
        int offset = specialRegOffset(src.name);
        if (offset >= 0) {
            out << "    lw " << ireg(dst.name) << ", " << offset << "(gp)"
                << "  # mov " << dst.name << ", %" << src.name << "\n";
        } else {
            out << "    # Unsupported special reg %" << src.name << "\n";
        }
        return;
    }

    if (src.kind == PtxOperand::Kind::Reg) {
        // mov.u32 %r0, %r1 (plain register copy)
        char pfx = dst.name.empty() ? 'r' : dst.name[0];
        if (pfx == 'f') {
            out << "    fmv.s " << freg(dst.name) << ", " << freg(src.name)
                << "  # " << instr.op << "\n";
        } else {
            std::string sreg = ireg(src.name);
            std::string dreg = ireg(dst.name);
            if (dreg != sreg)
                out << "    mv " << dreg << ", " << sreg << "  # " << instr.op << "\n";
        }
        return;
    }

    if (src.kind == PtxOperand::Kind::IntImm) {
        out << "    li " << ireg(dst.name) << ", " << src.int_val
            << "  # " << instr.op << "\n";
        return;
    }
    reject("unsupported mov operand: " + instr.op);
}

// ── cvta / cvt ────────────────────────────────────────────────────────────────

void RvEmitter::emitCvt(const PtxInstr& instr, std::ostringstream& out) {
    if (instr.operands.size() != 2 || instr.operands[0].kind != PtxOperand::Kind::Reg
        || instr.operands[1].kind != PtxOperand::Kind::Reg) {
        reject("cvt/cvta requires two register operands");
        return;
    }
    const auto& dst = instr.operands[0];
    const auto& src = instr.operands[1];

    // cvta.to.global.u64 %rd, %r → identity (mv)
    // cvt.u32.u64 %r, %rd → truncation treated as mv (32-bit world)
    // cvt.u64.u32 %rd, %r → zero-extend treated as mv (32-bit world)
    // cvt.s32.f32 %r, %f → fcvt.w.s
    // cvt.f32.s32 %f, %r → fcvt.s.w

    const std::string& op = instr.op;
    if (startsWith(op, "cvta")) {
        if (address_size_ != 64 || !isWideRegister(dst.name) || !isWideRegister(src.name)) {
            reject("cvta.u64 requires PTX64 declared wide registers");
            return;
        }
        std::string dreg = ireg(dst.name);
        std::string sreg = ireg(src.name);
        if (dreg != sreg) out << "    mv " << dreg << ", " << sreg << "  # " << op << " narrowed\n";
        return;
    }
    if (op == "cvt.u64.u32" || op == "cvt.s64.s32" || op == "cvt.u32.u64") {
        if (address_size_ != 64) {
            reject("64-bit conversion requires PTX64 narrowing mode");
            return;
        }
        std::string dreg = ireg(dst.name);
        std::string sreg = ireg(src.name);
        if (dreg != sreg) out << "    mv " << dreg << ", " << sreg << "  # " << op << " narrowed\n";
        return;
    }
    if (op.find(".u64") != std::string::npos || op.find(".s64") != std::string::npos
        || op.find(".b64") != std::string::npos || op.find(".f64") != std::string::npos) {
        reject("unsupported 64-bit conversion: " + op);
        return;
    }
    if (op.find("f32") != std::string::npos && op.find("s32") != std::string::npos) {
        // float↔int
        bool dst_float = (dst.name[0] == 'f');
        if (dst_float) {
            out << "    fcvt.s.w " << freg(dst.name) << ", " << ireg(src.name)
                << "  # " << op << "\n";
        } else {
            out << "    fcvt.w.s " << ireg(dst.name) << ", " << freg(src.name)
                << "  # " << op << "\n";
        }
        return;
    }

    // Default: mv (both treated as 32-bit)
    bool dst_float = (dst.name[0] == 'f');
    bool src_float = (src.kind == PtxOperand::Kind::Reg && src.name[0] == 'f');
    if (dst_float && !src_float) {
        out << "    fmv.w.x " << freg(dst.name) << ", " << ireg(src.name)
            << "  # " << op << "\n";
    } else if (!dst_float && src_float) {
        out << "    fmv.x.w " << ireg(dst.name) << ", " << freg(src.name)
            << "  # " << op << "\n";
    } else if (dst_float && src_float) {
        out << "    fmv.s " << freg(dst.name) << ", " << freg(src.name)
            << "  # " << op << "\n";
    } else {
        std::string dreg = ireg(dst.name);
        std::string sreg = ireg(src.name);
        if (dreg != sreg)
            out << "    mv " << dreg << ", " << sreg << "  # " << op << "\n";
    }
}

// ── setp ──────────────────────────────────────────────────────────────────────
//
// PTX setp.CMP.TYPE  %p, A, B   → RV comparison into integer pred register

void RvEmitter::emitSetp(const PtxInstr& instr, std::ostringstream& out) {
    if (instr.operands.size() != 3 || instr.operands[0].kind != PtxOperand::Kind::Reg
        || instr.operands[1].kind != PtxOperand::Kind::Reg) {
        reject("setp requires a predicate destination and two comparable operands");
        return;
    }
    const auto& dst = instr.operands[0];  // predicate %p
    const auto& a   = instr.operands[1];
    const auto& b   = instr.operands[2];

    const std::string& op = instr.op;
    std::string preg = ireg(dst.name);
    bool is_float = (op.find(".f32") != std::string::npos || op.find(".f64") != std::string::npos);

    if (is_float) {
        std::string fa = freg(a.name);
        std::string fb = freg(b.name);
        if (op.find(".lt") != std::string::npos) {
            out << "    flt.s " << preg << ", " << fa << ", " << fb << "  # " << op << "\n";
        } else if (op.find(".gt") != std::string::npos) {
            out << "    flt.s " << preg << ", " << fb << ", " << fa << "  # " << op << " (swapped)\n";
        } else if (op.find(".le") != std::string::npos) {
            out << "    fle.s " << preg << ", " << fa << ", " << fb << "  # " << op << "\n";
        } else if (op.find(".ge") != std::string::npos) {
            out << "    fle.s " << preg << ", " << fb << ", " << fa << "  # " << op << " (swapped)\n";
        } else if (op.find(".eq") != std::string::npos) {
            out << "    feq.s " << preg << ", " << fa << ", " << fb << "  # " << op << "\n";
        } else if (op.find(".ne") != std::string::npos) {
            out << "    feq.s " << preg << ", " << fa << ", " << fb << "\n";
            out << "    xori  " << preg << ", " << preg << ", 1  # " << op << "\n";
        }
        return;
    }

    // Integer setp: both operands are integer registers or immediates
    bool b_is_imm = (b.kind == PtxOperand::Kind::IntImm);
    std::string ra = ireg(a.name);
    std::string rb = b_is_imm ? "" : ireg(b.name);

    bool is_signed = (op.find(".s32") != std::string::npos);

    if (op.find(".lt") != std::string::npos) {
        if (b_is_imm) out << "    " << (is_signed ? "slti " : "sltiu ") << preg << ", " << ra << ", " << b.int_val << "\n";
        else out << "    " << (is_signed ? "slt " : "sltu ") << preg << ", " << ra << ", " << rb << "\n";
    } else if (op.find(".ge") != std::string::npos) {
        if (b_is_imm) {
            out << "    " << (is_signed ? "slti  " : "sltiu ") << preg << ", " << ra << ", " << b.int_val << "\n";
        } else {
            out << "    " << (is_signed ? "slt " : "sltu ") << preg << ", " << ra << ", " << rb << "\n";
        }
        out << "    xori  " << preg << ", " << preg << ", 1  # invert for ge\n";
    } else if (op.find(".gt") != std::string::npos) {
        if (b_is_imm) {
            // a > imm ⟺ a >= imm+1 ⟺ !(a < imm+1)
            out << "    " << (is_signed ? "slti  " : "sltiu ") << preg << ", " << ra << ", " << (b.int_val + 1) << "\n";
            out << "    xori  " << preg << ", " << preg << ", 1  # invert for gt\n";
        } else {
            out << "    " << (is_signed ? "slt " : "sltu ") << preg << ", " << rb << ", " << ra << "\n";
        }
    } else if (op.find(".le") != std::string::npos) {
        if (b_is_imm) {
            // a <= imm ⟺ !(a > imm) ⟺ !(a >= imm+1)
            out << "    " << (is_signed ? "slti  " : "sltiu ") << preg << ", " << ra << ", " << (b.int_val + 1) << "\n";
        } else {
            out << "    " << (is_signed ? "slt " : "sltu ") << preg << ", " << rb << ", " << ra << "\n";
            out << "    xori  " << preg << ", " << preg << ", 1  # invert for le\n";
        }
    } else if (op.find(".eq") != std::string::npos) {
        if (b_is_imm) {
            out << "    addi  " << preg << ", " << ra << ", " << (-b.int_val) << "\n";
            out << "    seqz  " << preg << ", " << preg << "  # " << op << "\n";
        } else {
            out << "    xor   " << preg << ", " << ra << ", " << rb << "\n";
            out << "    seqz  " << preg << ", " << preg << "  # " << op << "\n";
        }
    } else if (op.find(".ne") != std::string::npos) {
        if (b_is_imm) {
            out << "    addi  " << preg << ", " << ra << ", " << (-b.int_val) << "\n";
            out << "    snez  " << preg << ", " << preg << "  # " << op << "\n";
        } else {
            out << "    xor   " << preg << ", " << ra << ", " << rb << "\n";
            out << "    snez  " << preg << ", " << preg << "  # " << op << "\n";
        }
    } else {
        reject("unsupported setp opcode: " + op);
    }
}

// ── bra / ret ─────────────────────────────────────────────────────────────────

void RvEmitter::emitBra(const PtxInstr& instr, std::ostringstream& out) {
    if (instr.op == "ret") {
        out << "    ret\n";
        return;
    }

    // Unconditional branch
    if (instr.pred.empty()) {
        if (!instr.operands.empty() && instr.operands[0].kind == PtxOperand::Kind::Label) {
            out << "    j " << instr.operands[0].name << "\n";
        } else {
            reject("bra requires a label target");
        }
        return;
    }

    // Conditional branch: @%pred bra label
    std::string preg = ireg(instr.pred);
    std::string label;
    if (!instr.operands.empty()) label = instr.operands[0].name;

    if (instr.pred_not) {
        // @!%p bra label → if p==0 branch → beqz
        out << "    beqz " << preg << ", " << label
            << "  # @!" << instr.pred << " bra\n";
    } else {
        // @%p bra label → if p!=0 branch → bnez
        out << "    bnez " << preg << ", " << label
            << "  # @" << instr.pred << " bra\n";
    }
}

void RvEmitter::emitBarSync(const PtxInstr& instr, std::ostringstream& out) {
    if (!instr.pred.empty() || instr.operands.size() != 1
        || instr.operands[0].kind != PtxOperand::Kind::IntImm
        || instr.operands[0].int_val < 0 || instr.operands[0].int_val > 15) {
        error_ = "bar.sync requires one unpredicated immediate ID in range 0..15";
        return;
    }
    const uint32_t word = (static_cast<uint32_t>(instr.operands[0].int_val) << 20) | 0x0Bu;
    out << "    .4byte 0x" << std::hex << word << std::dec << "\n";
}

// ── Arithmetic ────────────────────────────────────────────────────────────────

void RvEmitter::emitArith(const PtxInstr& instr, std::ostringstream& out) {
    const std::string& op = instr.op;
    if (instr.operands.empty()) {
        reject("unsupported opcode: " + op);
        return;
    }

    // FP ops
    if (op.find(".f32") != std::string::npos || op.find(".f64") != std::string::npos) {
        if (instr.operands.size() < 3 && op.find("fma") == std::string::npos) {
            out << "    # FP operand count mismatch: " << op << "\n";
            return;
        }
        std::string fd = freg(instr.operands[0].name);
        if (startsWith(op, "fma")) {
            // fma.rn.f32 %fd, %fa, %fb, %fc → fmadd.s fd, fa, fb, fc
            if (instr.operands.size() < 4) { out << "    # fma missing operands\n"; return; }
            out << "    fmadd.s " << fd
                << ", " << freg(instr.operands[1].name)
                << ", " << freg(instr.operands[2].name)
                << ", " << freg(instr.operands[3].name)
                << "  # " << op << "\n";
        } else if (startsWith(op, "add")) {
            out << "    fadd.s " << fd
                << ", " << freg(instr.operands[1].name)
                << ", " << freg(instr.operands[2].name) << "\n";
        } else if (startsWith(op, "sub")) {
            out << "    fsub.s " << fd
                << ", " << freg(instr.operands[1].name)
                << ", " << freg(instr.operands[2].name) << "\n";
        } else if (startsWith(op, "mul")) {
            out << "    fmul.s " << fd
                << ", " << freg(instr.operands[1].name)
                << ", " << freg(instr.operands[2].name) << "\n";
        } else if (startsWith(op, "div")) {
            out << "    fdiv.s " << fd
                << ", " << freg(instr.operands[1].name)
                << ", " << freg(instr.operands[2].name) << "\n";
        } else if (startsWith(op, "neg")) {
            if (instr.operands.size() < 2) { out << "    # neg missing operands\n"; return; }
            out << "    fneg.s " << fd
                << ", " << freg(instr.operands[1].name) << "\n";
        } else if (startsWith(op, "abs")) {
            if (instr.operands.size() < 2) { out << "    # abs missing operands\n"; return; }
            out << "    fabs.s " << fd
                << ", " << freg(instr.operands[1].name) << "\n";
        } else if (startsWith(op, "sqrt")) {
            if (instr.operands.size() < 2) { out << "    # sqrt missing operands\n"; return; }
            out << "    fsqrt.s " << fd
                << ", " << freg(instr.operands[1].name) << "\n";
        } else {
            reject("unsupported FP opcode: " + op);
        }
        return;
    }

    // Integer ops
    if (instr.operands.size() < 2) {
        out << "    # Integer op with too few operands: " << op << "\n";
        return;
    }

    std::string rd = ireg(instr.operands[0].name);

    auto rarg = [&](size_t idx) -> std::string {
        if (idx >= instr.operands.size()) return "x0";
        const auto& operand = instr.operands[idx];
        if (operand.kind == PtxOperand::Kind::Reg) return ireg(operand.name);
        return "x0";
    };
    auto immarg = [&](size_t idx) -> int64_t {
        if (idx >= instr.operands.size()) return 0;
        return instr.operands[idx].int_val;
    };
    auto is_imm = [&](size_t idx) -> bool {
        if (idx >= instr.operands.size()) return false;
        return instr.operands[idx].kind == PtxOperand::Kind::IntImm;
    };

    bool is_signed = (op.find(".s32") != std::string::npos);

    if (startsWith(op, "add")) {
        if (instr.operands.size() >= 3 && is_imm(2)) {
            if (immarg(2) < -2048 || immarg(2) > 2047) {
                out << "    li t6, " << immarg(2) << "\n";
                out << "    add " << rd << ", " << rarg(1) << ", t6  # " << op << "\n";
            } else {
                out << "    addi " << rd << ", " << rarg(1) << ", " << immarg(2) << "  # " << op << "\n";
            }
        } else if (instr.operands.size() >= 3)
            out << "    add " << rd << ", " << rarg(1) << ", " << rarg(2) << "  # " << op << "\n";
    } else if (startsWith(op, "sub")) {
        if (instr.operands.size() >= 3 && is_imm(2)) {
            const int64_t immediate = -immarg(2);
            if (immediate < -2048 || immediate > 2047) {
                out << "    li t6, " << immediate << "\n";
                out << "    add " << rd << ", " << rarg(1) << ", t6  # " << op << "\n";
            } else {
                out << "    addi " << rd << ", " << rarg(1) << ", " << immediate << "  # " << op << "\n";
            }
        } else if (instr.operands.size() >= 3) {
            out << "    sub " << rd << ", " << rarg(1) << ", " << rarg(2) << "  # " << op << "\n";
        }
    } else if (startsWith(op, "mul.lo") || startsWith(op, "mul.wide")) {
        // mul by power of 2 → shift
        if (instr.operands.size() >= 3 && is_imm(2)) {
            int64_t imm = immarg(2);
            // Check if power of 2
            if (imm > 0 && (imm & (imm-1)) == 0) {
                int shift = 0;
                while ((1 << shift) < imm) ++shift;
                out << "    slli " << rd << ", " << rarg(1) << ", " << shift << "  # " << op << " *" << imm << "\n";
            } else {
                out << "    li t6, " << imm << "\n";
                out << "    mul " << rd << ", " << rarg(1) << ", t6  # " << op << "\n";
            }
        } else if (instr.operands.size() >= 3) {
            out << "    mul " << rd << ", " << rarg(1) << ", " << rarg(2) << "  # " << op << "\n";
        }
    } else if (startsWith(op, "mad.lo")) {
        // mad.lo.u32 %rw, %rx, %ry, %rz → mul tmp, rx, ry; add rw, tmp, rz
        if (instr.operands.size() >= 4) {
            out << "    mul " << rd << ", " << rarg(1) << ", " << rarg(2) << "\n";
            out << "    add " << rd << ", " << rd << ", " << rarg(3) << "  # " << op << "\n";
        }
    } else if (startsWith(op, "div")) {
        if (instr.operands.size() >= 3) {
            out << "    " << (is_signed ? "div " : "divu ") << rd
                << ", " << rarg(1) << ", " << rarg(2) << "  # " << op << "\n";
        }
    } else if (startsWith(op, "rem") || startsWith(op, "mod")) {
        if (instr.operands.size() >= 3) {
            out << "    " << (is_signed ? "rem " : "remu ") << rd
                << ", " << rarg(1) << ", " << rarg(2) << "  # " << op << "\n";
        }
    } else if (startsWith(op, "shl")) {
        if (instr.operands.size() >= 3 && is_imm(2))
            out << "    slli " << rd << ", " << rarg(1) << ", " << immarg(2) << "  # " << op << "\n";
        else if (instr.operands.size() >= 3)
            out << "    sll " << rd << ", " << rarg(1) << ", " << rarg(2) << "  # " << op << "\n";
    } else if (startsWith(op, "shr")) {
        bool arith = (op.find(".s32") != std::string::npos);
        if (instr.operands.size() >= 3 && is_imm(2))
            out << "    " << (arith ? "srai " : "srli ") << rd << ", " << rarg(1) << ", " << immarg(2) << "\n";
        else if (instr.operands.size() >= 3)
            out << "    " << (arith ? "sra " : "srl ") << rd << ", " << rarg(1) << ", " << rarg(2) << "\n";
    } else if (startsWith(op, "and")) {
        if (instr.operands.size() >= 3 && is_imm(2)) {
            out << "    li t6, " << immarg(2) << "\n";
            out << "    and " << rd << ", " << rarg(1) << ", t6  # " << op << "\n";
        } else if (instr.operands.size() >= 3) {
            out << "    and " << rd << ", " << rarg(1) << ", " << rarg(2) << "  # " << op << "\n";
        }
    } else if (startsWith(op, "or")) {
        if (instr.operands.size() >= 3 && is_imm(2)) {
            out << "    li t6, " << immarg(2) << "\n";
            out << "    or " << rd << ", " << rarg(1) << ", t6  # " << op << "\n";
        } else if (instr.operands.size() >= 3) {
            out << "    or " << rd << ", " << rarg(1) << ", " << rarg(2) << "  # " << op << "\n";
        }
    } else if (startsWith(op, "xor")) {
        if (instr.operands.size() >= 3 && is_imm(2)) {
            out << "    li t6, " << immarg(2) << "\n";
            out << "    xor " << rd << ", " << rarg(1) << ", t6  # " << op << "\n";
        } else if (instr.operands.size() >= 3) {
            out << "    xor " << rd << ", " << rarg(1) << ", " << rarg(2) << "  # " << op << "\n";
        }
    } else if (startsWith(op, "not")) {
        if (instr.operands.size() >= 2)
            out << "    not " << rd << ", " << rarg(1) << "  # " << op << "\n";
    } else if (startsWith(op, "neg")) {
        if (instr.operands.size() >= 2)
            out << "    neg " << rd << ", " << rarg(1) << "  # " << op << "\n";
    } else if (op == "membar.gl" || op == "membar.cta") {
        out << "    fence  # " << op << "\n";
    } else {
        reject("unsupported opcode: " + op);
    }
}

} // namespace ptx
} // namespace riscv_gpgpu
