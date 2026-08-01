// gpgpu_top.cpp - definition of the real top-level "compute" IP
// (docs/hls/interfaces.md SS15/SS16.6). See gpgpu_top.h for why this is a
// real .cpp, not header-only `inline` like schedulerCore.

#include "gpgpu_top.h"

// T024-style per-board tuning (see memory_pipeline.cpp's identical pattern):
// a macro, not constexpr, so it substitutes textually into the #pragma HLS
// INTERFACE m_axi line below. Falls back to 0 (widening disabled - prior
// behavior) if no board macro is defined, so plain csim/no-board builds are
// unaffected.
#ifndef RISCV_GPGPU_MAXI_MAX_WIDEN_BITWIDTH
#define RISCV_GPGPU_MAXI_MAX_WIDEN_BITWIDTH 0
#endif

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
    // docs/hls/interfaces.md SS16.29: max_widen_bitwidth matches Zynq
    // UltraScale+'s real PL-side AXI HP/HPC port width (128, AMD's own
    // sizing guidance) on KV260 builds - the Vivado IP flow's default is 0
    // (widening disabled), confirmed via this port's own real burst.xml
    // report ("threshold of 0") before this fix, not assumed. Lets the tool
    // pack multiple sequential 32-bit reads into fewer, wider AXI beats
    // instead of using 32 of 128 real bus bits per transfer.
