// cu_cluster.h - Hierarchical DATAFLOW cluster for NUM_CUS > 8
//
// When NUM_CUS > 8, a flat DATAFLOW region in gpgpu_top.cpp would generate
// 3*NUM_CUS backwards channels (barrier_events + status_in + cu_mem_resp),
// exceeding the Vitis HLS tool limit of ~40 per DATAFLOW region (confirmed
// at NUM_CUS=16: tool generates only 8 of the required 16 cu_mem_resp
// backwards channels, leaving cu_mem_resp_8..15 with illegal RTL connections).
//
// The fix: split into CLUSTER_SIZE-CU sub-regions (cuCluster), each with
// 3*CLUSTER_SIZE <= 24 backwards channels. The top-level DATAFLOW sees only
// NUM_CLUSTERS process calls, contributing only 4 backwards channels total.
//
// Only compiled when RISCV_GPGPU_NUM_CUS >= 13.

#ifndef RISCV_GPGPU_HLS_CU_CLUSTER_H
#define RISCV_GPGPU_HLS_CU_CLUSTER_H

#if RISCV_GPGPU_NUM_CUS >= 13

#include <hls_stream.h>
#include "../common/hls_config.h"
#include "../common/hls_types.h"
#include "barrier_arbiter.h"
#include "cu_dispatch_unit.h"
#include "mem_arbiter.h"
#include "gpgpu_top.h"              // schedulerCore, programLoader
#include "../compute_unit/compute_pipeline.h"

