// compute_pipeline.h - HLS-synthesizable top-level kernel (T022)
//
// Golden reference: models/systemc/src/compute_unit/compute_unit.{h,cpp}
// (ComputeUnit::executeWarp() and its executeALU/executeVector/executeMemOp/
// executeBranch/executeJoin helpers) + simt_controller for branch/join.
// Signature per docs/hls/interfaces.md SS2.2.
//
// Only the top-level function is declared here - executeALU/executeVector/
// executeMemOp/executeBranch/executeJoin are `static` in compute_pipeline.cpp,
// matching ComputeUnit's private-method encapsulation (they were private
// methods there; there is no class here, so file-scope `static` is the
// equivalent).

#ifndef RISCV_GPGPU_HLS_COMPUTE_PIPELINE_H
#define RISCV_GPGPU_HLS_COMPUTE_PIPELINE_H

#include <hls_stream.h>
#include "../common/hls_config.h"
#include "../common/hls_types.h"

namespace riscv_gpgpu_hls {

void compute_pipeline(
    cu_id_t          cu_id,
    warp_id_t        warp_id,
    thread_mask_t    active_mask_init,
    uint32_t         program_len,

    hls::stream<instr_word_t>& instr_in,

    reg_t regs[MAX_THREADS_PER_WARP][NUM_REGS_PER_THREAD],

    hls::stream<mem_req_t>&  mem_req_out,
    hls::stream<mem_resp_t>& mem_resp_in,

    hls::stream<warp_status_t>& status_out
);

}  // namespace riscv_gpgpu_hls

#endif  // RISCV_GPGPU_HLS_COMPUTE_PIPELINE_H