#pragma HLS INTERFACE m_axi    port=program_ptr      offset=slave bundle=gmem0 max_widen_bitwidth=RISCV_GPGPU_MAXI_MAX_WIDEN_BITWIDTH
#pragma HLS INTERFACE m_axi    port=initial_regs_ptr offset=slave bundle=gmem1 max_widen_bitwidth=RISCV_GPGPU_MAXI_MAX_WIDEN_BITWIDTH
#pragma HLS INTERFACE s_axilite port=program_len  bundle=control
#pragma HLS INTERFACE s_axilite port=total_warps  bundle=control
#pragma HLS INTERFACE s_axilite port=start        bundle=control
#pragma HLS INTERFACE s_axilite port=busy         bundle=control
#pragma HLS INTERFACE s_axilite port=done         bundle=control
#pragma HLS INTERFACE s_axilite port=fault        bundle=control
#pragma HLS INTERFACE axis      port=mem_req_out
#pragma HLS INTERFACE axis      port=mem_resp_in
#pragma HLS DATAFLOW

    // All local (SS16.6) - regs_/program_ storage shared between each CU's
    // own schedulerCore (writes program_ via loadProgram) and its own
    // compute_pipeline (reads/writes both through its own ports) - the one
    // sharing pattern never flagged in any real csynth attempt, now
    // repeated once per CU (SS16.37: NUM_CUS=2, real). WarpSlot[] is
    // private inside each schedulerCore's own body, not reachable here.
    // BarrierState lives inside the one, shared barrierCore instead - also
    // not reachable here, just the streams connecting to it are.
    //
    // Real DATAFLOW requirement (SS16.6's [HLS 214-113] canonical-form
    // rule): process-call arguments must be plain declared variables, not
    // a runtime loop over an array of task instances - HLS DATAFLOW
    // extracts concurrent tasks from the *source's own* distinct call
    // sites, not from loop iterations. Real NUM_CUS-instance support is
    // therefore explicit, named duplication (cu0/cu1, not `cu[NUM_CUS]`
    // called in a loop) - the same reason cu_id_t(0) below can't be
    // inlined either. If NUM_CUS ever needs to grow past 2, this section
    // needs the same explicit treatment repeated, not a config bump alone.
    CuDispatchUnit cu0;
    CuDispatchUnit cu1;
    hls::stream<warp_dispatch_t> dispatch_out0;
    hls::stream<warp_dispatch_t> dispatch_out1;
    hls::stream<warp_status_t>   status_in0;
    hls::stream<warp_status_t>   status_in1;
    hls::stream<mem_req_t>       cu_mem_req[NUM_CUS];
    hls::stream<mem_resp_t>      cu_mem_resp[NUM_CUS];
    // barrierCore itself IS generic over NUM_CUS (mem_arbiter's proven
    // array-of-streams shape) - only the per-CU schedulerCore/
    // compute_pipeline task CALLS need explicit duplication, not this.
    hls::stream<WarpStatusCode>   barrier_events[NUM_CUS];
    hls::stream<barrier_signal_t> barrier_signal[NUM_CUS];

    // Real csynth finding (SS16.6): DATAFLOW process-call arguments must
    // be plain declared variables or function arguments, not the result
    // of a method call like cu.programArray()/cu.regsArray() inlined
    // directly into the call - `[HLS 214-113]`, "either use an argument
    // of the function or declare the variable inside the dataflow loop
    // body". Declared here as real local references instead, one pair per
    // CU.
    instr_word_t (&program0)[MAX_PROGRAM_LEN] = cu0.programArray();
    instr_word_t (&program1)[MAX_PROGRAM_LEN] = cu1.programArray();
    reg_t (&regs0)[MAX_WARPS_PER_CU][MAX_THREADS_PER_WARP][NUM_REGS_PER_THREAD] = cu0.regsArray();
    reg_t (&regs1)[MAX_WARPS_PER_CU][MAX_THREADS_PER_WARP][NUM_REGS_PER_THREAD] = cu1.regsArray();
    // Same [HLS 214-113] rule, one more instance: cu_id_t(0)/cu_id_t(1) are
    // constructor-call expressions, not plain variables either.
    cu_id_t cu_id0 = 0;
    cu_id_t cu_id1 = 1;

    // SS16.37: the one genuinely new task - owns the single, real
    // BarrierState (barrier_arbiter.h has the full design reasoning) and
    // is the sole source of busy/done/fault, not either schedulerCore.
    // One instance regardless of NUM_CUS - it takes the whole NUM_CUS-
    // sized stream arrays itself, no per-CU duplication needed here.
    barrierCore(total_warps, start, busy, done, fault,
                barrier_events, barrier_signal);

    // initial_regs_ptr is shared, unmodified per CU (SS16: global-warp-id
    // indexed, not slot-indexed - each CU's compute_pipeline instance
    // already receives the correct global warp_id via its own dispatch,
    // itself derived from assignSlot()'s `w = cu_id + slot*NUM_CUS`
    // formula, so both CUs reading the same underlying DRAM buffer at
    // their own warp's offset is already correct with no change needed).
    // program_ptr is likewise broadcast, not routed (SS10.8) - each CU
    // independently loads its own full copy via its own loadProgram()
    // call inside its own schedulerCore instance.
    schedulerCore(cu0, cu_id0, program_ptr, program_len, total_warps, start,
                  dispatch_out0, status_in0,
                  barrier_events[0], barrier_signal[0]);
    schedulerCore(cu1, cu_id1, program_ptr, program_len, total_warps, start,
                  dispatch_out1, status_in1,
                  barrier_events[1], barrier_signal[1]);

    compute_pipeline(cu_id0, dispatch_out0,
                      program0, program_len,
                      regs0, initial_regs_ptr,
                      cu_mem_req[0], cu_mem_resp[0], status_in0);
    compute_pipeline(cu_id1, dispatch_out1,
                      program1, program_len,
                      regs1, initial_regs_ptr,
                      cu_mem_req[1], cu_mem_resp[1], status_in1);

    // Already NUM_CUS-generic (mem_arbiter.h's proven N:1/1:N array-of-
    // streams shape, docs/hls/interfaces.md SS10.9) - no change needed.
    mem_arbiter(cu_mem_req, cu_mem_resp, mem_req_out, mem_resp_in);
}

}  // namespace riscv_gpgpu_hls
