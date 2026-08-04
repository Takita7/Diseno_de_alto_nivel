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

    // One AXI master port per compute pipeline.
    // Both pointers may later reference the same physical DDR buffer.
    reg_t* initial_regs_ptr0,
    reg_t* initial_regs_ptr1,

    uint32_t      program_len,
    warp_id_t     total_warps,
    warp_id_t     warp_id_offset,
    bool          start,
    bool&         busy,
    bool&         done,
    bool&         fault,
    hls::stream<mem_req_t>&  mem_req_out,
    hls::stream<mem_resp_t>& mem_resp_in
) {
    // One AXI master owns program loading.
#pragma HLS INTERFACE m_axi \
    port=program_ptr \
    offset=slave \
    bundle=gmem0 \
    max_widen_bitwidth=RISCV_GPGPU_MAXI_MAX_WIDEN_BITWIDTH

    // IMPORTANT:
    // CU0 and CU1 use DIFFERENT AXI master bundles.
    // This avoids DATAFLOW multiple-process ownership of a single m_axi port.
#pragma HLS INTERFACE m_axi \
    port=initial_regs_ptr0 \
    offset=slave \
    bundle=gmem1 \
    max_widen_bitwidth=RISCV_GPGPU_MAXI_MAX_WIDEN_BITWIDTH

#pragma HLS INTERFACE m_axi \
    port=initial_regs_ptr1 \
    offset=slave \
    bundle=gmem2 \
    max_widen_bitwidth=RISCV_GPGPU_MAXI_MAX_WIDEN_BITWIDTH

#pragma HLS INTERFACE s_axilite port=program_len    bundle=control
#pragma HLS INTERFACE s_axilite port=total_warps    bundle=control
#pragma HLS INTERFACE s_axilite port=warp_id_offset bundle=control
#pragma HLS INTERFACE s_axilite port=start          bundle=control
#pragma HLS INTERFACE s_axilite port=busy           bundle=control
#pragma HLS INTERFACE s_axilite port=done           bundle=control
#pragma HLS INTERFACE s_axilite port=fault          bundle=control

#pragma HLS INTERFACE axis port=mem_req_out
#pragma HLS INTERFACE axis port=mem_resp_in

#pragma HLS DATAFLOW

    // ------------------------------------------------------------------
    // Per-CU state
    // ------------------------------------------------------------------

    CuDispatchUnit cu0;
    CuDispatchUnit cu1;

    hls::stream<warp_dispatch_t> dispatch_out0;
    hls::stream<warp_dispatch_t> dispatch_out1;

    hls::stream<warp_status_t> status_in0;
    hls::stream<warp_status_t> status_in1;

    hls::stream<mem_req_t>  cu_mem_req[NUM_CUS];
    hls::stream<mem_resp_t> cu_mem_resp[NUM_CUS];

    hls::stream<WarpStatusCode>   barrier_events[NUM_CUS];
    hls::stream<barrier_signal_t> barrier_signal[NUM_CUS];

    // ------------------------------------------------------------------
    // References to storage owned by each CuDispatchUnit
    // ------------------------------------------------------------------

    instr_word_t (&program0)[MAX_PROGRAM_LEN] =
        cu0.programArray();

    instr_word_t (&program1)[MAX_PROGRAM_LEN] =
        cu1.programArray();

    reg_t (&regs0)
        [MAX_WARPS_PER_CU]
        [MAX_THREADS_PER_WARP]
        [NUM_REGS_PER_THREAD] =
            cu0.regsArray();

    reg_t (&regs1)
        [MAX_WARPS_PER_CU]
        [MAX_THREADS_PER_WARP]
        [NUM_REGS_PER_THREAD] =
            cu1.regsArray();

    // Plain variables are required as DATAFLOW process-call arguments.
    cu_id_t cu_id0 = 0;
    cu_id_t cu_id1 = 1;

    // ------------------------------------------------------------------
    // Kernel-wide barrier controller
    // ------------------------------------------------------------------

    barrierCore(
        total_warps,
        start,
        busy,
        done,
        fault,
        barrier_events,
        barrier_signal
    );

    // ------------------------------------------------------------------
    // Program loading
    //
    // Only programLoader accesses program_ptr.
    // It broadcasts the same program to the two local CU program stores.
    // ------------------------------------------------------------------

    programLoader(
        program_ptr,
        program_len,
        start,
        program0,
        program1
    );

    // ------------------------------------------------------------------
    // CU schedulers
    // ------------------------------------------------------------------

    schedulerCore(
        cu0,
        cu_id0,
        program_len,
        total_warps,
        start,
        dispatch_out0,
        status_in0,
        barrier_events[0],
        barrier_signal[0],
        warp_id_offset
    );

    schedulerCore(
        cu1,
        cu_id1,
        program_len,
        total_warps,
        start,
        dispatch_out1,
        status_in1,
        barrier_events[1],
        barrier_signal[1],
        warp_id_offset
    );

    // ------------------------------------------------------------------
    // Compute pipelines
    //
    // IMPORTANT:
    //
    // Previously both instances received:
    //
    //     initial_regs_ptr
    //
    // which caused:
    //
    // [HLS 200-1013]
    // cannot read data in multiple processes
    //
    // CU0 now owns initial_regs_ptr0 / gmem1.
    // CU1 now owns initial_regs_ptr1 / gmem2.
    //
    // Both pointers may point to the SAME physical DDR address at runtime.
    // ------------------------------------------------------------------

    compute_pipeline(
        cu_id0,
        dispatch_out0,
        program0,
        program_len,
        regs0,
        initial_regs_ptr0,
        cu_mem_req[0],
        cu_mem_resp[0],
        status_in0
    );

    compute_pipeline(
        cu_id1,
        dispatch_out1,
        program1,
        program_len,
        regs1,
        initial_regs_ptr1,
        cu_mem_req[1],
        cu_mem_resp[1],
        status_in1
    );

    // ------------------------------------------------------------------
    // N:1 memory arbitration
    // ------------------------------------------------------------------

    mem_arbiter(
        cu_mem_req,
        cu_mem_resp,
        mem_req_out,
        mem_resp_in
    );
}

}  // namespace riscv_gpgpu_hls