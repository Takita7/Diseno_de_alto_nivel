// compute_pipeline.cpp - T022: HLS-synthesizable port of ComputeUnit::executeWarp()
//
// This is a direct port, not a reimplementation: every helper below mirrors
// one function in models/systemc/src/compute_unit/compute_unit.cpp, same
// name, same per-lane loop structure, same switch/case bodies. Deviations
// from the golden model are called out explicitly at the point they occur -
// there should be no undocumented behavioral difference.
//
// Known, intentional differences from the golden model (all documented
// inline at their point of occurrence below):
//   1. Register file is a fixed reg_t[MAX_THREADS_PER_WARP][32] instead of
//      WarpContext's std::vector<std::vector<uint32_t>> - required for HLS,
//      no behavioral change (same indexing, same never-reset-by-us contract).
//   2. Address computation wraps at 32 bits (addr_t = ap_uint<ADDR_BITS>)
//      instead of golden's Address=uint64_t widening trick - see
//      executeMemOp(). No behavioral difference for any address that doesn't
//      approach the 32-bit wraparound boundary (true of every kernel in
//      kernel_programs.h).
//   3. Memory requests/responses go over hls::stream instead of direct
//      MemoryHierarchy::loadWord()/storeWord() calls - required by the
//      compute_pipeline/memory_pipeline split (docs/hls/interfaces.md SS1).
//      Still one request per active lane, still blocking/sequential (no
//      batching across lanes) - same access pattern as the golden model's
//      per-thread loop, just expressed as stream I/O instead of a function
//      call.
//   4. Per-instruction/per-cycle statistics (ComputeUnit::total_cycles_/
//      total_instructions_, SIMTController::divergence_events_/
//      wasted_cycles_) are NOT exposed as outputs here - docs/hls/
//      interfaces.md SS2.2's signature has no port for them. DivergenceStack
//      still tracks them internally (accessible via getDivergenceEvents()/
//      getWastedCycles() if a future port needs them) but compute_pipeline
//      doesn't read or emit them yet.
//
// NOT ported (because the golden model doesn't implement it either - see
// executeALU()'s comment): BEQ/BNE/JAL/JALR. Grep-verified against
// compute_unit.cpp - these opcodes fall through to executeALU()'s
// `default: break` there too. This is inherited golden-model ISA scope, not
// a porting gap.
//
// ── docs/hls/interfaces.md SS2.5.3 rewrite: what changed, what didn't ──────
// Per user direction: reuse as much of the above (already implemented,
// already tested) as possible, rather than rewriting from scratch.
//
//   - executeALU/executeVector/executeMemOp/executeBranch/executeJoin below
//     are UNCHANGED, byte-for-byte, from the original T022 port. They
//     operate on a RegFile reference - exactly one slot's register storage
//     either way, so nothing about them needed to change.
//   - The old entry point (one decode loop, BARRIER -> write status_out +
//     `return`) is now executeOneWarp(), returning a warp_status_t instead
//     of writing one to a stream and returning from the whole kernel - a
//     free-running kernel can never `return` on a BARRIER, it has to go
//     back and read the next dispatch.
//   - Instructions come from a local program[] array indexed by pc
//     (SS10.8) instead of a sequential instr_in stream - resuming a
//     stalled warp is now "start the loop at resume_pc" instead of the
//     host carefully feeding a partial stream starting after the BARRIER.
//   - warp_status_t now carries resume_pc, found necessary while
//     implementing this: the old design never needed it because the host
//     tracked stream position itself; nothing here does that anymore, so
//     compute_pipeline has to report where to resume (SS2.5.3, hls_types.h).
//   - cu_id is back as a scalar parameter, not part of warp_dispatch_t
//     (an earlier SS2.5.3 draft omitted it without saying where it should
//     live) - it's fixed per compute_pipeline instance (one per CU), not
//     per-dispatch varying, so it belongs alongside program_len as
//     launch-time configuration, not a per-dispatch stream field.

