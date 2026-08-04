// rv_emitter.h — PTX AST → RV32IMF assembly text
//
// Register allocation strategy:
//   - Integer PTX regs (%r, %rd, %p) → pool: t0-t5, s0-s11  (18 regs)
//   - Float PTX regs (%f)            → pool: fa0-fa7, ft0-ft11 (20 regs)
//   - Parameters arrive in a0-a7 (integer ABI).
//     ld.param.u32 %rx, [param_N] → mv {ireg(rx)}, a{N}
//     ld.param.f32 %fx, [param_N] → fmv.w.x {freg(fx)}, a{N}
//
// Special registers (accessed via gp = THREAD_CTX_BASE = 0x00200000):
//   %tid.x   = 0(gp)    %tid.y   = 4(gp)    %tid.z   = 8(gp)
//   %ctaid.x = 12(gp)   %ctaid.y = 16(gp)   %ctaid.z = 20(gp)
//   %ntid.x  = 24(gp)   %ntid.y  = 28(gp)   %ntid.z  = 32(gp)
// THREAD_CTX_BASE is below SHARED_MEM_BASE (0x00400000).

#pragma once

#include "ptx_parser.h"
#include <string>
#include <map>

namespace riscv_gpgpu {
namespace ptx {

class RvEmitter {
public:
    // Emit RV32IMF assembly source for the given PTX kernel.
    // Returns the .s text, or "" on error.
    std::string emit(const PtxKernel& kernel);

    const std::string& lastError() const { return error_; }

private:
    std::string error_;

    // Register allocator state
    std::map<std::string, std::string> ireg_map_;   // ptx_reg_name → rv_ireg
    std::map<std::string, std::string> freg_map_;   // ptx_reg_name → rv_freg
    size_t next_ireg_ = 0;
    size_t next_freg_ = 0;

    // Param info
    std::vector<PtxParam> params_;

    void buildRegMaps(const PtxKernel& k);

    // Map a PTX register name (without %) to a hardware register name.
    // Allocates on first use.
    std::string ireg(const std::string& ptx_name);
    std::string freg(const std::string& ptx_name);

    // Emit a single PTX instruction
    void emitInstr(const PtxInstr& instr, std::ostringstream& out);

    // Helpers for specific instruction classes
    void emitLdParam   (const PtxInstr& i, std::ostringstream& out);
    void emitLdGlobal  (const PtxInstr& i, std::ostringstream& out);
    void emitStGlobal  (const PtxInstr& i, std::ostringstream& out);
    void emitLdShared  (const PtxInstr& i, std::ostringstream& out);
    void emitStShared  (const PtxInstr& i, std::ostringstream& out);
    void emitMov       (const PtxInstr& i, std::ostringstream& out);
    void emitArith     (const PtxInstr& i, std::ostringstream& out);
    void emitSetp      (const PtxInstr& i, std::ostringstream& out);
    void emitBra       (const PtxInstr& i, std::ostringstream& out);
    void emitCvt       (const PtxInstr& i, std::ostringstream& out);
    void emitBarSync   (const PtxInstr& i, std::ostringstream& out);

    // Resolve operand to a register name or immediate string
    std::string resolveReg(const PtxOperand& op);
    std::string resolveFreg(const PtxOperand& op);
    std::string resolveImm(const PtxOperand& op);

    // Check if string starts with prefix
    static bool startsWith(const std::string& s, const std::string& p) {
        return s.size() >= p.size() && s.substr(0, p.size()) == p;
    }
};

} // namespace ptx
} // namespace riscv_gpgpu
