// gpgpu_top.cpp - definition of the real top-level "compute" IP
// (docs/hls/interfaces.md SS15/SS16.6). See gpgpu_top.h for why this is a
// real .cpp, not header-only `inline` like schedulerCore.

#include "gpgpu_top.h"

#if RISCV_GPGPU_NUM_CUS >= 13
#include "cu_cluster.h"   // cuCluster, superMemArbiter, clusterBarrierRelay
#endif

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

#if RISCV_GPGPU_NUM_CUS >= 13
    // ------------------------------------------------------------------
    // Hierarchical DATAFLOW path (NUM_CUS >= 13)
    //
    // Flat DATAFLOW with NUM_CUS > 8 hits the Vitis HLS ~40-backwards-
    // channel limit: 3 backwards arrays × NUM_CUS = 3×16=48 > 40.
    // The tool silently omits cu_mem_resp_8..15 causing "Illegal connection"
    // in RTL generation.
    //
    // Solution: split into NUM_CLUSTERS=2 cuCluster instances each
    // managing CLUSTER_SIZE=NUM_CUS/2 CUs.  Each cluster's internal
    // DATAFLOW has 3*CLUSTER_SIZE=24 backwards channels (well < 40).
    // The top-level DATAFLOW has only 4 backwards channels (2 cluster_events
    // + 2 cluster_signal from cuCluster→barrierCore and
    // superMemArbiter→cuCluster).
    // ------------------------------------------------------------------

    CuDispatchUnit cu_a[CLUSTER_SIZE];
    CuDispatchUnit cu_b[CLUSTER_SIZE];

    hls::stream<WarpStatusCode>   cluster_events[NUM_CLUSTERS];
    hls::stream<barrier_signal_t> cluster_signal[NUM_CLUSTERS];
    hls::stream<mem_req_t>        cluster_req[NUM_CLUSTERS];
    hls::stream<mem_resp_t>       cluster_resp[NUM_CLUSTERS];

#pragma HLS STREAM variable=cluster_events  depth=MAX_WARPS_PER_CU dim=1
#pragma HLS STREAM variable=cluster_signal  depth=2                dim=1
#pragma HLS STREAM variable=cluster_req     depth=2                dim=1
#pragma HLS STREAM variable=cluster_resp    depth=2                dim=1

    // Barrier controller sees NUM_CLUSTERS (=2) streams instead of NUM_CUS.
    barrierCoreN<NUM_CLUSTERS>(
        total_warps, start, busy, done, fault,
        cluster_events, cluster_signal
    );

    // Program loader writes to both cluster arrays.
    programLoaderHier(
        program_ptr, program_len, start, cu_a, cu_b
    );

    // Cluster A: CU 0 .. CLUSTER_SIZE-1
    // Cluster A owns gmem1, cluster B owns gmem2 — no shared AXI bundles.
    cuCluster(
        cu_a, cu_id_t(0), total_warps, start, program_len, warp_id_offset,
        initial_regs_ptr0,
        cluster_events[0], cluster_signal[0],
        cluster_req[0], cluster_resp[0]
    );

    cuCluster(
        cu_b, cu_id_t(CLUSTER_SIZE), total_warps, start, program_len, warp_id_offset,
        initial_regs_ptr1,
        cluster_events[1], cluster_signal[1],
        cluster_req[1], cluster_resp[1]
    );

    // 2-cluster memory router.
    superMemArbiter(
        cluster_req, cluster_resp,
        mem_req_out, mem_resp_in
    );

#else
    // ------------------------------------------------------------------
    // Flat DATAFLOW path (NUM_CUS <= 8)
    // 3 * NUM_CUS <= 24 backwards channels — within the ~40 tool limit.
    // ------------------------------------------------------------------

    CuDispatchUnit cu[NUM_CUS];

    hls::stream<warp_dispatch_t> dispatch_out[NUM_CUS];
    hls::stream<warp_status_t>   status_in[NUM_CUS];

    hls::stream<mem_req_t>  cu_mem_req[NUM_CUS];
    hls::stream<mem_resp_t> cu_mem_resp[NUM_CUS];

    hls::stream<WarpStatusCode>   barrier_events[NUM_CUS];
    hls::stream<barrier_signal_t> barrier_signal[NUM_CUS];

#pragma HLS STREAM variable=dispatch_out   depth=2              dim=1
#pragma HLS STREAM variable=status_in      depth=2              dim=1
#pragma HLS STREAM variable=cu_mem_req     depth=2              dim=1
#pragma HLS STREAM variable=cu_mem_resp    depth=2              dim=1
#pragma HLS STREAM variable=barrier_events depth=MAX_WARPS_PER_CU dim=1
#pragma HLS STREAM variable=barrier_signal depth=2              dim=1

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
        cu
    );

    // ------------------------------------------------------------------
    // CU schedulers
    // ------------------------------------------------------------------

    for (int c = 0; c < NUM_CUS; ++c) {
#pragma HLS UNROLL
        cu_id_t cu_id = cu_id_t(c);
        schedulerCore(
            cu[c],
            cu_id,
            program_len,
            total_warps,
            start,
            dispatch_out[c],
            status_in[c],
            barrier_events[c],
            barrier_signal[c],
            warp_id_offset
        );
    }

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

    for (int c = 0; c < NUM_CUS; ++c) {
#pragma HLS UNROLL
        cu_id_t cu_id = cu_id_t(c);
        reg_t* initial_regs_ptr = (c % 2 == 0) ? initial_regs_ptr0 : initial_regs_ptr1;
        instr_word_t (&program_c)[MAX_PROGRAM_LEN] = cu[c].programArray();
        reg_t (&regs_c)
            [MAX_WARPS_PER_CU]
            [MAX_THREADS_PER_WARP]
            [NUM_REGS_PER_THREAD] =
                cu[c].regsArray();

        compute_pipeline(
            cu_id,
            dispatch_out[c],
            program_c,
            program_len,
            regs_c,
            initial_regs_ptr,
            cu_mem_req[c],
            cu_mem_resp[c],
            status_in[c]
        );
    }

    // ------------------------------------------------------------------
    // N:1 memory arbitration
    // ------------------------------------------------------------------

    mem_arbiter(
        cu_mem_req,
        cu_mem_resp,
        mem_req_out,
        mem_resp_in
    );

#endif  // RISCV_GPGPU_NUM_CUS >= 13
}

}  // namespace riscv_gpgpu_hls
