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

// Register loader for one cluster: seeds CLUSTER_SIZE CUs' register files from
// initial_regs_ptr and signals each schedulerCore when its CU is ready.
// Mirrors programLoader's Phase 2 but scoped to one cluster and one AXI port.
inline void clusterRegLoader(
    reg_t*    initial_regs_ptr,
    cu_id_t   base_cu_id,
    warp_id_t total_warps,
    warp_id_t warp_id_offset,
    bool&     start,
    hls::stream<reg_seed_t> (&seed_out)[CLUSTER_SIZE],
    hls::stream<bool>       (&loaded_out)[CLUSTER_SIZE]
) {
    bool     done      = false;
    uint8_t  seed_c    = 0;
    uint8_t  seed_slot = 0;
    uint32_t seed_i    = 0;

    while (true) {
#pragma HLS PIPELINE off
        if (!start) {
            done      = false;
            seed_c    = 0;
            seed_slot = 0;
            seed_i    = 0;
            continue;
        }
        if (!done) {
            if (seed_c < CLUSTER_SIZE) {
                warp_id_t global_cu = warp_id_t(int(base_cu_id) + seed_c);
                warp_id_t local_w   = global_cu + warp_id_t(seed_slot) * warp_id_t(NUM_CUS);
                if (local_w < total_warps) {
                    warp_id_t global_w = warp_id_offset + local_w;
                    uint64_t  base     = uint64_t(global_w) * MAX_THREADS_PER_WARP * NUM_REGS_PER_THREAD;
                    reg_seed_t s;
                    s.slot_id = slot_id_t(seed_slot);
                    s.flat_i  = ap_uint<10>(seed_i);
                    s.value   = initial_regs_ptr[base + seed_i];
                    seed_out[seed_c].write(s);
                }
                ++seed_i;
                if (seed_i >= uint32_t(MAX_THREADS_PER_WARP * NUM_REGS_PER_THREAD)) {
                    seed_i = 0;
                    ++seed_slot;
                    if (seed_slot >= uint8_t(MAX_WARPS_PER_CU)) {
                        seed_slot = 0;
                        loaded_out[seed_c].write(true);
                        ++seed_c;
                    }
                }
            } else {
                done = true;
                seed_c = 0; seed_slot = 0; seed_i = 0;
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
    hls::stream<reg_seed_t>        reg_seed[CLUSTER_SIZE];
    hls::stream<bool>              loaded_sig[CLUSTER_SIZE];

#pragma HLS STREAM variable=sched_events  depth=MAX_WARPS_PER_CU dim=1
#pragma HLS STREAM variable=sched_signal  depth=2                dim=1
#pragma HLS STREAM variable=dispatch_out  depth=2                dim=1
#pragma HLS STREAM variable=status_in     depth=2                dim=1
#pragma HLS STREAM variable=cu_mem_req    depth=2                dim=1
#pragma HLS STREAM variable=cu_mem_resp   depth=2                dim=1
#pragma HLS STREAM variable=reg_seed      depth=4                dim=1
#pragma HLS STREAM variable=loaded_sig    depth=1                dim=1

    clusterBarrierRelay(start, sched_events, cluster_events_out,
                        cluster_signal_in, sched_signal);

    clusterRegLoader(initial_regs_ptr, base_cu_id, total_warps, warp_id_offset,
                     start, reg_seed, loaded_sig);

    for (int c = 0; c < CLUSTER_SIZE; ++c) {
#pragma HLS UNROLL
        cu_id_t cu_id = cu_id_t(int(base_cu_id) + c);
        schedulerCore(cu[c], cu_id, program_len, total_warps, start,
                      dispatch_out[c], status_in[c],
                      sched_events[c], sched_signal[c],
                      warp_id_offset, loaded_sig[c]);
    }

    // Explicit register files per cluster-CU (same aliasing fix as gpgpu_top.cpp).
    reg_t cl_regs_0[MAX_WARPS_PER_CU][MAX_THREADS_PER_WARP][NUM_REGS_PER_THREAD];
#pragma HLS ARRAY_PARTITION variable=cl_regs_0 dim=2 complete
    compute_pipeline(cu_id_t(int(base_cu_id)+0), dispatch_out[0], cu[0].programArray(),
                     program_len, cl_regs_0, reg_seed[0],
                     cu_mem_req[0], cu_mem_resp[0], status_in[0]);
#if CLUSTER_SIZE >= 2
    reg_t cl_regs_1[MAX_WARPS_PER_CU][MAX_THREADS_PER_WARP][NUM_REGS_PER_THREAD];
#pragma HLS ARRAY_PARTITION variable=cl_regs_1 dim=2 complete
    compute_pipeline(cu_id_t(int(base_cu_id)+1), dispatch_out[1], cu[1].programArray(),
                     program_len, cl_regs_1, reg_seed[1],
                     cu_mem_req[1], cu_mem_resp[1], status_in[1]);
#endif
#if CLUSTER_SIZE >= 3
    reg_t cl_regs_2[MAX_WARPS_PER_CU][MAX_THREADS_PER_WARP][NUM_REGS_PER_THREAD];
#pragma HLS ARRAY_PARTITION variable=cl_regs_2 dim=2 complete
    compute_pipeline(cu_id_t(int(base_cu_id)+2), dispatch_out[2], cu[2].programArray(),
                     program_len, cl_regs_2, reg_seed[2],
                     cu_mem_req[2], cu_mem_resp[2], status_in[2]);
#endif
#if CLUSTER_SIZE >= 4
    reg_t cl_regs_3[MAX_WARPS_PER_CU][MAX_THREADS_PER_WARP][NUM_REGS_PER_THREAD];
#pragma HLS ARRAY_PARTITION variable=cl_regs_3 dim=2 complete
    compute_pipeline(cu_id_t(int(base_cu_id)+3), dispatch_out[3], cu[3].programArray(),
                     program_len, cl_regs_3, reg_seed[3],
                     cu_mem_req[3], cu_mem_resp[3], status_in[3]);
#endif
#if CLUSTER_SIZE >= 5
    reg_t cl_regs_4[MAX_WARPS_PER_CU][MAX_THREADS_PER_WARP][NUM_REGS_PER_THREAD];
#pragma HLS ARRAY_PARTITION variable=cl_regs_4 dim=2 complete
    compute_pipeline(cu_id_t(int(base_cu_id)+4), dispatch_out[4], cu[4].programArray(),
                     program_len, cl_regs_4, reg_seed[4],
                     cu_mem_req[4], cu_mem_resp[4], status_in[4]);
#endif
#if CLUSTER_SIZE >= 6
    reg_t cl_regs_5[MAX_WARPS_PER_CU][MAX_THREADS_PER_WARP][NUM_REGS_PER_THREAD];
#pragma HLS ARRAY_PARTITION variable=cl_regs_5 dim=2 complete
    compute_pipeline(cu_id_t(int(base_cu_id)+5), dispatch_out[5], cu[5].programArray(),
                     program_len, cl_regs_5, reg_seed[5],
                     cu_mem_req[5], cu_mem_resp[5], status_in[5]);
#endif
#if CLUSTER_SIZE >= 7
    reg_t cl_regs_6[MAX_WARPS_PER_CU][MAX_THREADS_PER_WARP][NUM_REGS_PER_THREAD];
#pragma HLS ARRAY_PARTITION variable=cl_regs_6 dim=2 complete
    compute_pipeline(cu_id_t(int(base_cu_id)+6), dispatch_out[6], cu[6].programArray(),
                     program_len, cl_regs_6, reg_seed[6],
                     cu_mem_req[6], cu_mem_resp[6], status_in[6]);
#endif
#if CLUSTER_SIZE >= 8
    reg_t cl_regs_7[MAX_WARPS_PER_CU][MAX_THREADS_PER_WARP][NUM_REGS_PER_THREAD];
#pragma HLS ARRAY_PARTITION variable=cl_regs_7 dim=2 complete
    compute_pipeline(cu_id_t(int(base_cu_id)+7), dispatch_out[7], cu[7].programArray(),
                     program_len, cl_regs_7, reg_seed[7],
                     cu_mem_req[7], cu_mem_resp[7], status_in[7]);
#endif

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
