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
// 3 bits (0-7), not 8 (0-255, the old width): docs/hls/interfaces.md SS16.16.
// Real measured math (SS16.17): 2 CUs fit KV260's LUT budget (~79%), 3 do
// not (~118%, compute_pipeline's own footprint alone already exceeds the
// device at 3x), so 8 bits (256 CUs) was pure over-provisioning against a
// device that realistically caps out at 2. This width selects
// memory_pipeline.h's l1_caches_[NUM_CUS] array (kept as a real array, not
// collapsed to a scalar, specifically to support NUM_CUS>1 - see SS16.16).
// NOTE: tested directly as a candidate fix for the -3.37ns memory_pipeline
// timing violation (SS16.13/16.15) - real synthesis showed zero effect
// (identical Fmax, identical slack, SS16.17) - kept anyway as a harmless,
// verified-safe (46/46 csim tests) right-sizing matching the real 2-CU
// target, not because it fixes anything.
typedef ap_uint<3>           cu_id_t;
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

// On-chip storage/wire form of a program word (docs/hls/interfaces.md
// SS13.2) - real RV32I/custom-opcode bit encoding, decoded into the
// `Instruction` struct above (unchanged, still what every execute-stage
// function reads) once per fetch via decodeInstruction()
// (compute_unit/rv32i_codec.h). `instr_word_t` - the on-chip program-store
// element type (CuDispatchUnit::program_[], compute_pipeline's program[]
// parameter) - now names this raw form instead of `Instruction` directly;
// neither of those call sites inspects instruction fields, so this rename
// alone requires no changes to them (SS13.1).
typedef ap_uint<32> raw_instr_t;
typedef raw_instr_t instr_word_t;

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

// slot_id indexes one of a CU's MAX_WARPS_PER_CU resident warp slots -
// declared here (moved ahead of its original SS2.5.3 position below) since
// warp_status_t now needs it too. Width must hold MAX_WARPS_PER_CU+1 distinct
// values (0..MAX_WARPS_PER_CU-1 for real slots, plus cu_dispatch_unit.h's
// INVALID_SLOT sentinel == MAX_WARPS_PER_CU itself) - ap_uint<3> (0-7) was
// only ever one value short of overflowing at MAX_WARPS_PER_CU=8
// (INVALID_SLOT=8 silently wrapped to 0), found real via a genuine csim
// test failure (docs/hls/interfaces.md SS16.25) while testing that value,
// not by inspection. ap_uint<4> (0-15) covers MAX_WARPS_PER_CU up to 14
// with headroom.
typedef ap_uint<4> slot_id_t;

// ── Warp status (docs/hls/interfaces.md SS2.4 - host-orchestrated barriers) ─
enum class WarpStatusCode : uint8_t { COMPLETE = 0, STALLED_AT_BARRIER = 1 };

struct warp_status_t {
    WarpStatusCode code       = WarpStatusCode::COMPLETE;
    barrier_id_t   barrier_id = 0;   // meaningful only if code == STALLED_AT_BARRIER
    slot_id_t      slot_id    = 0;   // NEW (SS2.5.3) - which CuDispatchUnit slot this
                                      // result belongs to
    ap_uint<16>    resume_pc  = 0;   // NEW, found while implementing step 5 - the old
                                      // host-orchestrated design never needed this field
                                      // because the host tracked instruction-stream
                                      // position itself; compute_pipeline now reads a
                                      // shared program[] array by an internal index, so
                                      // it must report where to resume. Meaningful only
                                      // if code == STALLED_AT_BARRIER.
};

// ── On-chip scheduler types (docs/hls/interfaces.md SS2.5.3) ────────────────
// One resident slot per warp a CU currently owns for the running kernel -
// CuDispatchUnit's per-CU state (docs/hls/interfaces.md SS2.5.3/SS10.7).
// Golden reference for the fields it replaces: GPGPUTop::simulationProcess()'s
// local barrier_queue (a saved WarpContext per stalled warp, unbounded) -
// this is the bounded, per-slot equivalent.
// regs is deliberately NOT a member here (differs from an earlier docs/hls/
// interfaces.md SS2.5.3 draft) - found while implementing CuDispatchUnit:
// compute_pipeline's regs[][] port (SS2.5.3) needs one flat
// reg_t[MAX_WARPS_PER_CU][MAX_THREADS_PER_WARP][32] array to alias, and an
// array of WarpSlot structs (array-of-structs, one 4KB regs blob per element
// mixed with small bookkeeping fields) doesn't compose into that shape. Register
// storage lives in CuDispatchUnit as its own separate, flat member instead
// (struct-of-arrays) - see cu_dispatch_unit.h. WarpSlot keeps only the small
// per-warp bookkeeping fields.
struct WarpSlot {
    warp_id_t     warp_id    = 0;
    ap_uint<16>   resume_pc  = 0;   // program-store index to resume at after a stall
    barrier_id_t  barrier_id = 0;   // meaningful only if state == STALLED
    enum class State : uint8_t { EMPTY, READY, STALLED, DONE };
    State         state      = State::EMPTY;
    // Set true by launch() for a freshly-assigned slot, cleared by the
    // first buildDispatch() that packages it (docs/hls/interfaces.md
    // SS16) - lets compute_pipeline tell "first dispatch of a fresh warp,
    // seed regs_ from initial_regs_ptr" apart from "barrier resume, regs_
    // already holds live state, don't touch it". NOT the same thing
    // resume_pc==0 would suggest: a warp can legitimately stall with
    // resume_pc==0 in principle, so that field alone can't be trusted as
    // a fresh/resume signal.
    bool          fresh      = false;
};

// CuDispatchUnit -> compute_pipeline (docs/hls/interfaces.md SS2.5.3).
// Supersedes compute_pipeline's old per-invocation scalar arguments
// (cu_id/warp_id/active_mask_init, SS2.2) with one stream-carried struct.
struct warp_dispatch_t {
    slot_id_t     slot_id           = 0;
    warp_id_t     warp_id           = 0;
    thread_mask_t active_mask_init  = 0;
    ap_uint<16>   resume_pc         = 0;   // 0 for a fresh warp, else past the BARRIER
    // docs/hls/interfaces.md SS16: true exactly once per warp, on its
    // first dispatch after launch - tells compute_pipeline to seed
    // regs[slot_id] from initial_regs_ptr (indexed by warp_id) before
    // executing. Needed so regs_ has exactly one writer (compute_pipeline
    // itself) for real Vitis HLS DATAFLOW legality (SS15) - the scheduler
    // used to load initial regs at launch time, which real csynth
    // rejected as a second writer.
    bool          fresh_launch      = false;
};

}  // namespace riscv_gpgpu_hls

#endif  // RISCV_GPGPU_HLS_TYPES_H
