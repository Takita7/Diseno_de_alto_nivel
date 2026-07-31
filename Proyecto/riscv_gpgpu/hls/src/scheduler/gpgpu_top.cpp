// gpgpu_top.cpp - definition of the real top-level "compute" IP
// (docs/hls/interfaces.md SS15/SS16.6). See gpgpu_top.h for why this is a
// real .cpp, not header-only `inline` like schedulerCore.

#include "gpgpu_top.h"

namespace riscv_gpgpu_hls {

void gpgpu_scheduler(
    instr_word_t* program_ptr,
    reg_t*        initial_regs_ptr,
    uint32_t      program_len,
    warp_id_t     total_warps,
    bool          start,
    bool&         busy,
    bool&         done,
    bool&         fault,
    hls::stream<mem_req_t>&  mem_req_out,
    hls::stream<mem_resp_t>& mem_resp_in
) {
#pragma HLS INTERFACE m_axi    port=program_ptr      offset=slave bundle=gmem0
#pragma HLS INTERFACE m_axi    port=initial_regs_ptr offset=slave bundle=gmem1
#pragma HLS INTERFACE s_axilite port=program_len  bundle=control
#pragma HLS INTERFACE s_axilite port=total_warps  bundle=control
#pragma HLS INTERFACE s_axilite port=start        bundle=control
#pragma HLS INTERFACE s_axilite port=busy         bundle=control
#pragma HLS INTERFACE s_axilite port=done         bundle=control
#pragma HLS INTERFACE s_axilite port=fault        bundle=control
#pragma HLS INTERFACE axis      port=mem_req_out
#pragma HLS INTERFACE axis      port=mem_resp_in
#pragma HLS DATAFLOW

    // All local (SS16.6) - regs_/program_ storage shared between
    // schedulerCore (writes program_ via loadProgram) and compute_pipeline
    // (reads/writes both through its own ports) - the one sharing pattern
    // never flagged in any real csynth attempt. Everything else
    // (WarpSlot[]/BarrierState) is private inside schedulerCore's own body
    // now, not reachable here at all.
    CuDispatchUnit cu;
    hls::stream<warp_dispatch_t> dispatch_out;
    hls::stream<warp_status_t>   status_in;
    hls::stream<mem_req_t>       cu_mem_req[NUM_CUS];
    hls::stream<mem_resp_t>      cu_mem_resp[NUM_CUS];

    // Real csynth finding (SS16.6): DATAFLOW process-call arguments must
    // be plain declared variables or function arguments, not the result
    // of a method call like cu.programArray()/cu.regsArray() inlined
    // directly into the call - `[HLS 214-113]`, "either use an argument
    // of the function or declare the variable inside the dataflow loop
    // body". Declared here as real local references instead.
    instr_word_t (&program)[MAX_PROGRAM_LEN] = cu.programArray();
    reg_t (&regs)[MAX_WARPS_PER_CU][MAX_THREADS_PER_WARP][NUM_REGS_PER_THREAD] = cu.regsArray();
    // Same [HLS 214-113] rule, one more instance: cu_id_t(0) is a
    // constructor-call expression, not a plain variable either.
    cu_id_t cu_id = 0;

    schedulerCore(cu, program_ptr, program_len, total_warps,
                  start, busy, done, fault, dispatch_out, status_in);

    // NUM_CUS=1 (decided, docs/hls/interfaces.md SS10.11).
    compute_pipeline(cu_id, dispatch_out,
                      program, program_len,
                      regs, initial_regs_ptr,
                      cu_mem_req[0], cu_mem_resp[0], status_in);

    mem_arbiter(cu_mem_req, cu_mem_resp, mem_req_out, mem_resp_in);
}

}  // namespace riscv_gpgpu_hls
