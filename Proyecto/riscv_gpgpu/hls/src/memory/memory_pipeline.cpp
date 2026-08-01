// memory_pipeline.cpp - T023: HLS-synthesizable port of MemoryHierarchy
//
// The actual load/store policy lives in MemorySubsystem (memory_pipeline.h),
// ported directly from MemoryHierarchy::loadWord()/storeWord(). This file is
// only the top-level kernel wrapper: a persistent MemorySubsystem instance
// serviced by an unbounded request/response loop (see memory_pipeline.h's
// file header for why this is `while (true)`, not a bounded loop - it mirrors
// a real, always-on memory subsystem, unlike compute_pipeline's
// once-per-warp invocation model).

#include "memory_pipeline.h"

// T024: per-board m_axi tuning (hls/config/kv260.h), selected the same
// way hls_config.h picks up RISCV_GPGPU_BOARD_KV260. These are macros,
// not constexpr - required so they substitute textually into the #pragma
// HLS INTERFACE m_axi line below (pragma argument parsing wants literal
// integers, not named C++ constants). Falls back to the values this file
// hardcoded before T024 (32/32, no outstanding-transaction tuning at all) if
// no board macro is defined, so existing csim tests are unaffected.
#ifndef RISCV_GPGPU_MAXI_MAX_READ_BURST_LEN
#define RISCV_GPGPU_MAXI_MAX_READ_BURST_LEN 32
#endif
#ifndef RISCV_GPGPU_MAXI_MAX_WRITE_BURST_LEN
#define RISCV_GPGPU_MAXI_MAX_WRITE_BURST_LEN 32
#endif
#ifndef RISCV_GPGPU_MAXI_NUM_READ_OUTSTANDING
#define RISCV_GPGPU_MAXI_NUM_READ_OUTSTANDING 2
#endif
#ifndef RISCV_GPGPU_MAXI_NUM_WRITE_OUTSTANDING
#define RISCV_GPGPU_MAXI_NUM_WRITE_OUTSTANDING 2
#endif
// docs/hls/interfaces.md SS16.29: real fix for a real, confirmed missed-
// widen finding (burst.xml: "threshold of 0", the Vivado IP flow's actual
// default) - 128 (KV260) matches Zynq UltraScale+'s real PL-side AXI HP/
// HPC port width. Falls back to 0 (disabled - prior behavior) if no board
// macro is defined.
#ifndef RISCV_GPGPU_MAXI_MAX_WIDEN_BITWIDTH
#define RISCV_GPGPU_MAXI_MAX_WIDEN_BITWIDTH 0
#endif

namespace riscv_gpgpu_hls {

// Catches the burst-length macros drifting from the actual line size they're
// supposed to match (hls_config.h's WORDS_PER_LINE) - e.g. if WORDS_PER_LINE
// is ever resized without updating hls/config/kv260.h. A plain C++
// constant comparison, not a pragma, so this is checked by ANY build
// (csim included), not just real synthesis.
static_assert(RISCV_GPGPU_MAXI_MAX_READ_BURST_LEN == WORDS_PER_LINE,
              "m_axi read burst length must match WORDS_PER_LINE (hls_config.h) - "
              "one cache line per burst, see memory_pipeline.h's loadWord()");
static_assert(RISCV_GPGPU_MAXI_MAX_WRITE_BURST_LEN == WORDS_PER_LINE,
              "m_axi write burst length must match WORDS_PER_LINE (hls_config.h)");

void memory_pipeline(
    hls::stream<mem_req_t>&  req_in,
    hls::stream<mem_resp_t>& resp_out,
    ap_uint<32>*             global_mem
) {
#pragma HLS INTERFACE axis      port=req_in
#pragma HLS INTERFACE axis      port=resp_out
#pragma HLS INTERFACE m_axi     port=global_mem offset=slave bundle=gmem \
        max_read_burst_length=RISCV_GPGPU_MAXI_MAX_READ_BURST_LEN \
        max_write_burst_length=RISCV_GPGPU_MAXI_MAX_WRITE_BURST_LEN \
        num_read_outstanding=RISCV_GPGPU_MAXI_NUM_READ_OUTSTANDING \
        num_write_outstanding=RISCV_GPGPU_MAXI_NUM_WRITE_OUTSTANDING \
        max_widen_bitwidth=RISCV_GPGPU_MAXI_MAX_WIDEN_BITWIDTH
#pragma HLS INTERFACE s_axilite port=global_mem bundle=control
#pragma HLS INTERFACE s_axilite port=return     bundle=control

    // Persists for the kernel's whole lifetime - real hardware never
    // "restarts" this state between requests; see memory_pipeline.h's
    // MemorySubsystem::reset()/invalidateCache() for the golden-model-mapped
    // reset semantics, not invoked here (nothing in the ported control flow
    // calls MemoryHierarchy::invalidateCache() either - see memory_pipeline.h).
    static MemorySubsystem mem;

MEMORY_PIPELINE_LOOP:
    while (true) {
        // Re-verified docs/hls/interfaces.md SS16.32: this comment's
        // original SS16.13 numbers (II=1 forcing 105 BRAM/128 URAM
        // fragmentation, a hard URAM overflow) no longer reproduce under
        // the current tool version/source - real re-test at II=1 with
        // today's source (16KB shared mem, L2 on URAM, outstanding=16/16,
        // widen=128) landed on 34 BRAM/16 URAM, identical to II=4. Kept at
        // II=4 anyway: zero cost either way today, and zero latency
        // benefit from II=1 regardless - the outer loop never actually
        // achieves the requested II (Pipelined=no either way), since its
        // body blocks on loadWord()/storeWord()'s own real latency
        // (90/13 cycles) before the next iteration can start - memory-
        // dependency-bound, not pipeline-scheduling-bound, matching the
        // original comment's conclusion for a different underlying reason.
        // II=4 kept as the documented, already-understood value rather
        // than switching to a functionally-identical alternative for no
        // reason.
#pragma HLS PIPELINE II=4
        mem_req_t req = req_in.read();
        resp_out.write(mem.handleRequest(req, global_mem));
    }
}

}  // namespace riscv_gpgpu_hls
