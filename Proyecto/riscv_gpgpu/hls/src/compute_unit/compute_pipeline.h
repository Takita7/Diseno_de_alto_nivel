// compute_pipeline.h - HLS-synthesizable top-level kernel
//
// Golden reference: models/systemc/src/compute_unit/compute_unit.{h,cpp}
// (ComputeUnit::executeWarp() and its executeALU/executeVector/executeMemOp/
// executeBranch/executeJoin helpers) + simt_controller for branch/join.
//
// Rewritten per docs/hls/interfaces.md SS2.5.3 (the on-chip-scheduler
// design) from T022's original per-invocation ap_ctrl_hs shape to a
// free-running, stream-dispatched one - see compute_pipeline.cpp's file
// header for exactly what changed and what was kept unchanged (per user
// direction: reuse as much of the already-tested T022 code as possible).
//
// Only the top-level function is declared here - executeALU/executeVector/
// executeMemOp/executeBranch/executeJoin/executeOneWarp are `static` in
// compute_pipeline.cpp, matching ComputeUnit's private-method encapsulation.

#ifndef RISCV_GPGPU_HLS_COMPUTE_PIPELINE_H
#define RISCV_GPGPU_HLS_COMPUTE_PIPELINE_H

#include <hls_stream.h>
#include "../common/hls_config.h"
#include "../common/hls_types.h"

namespace riscv_gpgpu_hls {

void compute_pipeline(
    cu_id_t          cu_id,       // s_axilite - fixed per instance (one per CU),
                                   // not per-dispatch: found while implementing
                                   // this, an earlier SS2.5.3 draft omitted it
                                   // from warp_dispatch_t without saying where
                                   // it should live instead. executeMemOp()
                                   // still needs it to stamp outgoing mem_req_t.

    hls::stream<warp_dispatch_t>& dispatch_in,   // axis, from this CU's CuDispatchUnit

    instr_word_t program[MAX_PROGRAM_LEN],        // ap_memory - this CU's local
                                                   // program store (SS10.8)
    uint32_t     program_len,                     // s_axilite, set once at launch

    reg_t regs[MAX_WARPS_PER_CU][MAX_THREADS_PER_WARP][NUM_REGS_PER_THREAD],
                                                   // ap_memory - aliases the owning
                                                   // CuDispatchUnit::regsArray()

    hls::stream<mem_req_t>&  mem_req_out,         // axis, via MemArbiter now
    hls::stream<mem_resp_t>& mem_resp_in,         // axis, via MemArbiter now

    hls::stream<warp_status_t>& status_out        // axis, to this CU's CuDispatchUnit
);

}  // namespace riscv_gpgpu_hls

#endif  // RISCV_GPGPU_HLS_COMPUTE_PIPELINE_H
