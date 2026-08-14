// mem_arbiter.h - HLS-synthesizable N:1 request / 1:N response router
// between NUM_CUS compute_pipeline instances and the single memory_pipeline
//
// Design and justification: docs/hls/interfaces.md SS10.9. memory_pipeline
// itself needs no changes (confirmed there by reading its real
// implementation - it already tags every response with the requesting
// cu_id, and its own storage is already banked per CU where banking
// matters). This class is pure plumbing outside both existing blocks.
//
// Two properties (both confirmed against real code, SS10.9) make this
// simple: compute_pipeline's executeMemOp() never has more than one
// outstanding request per CU (it blocks on each response before issuing
// the next), and memory_pipeline processes exactly one request at a time,
// strictly in order. Together: responses come back in exactly the order
// requests were accepted, so no reordering/outstanding-request tracking is
// needed here - just a round-robin request mux and a cu_id-keyed response
// demux.
//
// Follows the same class-plus-free-running-wrapper shape memory_pipeline.h
// already uses (MemorySubsystem::handleRequest() + the free-running
// memory_pipeline() kernel): a bounded, directly-testable core
// (arbitrateOnce()/routeResponse()) wrapped by a persistent-hardware
// mem_arbiter() top-level kernel.

#ifndef RISCV_GPGPU_HLS_MEM_ARBITER_H
#define RISCV_GPGPU_HLS_MEM_ARBITER_H

#include <hls_stream.h>
#include "../common/hls_config.h"
#include "../common/hls_types.h"

namespace riscv_gpgpu_hls {

template<int N>
class MemArbiter {
public:
    // One arbitration step: round-robin scan starting just past the last
    // serviced CU, forward the first non-empty request stream found to
    // mem_req_out untouched (cu_id is already set by the sending
    // compute_pipeline instance - nothing here needs to tag it). No-op,
    // returns false, if every input stream is currently empty.
    bool arbitrateOnce(hls::stream<mem_req_t> req_in[N],
                        hls::stream<mem_req_t>& mem_req_out) {
    ARBITRATE_SCAN:
        for (int i = 0; i < N; ++i) {
            int idx = (next_cu_ + i) % N;
            if (!req_in[idx].empty()) {
                mem_req_out.write(req_in[idx].read());
                next_cu_ = (idx + 1) % N;
                return true;
            }
        }
        return false;
    }

    // Response routing: read one response from memory_pipeline's resp_out
    // (if any) and forward it to the matching CU's response stream, keyed
    // by (cu_id % N) to support both flat (base=0) and cluster-relative
    // (base=CLUSTER_SIZE) addressing. No-op, returns false, if mem_resp_in
    // is currently empty.
    bool routeResponse(hls::stream<mem_resp_t>& mem_resp_in,
                        hls::stream<mem_resp_t> resp_out[N]) {
        if (mem_resp_in.empty()) return false;
        mem_resp_t resp = mem_resp_in.read();
        resp_out[int(resp.cu_id) % N].write(resp);
        return true;
    }

private:
    int next_cu_ = 0;   // round-robin pointer over [0, N)
};

// Backward-compat alias: existing tests use MemArbiter directly.
using MemArbiterFlat = MemArbiter<NUM_CUS>;

// Free-running top-level kernel - same persistent-hardware model
// memory_pipeline (docs/hls/interfaces.md SS3.3) already uses. Services
// both directions every pass without blocking on either (both steps are
// internally non-blocking, per their empty() guards above).
template<int N>
inline void mem_arbiter_n(
    hls::stream<mem_req_t>  req_in[N],
    hls::stream<mem_resp_t> resp_out[N],
    hls::stream<mem_req_t>&  mem_req_out,
    hls::stream<mem_resp_t>& mem_resp_in
) {
    static MemArbiter<N> arb;
    while (true) {
#pragma HLS PIPELINE II=1
        arb.arbitrateOnce(req_in, mem_req_out);
        arb.routeResponse(mem_resp_in, resp_out);
    }
}

// Convenience wrapper keeping the existing call sites unchanged.
inline void mem_arbiter(
    hls::stream<mem_req_t>  req_in[NUM_CUS],
    hls::stream<mem_resp_t> resp_out[NUM_CUS],
    hls::stream<mem_req_t>&  mem_req_out,
    hls::stream<mem_resp_t>& mem_resp_in
) {
    mem_arbiter_n<NUM_CUS>(req_in, resp_out, mem_req_out, mem_resp_in);
}

}  // namespace riscv_gpgpu_hls

#endif  // RISCV_GPGPU_HLS_MEM_ARBITER_H