namespace riscv_gpgpu_hls {

// Free-running relay between CLUSTER_SIZE per-CU scheduler barrier streams
// and the top-level barrierCore's single cluster-level streams.
// Runs as one DATAFLOW process inside cuCluster's DATAFLOW region.
//
// Call order: relay(1) → schedulers(2) → compute(3) → local_arb(4)
// Backwards in cluster DATAFLOW: sched_events(2→1), status_in(3→2),
// cu_mem_resp(4→3) = 3*CLUSTER_SIZE channels, all ≤ 40.
inline void clusterBarrierRelay(
    bool&                          start,
    hls::stream<WarpStatusCode>    (&sched_events)[CLUSTER_SIZE],
    hls::stream<WarpStatusCode>&   cluster_events_out,
    hls::stream<barrier_signal_t>& cluster_signal_in,
    hls::stream<barrier_signal_t>  (&sched_signal)[CLUSTER_SIZE]
) {
    while (true) {
#pragma HLS PIPELINE off
        // Forward per-CU warp events up to the top-level barrierCore.
    RELAY_EVENTS:
        for (int c = 0; c < CLUSTER_SIZE; ++c) {
#pragma HLS UNROLL
            if (!sched_events[c].empty())
                cluster_events_out.write(sched_events[c].read());
        }
        // Fan out the top-level release/done signal to all CU schedulers.
        if (!cluster_signal_in.empty()) {
            barrier_signal_t sig = cluster_signal_in.read();
        FANOUT_SIGNAL:
            for (int c = 0; c < CLUSTER_SIZE; ++c) {
#pragma HLS UNROLL
                sched_signal[c].write(sig);
            }
        }
    }
}

// Per-cluster DATAFLOW region: CLUSTER_SIZE schedulers + compute_pipelines
// + a local memory arbiter that merges CLUSTER_SIZE req streams into one
// cluster-level output and routes responses back.
//
// 'base_cu_id' is the global CU index of the first CU in this cluster
// (0 for cluster 0, CLUSTER_SIZE for cluster 1). It is used so that
// schedulerCore produces correct global warp assignments.
// Each cluster owns exactly one AXI master port to avoid DATAFLOW process
// merging that occurs when two process calls share the same m_axi bundle.
inline void cuCluster(
    CuDispatchUnit (&cu)[CLUSTER_SIZE],
    cu_id_t         base_cu_id,
    warp_id_t       total_warps,
    bool&           start,
    uint32_t        program_len,
    warp_id_t       warp_id_offset,
    reg_t*          initial_regs_ptr,
    hls::stream<WarpStatusCode>&   cluster_events_out,
    hls::stream<barrier_signal_t>& cluster_signal_in,
    hls::stream<mem_req_t>&        cluster_req_out,
    hls::stream<mem_resp_t>&       cluster_resp_in
) {
#pragma HLS DATAFLOW

    hls::stream<WarpStatusCode>    sched_events[CLUSTER_SIZE];
    hls::stream<barrier_signal_t>  sched_signal[CLUSTER_SIZE];
    hls::stream<warp_dispatch_t>   dispatch_out[CLUSTER_SIZE];
    hls::stream<warp_status_t>     status_in[CLUSTER_SIZE];
    hls::stream<mem_req_t>         cu_mem_req[CLUSTER_SIZE];
    hls::stream<mem_resp_t>        cu_mem_resp[CLUSTER_SIZE];

#pragma HLS STREAM variable=sched_events  depth=MAX_WARPS_PER_CU dim=1
#pragma HLS STREAM variable=sched_signal  depth=2                dim=1
#pragma HLS STREAM variable=dispatch_out  depth=2                dim=1
#pragma HLS STREAM variable=status_in     depth=2                dim=1
#pragma HLS STREAM variable=cu_mem_req    depth=2                dim=1
#pragma HLS STREAM variable=cu_mem_resp   depth=2                dim=1

    clusterBarrierRelay(start, sched_events, cluster_events_out,
                        cluster_signal_in, sched_signal);

    for (int c = 0; c < CLUSTER_SIZE; ++c) {
#pragma HLS UNROLL
        cu_id_t cu_id = cu_id_t(int(base_cu_id) + c);
        schedulerCore(cu[c], cu_id, program_len, total_warps, start,
                      dispatch_out[c], status_in[c],
                      sched_events[c], sched_signal[c],
                      warp_id_offset);
    }

    for (int c = 0; c < CLUSTER_SIZE; ++c) {
#pragma HLS UNROLL
        cu_id_t cu_id = cu_id_t(int(base_cu_id) + c);
        // Alternate ptr0/ptr1 per CU to satisfy DATAFLOW's one-reader-per-port
        // requirement (same pattern as the flat-DATAFLOW 2-CU original).
        compute_pipeline(cu_id, dispatch_out[c], cu[c].programArray(), program_len,
                         cu[c].regsArray(), initial_regs_ptr,
                         cu_mem_req[c], cu_mem_resp[c], status_in[c]);
    }

    mem_arbiter_n<CLUSTER_SIZE>(cu_mem_req, cu_mem_resp,
                                cluster_req_out, cluster_resp_in);
}

// 2-to-1 memory request mux / 1-to-2 response demux between the two
// cuCluster instances and the single memory_pipeline.
// Routes responses by cu_id: cu_id < CLUSTER_SIZE → cluster 0,
//                             cu_id >= CLUSTER_SIZE → cluster 1.
inline void superMemArbiter(
    hls::stream<mem_req_t>  cluster_req[NUM_CLUSTERS],
    hls::stream<mem_resp_t> cluster_resp[NUM_CLUSTERS],
    hls::stream<mem_req_t>&  mem_req_out,
    hls::stream<mem_resp_t>& mem_resp_in
) {
    static int next_cluster = 0;
    while (true) {
#pragma HLS PIPELINE II=1
    SUPER_ARB_REQ:
        for (int i = 0; i < NUM_CLUSTERS; ++i) {
#pragma HLS UNROLL
            int idx = (next_cluster + i) % NUM_CLUSTERS;
            if (!cluster_req[idx].empty()) {
                mem_req_out.write(cluster_req[idx].read());
                next_cluster = (idx + 1) % NUM_CLUSTERS;
                break;
            }
        }
        if (!mem_resp_in.empty()) {
            mem_resp_t resp = mem_resp_in.read();
            int cid = static_cast<int>(resp.cu_id);
            cluster_resp[cid < CLUSTER_SIZE ? 0 : 1].write(resp);
        }
    }
}

}  // namespace riscv_gpgpu_hls

#endif  // RISCV_GPGPU_NUM_CUS >= 13
#endif  // RISCV_GPGPU_HLS_CU_CLUSTER_H