#include "compute_pipeline.h"
#include "../simt_controller/divergence_stack.h"
#include "rv32i_codec.h"

namespace riscv_gpgpu_hls {

typedef reg_t RegFile[MAX_THREADS_PER_WARP][NUM_REGS_PER_THREAD];

// ── Scalar ALU (integer + FP) ────────────────────────────────────────────────
// Golden reference: ComputeUnit::executeALU() (compute_unit.cpp:124-147).
static void executeALU(RegFile regs, const Instruction& instr, thread_mask_t mask) {
    Opcode op = instr.opcode;
    // TEMP: SS16.16 scratch experiment - is the per-lane, per-opcode
#pragma HLS ALLOCATION operation instances=add limit=8
#pragma HLS ALLOCATION operation instances=sub limit=8
EXECUTE_ALU_LANES:
    for (int t = 0; t < MAX_THREADS_PER_WARP; ++t) {
#pragma HLS UNROLL
        if (!mask[t]) continue;
        reg_t a = regs[t][instr.rs1];
        reg_t b = (op == Opcode::ADDI || op == Opcode::LUI)
                  ? reg_t(instr.imm)
                  : regs[t][instr.rs2];
        switch (op) {
            case Opcode::ADD:
            case Opcode::ADDI: regs[t][instr.rd] = a + b; break;
            case Opcode::SUB:  regs[t][instr.rd] = a - b; break;
            case Opcode::AND:  regs[t][instr.rd] = a & b; break;
            case Opcode::OR:   regs[t][instr.rd] = a | b; break;
            case Opcode::XOR:  regs[t][instr.rd] = a ^ b; break;
            case Opcode::SLT:  regs[t][instr.rd] = (ap_int<32>(a) < ap_int<32>(b)) ? reg_t(1) : reg_t(0); break;
            case Opcode::LUI:  regs[t][instr.rd] = b << 12; break;
            case Opcode::FADD: regs[t][instr.rd] = floatAsReg(regAsFloat(a) + regAsFloat(b)); break;
            case Opcode::FMUL: regs[t][instr.rd] = floatAsReg(regAsFloat(a) * regAsFloat(b)); break;
            // BEQ/BNE/JAL/JALR/etc land here. Golden ComputeUnit::executeALU's
            // switch (compute_unit.cpp:133-145) has no cases for them either
            // (grep-verified) - they are unimplemented dead opcode space in
            // the golden model itself, not something dropped during porting.
            default: break;
        }
    }
}

// ── Vector (integer + FP) ────────────────────────────────────────────────────
// Golden reference: ComputeUnit::executeVector() (compute_unit.cpp:151-171).
// Gated by RISCV_GPGPU_ENABLE_VECTOR_OPS (default OFF): audit confirmed the
// PTX transpiler never emits these opcodes, so the hardware is dead area for
// all current PTX-based workloads.  Re-enable with -DRISCV_GPGPU_ENABLE_VECTOR_OPS=1.
static void executeVector(RegFile regs, const Instruction& instr, thread_mask_t mask) {
#ifdef RISCV_GPGPU_ENABLE_VECTOR_OPS
    Opcode op = instr.opcode;
#pragma HLS ALLOCATION operation instances=add limit=8
#pragma HLS ALLOCATION operation instances=sub limit=8
#pragma HLS ALLOCATION operation instances=mul limit=8
#pragma HLS ALLOCATION operation instances=fadd limit=8
#pragma HLS ALLOCATION operation instances=fsub limit=8
#pragma HLS ALLOCATION operation instances=fmul limit=8
EXECUTE_VECTOR_LANES:
    for (int t = 0; t < MAX_THREADS_PER_WARP; ++t) {
#pragma HLS UNROLL
        if (!mask[t]) continue;
        reg_t a = regs[t][instr.rs1];
        reg_t b = regs[t][instr.rs2];
        reg_t c = regs[t][instr.rd];
        switch (op) {
            case Opcode::VADD:    regs[t][instr.rd] = a + b;     break;
            case Opcode::VSUB:    regs[t][instr.rd] = a - b;     break;
            case Opcode::VMUL:    regs[t][instr.rd] = a * b;     break;
            case Opcode::VFMADD:  regs[t][instr.rd] = a * b + c; break;
            case Opcode::VFADD:   regs[t][instr.rd] = floatAsReg(regAsFloat(a) + regAsFloat(b));                 break;
            case Opcode::VFSUB:   regs[t][instr.rd] = floatAsReg(regAsFloat(a) - regAsFloat(b));                 break;
            case Opcode::VFMUL:   regs[t][instr.rd] = floatAsReg(regAsFloat(a) * regAsFloat(b));                 break;
            case Opcode::VFFMADD: regs[t][instr.rd] = floatAsReg(regAsFloat(a) * regAsFloat(b) + regAsFloat(c)); break;
            default: break;
        }
    }
#else
    (void)regs; (void)instr; (void)mask;
#endif
}

// ── Memory ────────────────────────────────────────────────────────────────────
// Golden reference: ComputeUnit::executeMemOp() (compute_unit.cpp:175-190).
// One request per active lane, sequential - matches the golden per-thread
// loop's access pattern exactly (no coalescing/batching introduced here).
// Every request (load or store) gets exactly one response back (see the
// "memory contract" note in docs/hls/interfaces.md SS2.2) - a store's
// response data is don't-care, just a completion signal, so this loop can
// always do write-then-blocking-read without branching on op.
static void executeMemOp(cu_id_t cu_id, warp_id_t warp_id, RegFile regs,
                          const Instruction& instr, thread_mask_t mask,
                          hls::stream<mem_req_t>&  mem_req_out,
                          hls::stream<mem_resp_t>& mem_resp_in) {
    Opcode op = instr.opcode;
EXECUTE_MEM_LANES:
    for (int t = 0; t < MAX_THREADS_PER_WARP; ++t) {
        if (!mask[t]) continue;

        // Golden: `Address addr = (int64_t)ctx.regs[t][instr.rs1] + instr.imm;`
        // (compute_unit.cpp:180-181) widens to 64 bits purely as a simulation
        // convenience (Address is uint64_t there). addr_t here is
        // ap_uint<ADDR_BITS> (32 by default, docs/hls/interfaces.md SS4) -
        // real hardware has a real, bounded address bus, so this wraps at
        // 2^ADDR_BITS instead of widening. No behavioral difference for any
        // kernel whose addresses don't approach that boundary (true of every
        // kernel_programs.h kernel - see memoryRoundTrip()/barrierRoundTrip(),
        // both use small offsets from a base in the 0x10000 range).
        addr_t addr = addr_t(regs[t][instr.rs1]) + addr_t(instr.imm);

        mem_req_t req;
        req.cu_id     = cu_id;
        req.warp_id   = warp_id;
        req.lane_id   = t;
        req.address   = addr;
        req.is_write  = (op == Opcode::SW);
        req.write_data = (op == Opcode::SW) ? regs[t][instr.rs2] : reg_t(0);
        mem_req_out.write(req);

        mem_resp_t resp = mem_resp_in.read();   // blocking - matches golden's
                                                 // synchronous loadWord()/storeWord() call
        if (op == Opcode::LW) {
            regs[t][instr.rd] = resp.data;
        }
        // SW: resp is a completion ack only, data field unused - see the
        // "memory contract" note in docs/hls/interfaces.md SS2.2.
    }
}

// ── SIMT branch / join ────────────────────────────────────────────────────────
// Golden reference: ComputeUnit::executeBranch()/executeJoin()
// (compute_unit.cpp:194-206).
static void executeBranch(DivergenceStack& simt, RegFile regs,
                           const Instruction& instr, thread_mask_t mask) {
    bool conditions[MAX_THREADS_PER_WARP];
EXECUTE_BRANCH_LANES:
    for (int t = 0; t < MAX_THREADS_PER_WARP; ++t) {
#pragma HLS UNROLL
        // Golden only ever reads conditions[t] for lanes active in `mask`
        // (SIMTController::handleBranch's `if ((current>>t)&1u)` guard) - the
        // `false` default for inactive lanes here is just deterministic
        // filler, never read.
        conditions[t] = mask[t] ? (regs[t][instr.rs1] == 0) : false;
    }
    simt.handleBranch(conditions);
}

static void executeJoin(DivergenceStack& simt) {
    simt.handleJoin();
}

// ── Per-warp execution (docs/hls/interfaces.md SS2.5.3) ─────────────────────
// Golden reference: ComputeUnit::executeWarp() (compute_unit.cpp:62-120) -
// same reference T022 used; this is that same logic, restructured to return
// its result instead of writing to status_out and returning from the whole
// kernel (see this file's header for why).
static warp_status_t executeOneWarp(cu_id_t cu_id, const warp_dispatch_t& d,
                                     instr_word_t program[MAX_PROGRAM_LEN],
                                     uint32_t program_len,
                                     RegFile regs,
                                     hls::stream<mem_req_t>&  mem_req_out,
                                     hls::stream<mem_resp_t>& mem_resp_in) {
    DivergenceStack simt;
    // Golden: `simt_ctrl_->initializeWarp(ctx.warp_id, config_.threads_per_warp);`
    // is called at the top of EVERY executeWarp() invocation, fresh start or
    // barrier resume alike (compute_unit.cpp:71 + its comment) - ported
    // unconditionally here too, not just on a "first call" path.
    simt.initializeWarp(d.active_mask_init);

    ap_uint<32> instr_count = 0;  // T077: instructions retired in this dispatch

EXECUTE_ONE_WARP_DECODE_LOOP:
    for (uint32_t i = d.resume_pc; i < program_len; ++i) {
#pragma HLS LOOP_TRIPCOUNT max=MAX_PROGRAM_LEN
        if (i >= static_cast<uint32_t>(MAX_PROGRAM_LEN)) break;  // static bound - see hls_config.h's open MAX_PROGRAM_LEN sizing note

        const Instruction instr = decodeInstruction(program[i]);   // SS13.2 - program[] now holds raw_instr_t
        thread_mask_t mask = simt.getActiveMask();
        Opcode       op    = instr.opcode;

        if (op == Opcode::HALT) {
            break;  // golden: `++total_instructions_; break;` (compute_unit.cpp:83-86) - see file header note 4 on stats
        }

        ++instr_count;  // T077: count every non-HALT instruction that retires

        if (op == Opcode::BARRIER) {
            // Golden: records arrival (simt_ctrl_->threadHitBarrier), advances
            // pc past BARRIER, sets STALLED, returns (compute_unit.cpp:88-105).
            // Arrival bookkeeping is now on-chip (BarrierArbiter, docs/hls/
            // interfaces.md SS2.5.5, superseding SS2.4's host-orchestrated
            // contract) - compute_pipeline's job is still just to report the
            // stall and stop where it is, same as before.
            warp_status_t st;
            st.code        = WarpStatusCode::STALLED_AT_BARRIER;
            st.barrier_id  = barrier_id_t(instr.imm);
            st.resume_pc   = ap_uint<16>(i + 1);   // golden: `++ctx.pc` before returning
            st.instr_count = instr_count;
            return st;
        }

        // Golden dispatch order, unchanged (compute_unit.cpp:107-111):
        if      (op == Opcode::VBRANCH) executeBranch(simt, regs, instr, mask);
        else if (op == Opcode::VJOIN)   executeJoin(simt);
        else if (instr.is_memory)       executeMemOp(cu_id, d.warp_id, regs, instr, mask, mem_req_out, mem_resp_in);
#ifdef RISCV_GPGPU_ENABLE_VECTOR_OPS
        else if (instr.is_vector)       executeVector(regs, instr, mask);
#endif
        else                            executeALU(regs, instr, mask);
    }

    warp_status_t st;
    st.code        = WarpStatusCode::COMPLETE;
    st.barrier_id  = 0;
    st.instr_count = instr_count;
    return st;
}

// ── compute_pipeline (docs/hls/interfaces.md SS2.5.3) ───────────────────────
void compute_pipeline(
    cu_id_t          cu_id,

    hls::stream<warp_dispatch_t>& dispatch_in,

    instr_word_t program[MAX_PROGRAM_LEN],
    uint32_t     program_len,

    reg_t regs[MAX_WARPS_PER_CU][MAX_THREADS_PER_WARP][NUM_REGS_PER_THREAD],

    reg_t* initial_regs_ptr,

    hls::stream<mem_req_t>&  mem_req_out,
    hls::stream<mem_resp_t>& mem_resp_in,

    hls::stream<warp_status_t>& status_out
) {
#pragma HLS INTERFACE s_axilite port=cu_id       bundle=control
#pragma HLS INTERFACE s_axilite port=program_len bundle=control
#pragma HLS INTERFACE axis      port=dispatch_in
#pragma HLS INTERFACE ap_memory port=program
#pragma HLS INTERFACE ap_memory port=regs
#pragma HLS INTERFACE m_axi     port=initial_regs_ptr offset=slave bundle=gmem
    // Lane dimension is now dim=2, not dim=1 - a slot dimension (dim=1) was
    // added in front of it (SS2.5.3). Same reason as T024's original
    // partition (executeALU/executeVector unroll over all 32 lanes, every
    // one reading/writing regs[..][t][..] the same cycle); dim=1 (slot) is
    // deliberately left un-partitioned so slot_id addresses into ordinary
    // BRAM instead of replicating storage across all MAX_WARPS_PER_CU slots
    // - unverified against real synthesis yet, same T024-era caveat as the
    // rest of this file's pragmas (docs/hls/interfaces.md SS8).
#pragma HLS ARRAY_PARTITION variable=regs dim=2 complete
#pragma HLS INTERFACE axis      port=mem_req_out
#pragma HLS INTERFACE axis      port=mem_resp_in
#pragma HLS INTERFACE axis      port=status_out

    // Free-running (docs/hls/interfaces.md SS2.5.2) - same persistent-
    // hardware model memory_pipeline already used, now extended here too.
    // No `return` anywhere in this loop, unlike T022: see this file's
    // header for why a BARRIER can no longer end the kernel invocation.
    while (true) {
#pragma HLS PIPELINE off
        warp_dispatch_t d = dispatch_in.read();   // blocking

        // docs/hls/interfaces.md SS16: seed this slot's regs from the
        // host's DRAM buffer exactly once, on its first dispatch since
        // launch - never on a barrier resume (regs already hold live
        // state then). initial_regs_ptr is global-warp-id-indexed (SS16),
        // not slot-indexed - different threads/warps get different
        // tid/address argument values, unlike program[] (broadcast,
        // SS10.8). Flattened to a single loop (SS16.10/16.11): the 2D
        // t/r nested-loop form crashed two independent HLS compiler
        // versions (2023.1 SeqAccessesInfoPass, 2026.1 FlattenLoopNest)
        // in their own loop-nest analysis passes; this form needs no
        // such analysis since there's only one loop level.
        if (d.fresh_launch) {
            uint64_t base = uint64_t(d.warp_id) * MAX_THREADS_PER_WARP * NUM_REGS_PER_THREAD;
        SEED_INITIAL_REGS:
            for (int i = 0; i < MAX_THREADS_PER_WARP * NUM_REGS_PER_THREAD; ++i) {
                regs[d.slot_id][i / NUM_REGS_PER_THREAD][i % NUM_REGS_PER_THREAD] =
                    initial_regs_ptr[base + i];
            }
        }

        warp_status_t st = executeOneWarp(cu_id, d, program, program_len,
                                           regs[d.slot_id],
                                           mem_req_out, mem_resp_in);
        st.slot_id = d.slot_id;
        status_out.write(st);
    }
}

}  // namespace riscv_gpgpu_hls
