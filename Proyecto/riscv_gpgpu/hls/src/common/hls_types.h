// hls_types.h - HLS-synthesizable streamable types
//
// Golden reference: models/systemc/src/common/types.h. That header's
// Instruction/WarpContext/MemTransaction structs are STL-free where they
// matter (POD fields) but WarpContext.regs/vregs/program are
// std::vector<std::vector<...>> - not synthesizable. This header replaces
// those with ap_uint-based, fixed-width equivalents sized per hls_config.h,
// and adds the stream element types (instr_word_t, mem_req_t, mem_resp_t,
// warp_status_t) named in docs/hls/interfaces.md SS2.2/SS3.3 but not yet
// defined there.
//
// Design note: Vitis HLS streams (hls::stream<T>) accept plain structs of
// fixed-width fields directly - no manual bit-packing into a raw ap_uint is
// needed for correctness, only for controlling exact interconnect width.
// This first pass favors structs (readable, matches the golden model's field
// names 1:1 for parity-test clarity) over hand-packed bit vectors; revisit
// only if T024 pragma/resource work shows the struct form costs more than a
// packed form would.

#ifndef RISCV_GPGPU_HLS_TYPES_H
#define RISCV_GPGPU_HLS_TYPES_H

#include <ap_int.h>
#include "hls_config.h"

namespace riscv_gpgpu_hls {

// ── Primitive aliases (docs/hls/interfaces.md SS4) ───────────────────────────
typedef ap_uint<32>          warp_id_t;
typedef ap_uint<8>           cu_id_t;
typedef ap_uint<6>           lane_id_t;                    // 0..31
typedef ap_uint<MAX_THREADS_PER_WARP> thread_mask_t;        // ap_uint<32>
typedef ap_uint<32>          reg_t;
typedef ap_uint<ADDR_BITS>   addr_t;
typedef ap_int<32>           imm_t;
typedef ap_uint<32>          barrier_id_t;

// ── Opcode (mirrors riscv_gpgpu::Opcode in types.h exactly - same values) ───
enum class Opcode : uint8_t {
    ADD    = 0x00, SUB  = 0x01, AND = 0x02, OR = 0x03, XOR = 0x04, SLT = 0x05,
    FADD   = 0x06, FMUL = 0x08,
    ADDI   = 0x10, LUI  = 0x11,
    LW     = 0x20, SW   = 0x21,
    BEQ    = 0x30, BNE  = 0x31, VBRANCH = 0x32, VJOIN = 0x33,
    VADD   = 0x40, VSUB = 0x41, VMUL    = 0x42, VFMADD  = 0x43,
    VFADD  = 0x48, VFSUB = 0x49, VFMUL  = 0x4A, VFFMADD = 0x4B,
    JAL    = 0x60, JALR = 0x61,
    BARRIER = 0x70,
    HALT   = 0xFF
};

inline bool isVectorOp(Opcode op) {
    uint8_t o = static_cast<uint8_t>(op);
    return o >= 0x40 && o < 0x60;
}
inline bool isMemoryOp(Opcode op) {
    uint8_t o = static_cast<uint8_t>(op);
    return o >= 0x20 && o < 0x30;
}
inline bool isBranchOp(Opcode op) {
    uint8_t o = static_cast<uint8_t>(op);
    return o >= 0x30 && o < 0x40;
}

// ── Instruction / instr_word_t ────────────────────────────────────────────────
// Golden reference: types.h's Instruction struct + makeInstr(). is_vector/
// is_memory/is_branch are carried explicitly (as the golden model does) rather
// than recomputed from opcode on every use, since they're cheap to compute
// once on the host/compiler side and this keeps compute_pipeline's decode
// logic identical in shape to ComputeUnit::executeWarp()'s dispatch.
struct Instruction {
    ap_uint<32> pc        = 0;
    Opcode      opcode    = Opcode::HALT;
    ap_uint<8>  rs1 = 0, rs2 = 0, rd = 0;
    imm_t       imm       = 0;
    bool        is_vector = false;
    bool        is_memory = false;
    bool        is_branch = false;
};
typedef Instruction instr_word_t;

// Register-bit reinterpretation helpers - identical semantics to
// types.h's regAsFloat/floatAsReg (bit reinterpretation only, no HLS-specific
// change needed; union avoids any host/vendor memcpy dependency under
// synthesis).
inline float regAsFloat(reg_t bits) {
    union { uint32_t u; float f; } c;
    c.u = static_cast<uint32_t>(bits);
    return c.f;
}
inline reg_t floatAsReg(float f) {
    union { uint32_t u; float f; } c;
    c.f = f;
    return reg_t(c.u);
}

// ── Register file (docs/hls/interfaces.md SS2.2's regs[][] parameter type) ──
typedef reg_t RegisterFile[MAX_THREADS_PER_WARP][NUM_REGS_PER_THREAD];

// ── Memory request/response (docs/hls/interfaces.md SS3.3) ──────────────────
// One request per active lane per LW/SW instruction (matches
// ComputeUnit::executeMemOp's per-thread loop - not lane-coalesced). cu_id +
// warp_id + lane_id let memory_pipeline route responses correctly when it is
// shared across multiple concurrent compute_pipeline instances (multi-CU).
struct mem_req_t {
    cu_id_t   cu_id     = 0;
    warp_id_t warp_id   = 0;
    lane_id_t lane_id   = 0;
    addr_t    address    = 0;
    bool      is_write   = false;
    reg_t     write_data = 0;
};

struct mem_resp_t {
    cu_id_t   cu_id   = 0;
    warp_id_t warp_id = 0;
    lane_id_t lane_id = 0;
    reg_t     data    = 0;
};

// ── Warp status (docs/hls/interfaces.md SS2.4 - host-orchestrated barriers) ─
enum class WarpStatusCode : uint8_t { COMPLETE = 0, STALLED_AT_BARRIER = 1 };

struct warp_status_t {
    WarpStatusCode code       = WarpStatusCode::COMPLETE;
    barrier_id_t   barrier_id = 0;   // meaningful only if code == STALLED_AT_BARRIER
};

}  // namespace riscv_gpgpu_hls

#endif  // RISCV_GPGPU_HLS_TYPES_H
