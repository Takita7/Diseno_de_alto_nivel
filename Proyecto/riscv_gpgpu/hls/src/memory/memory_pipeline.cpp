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

// T024: per-board m_axi tuning (hls/config/{kv260,u55c}.h), selected the same
// way hls_config.h picks up RISCV_GPGPU_BOARD_KV260/U55C. These are macros,
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

namespace riscv_gpgpu_hls {

// Catches the burst-length macros drifting from the actual line size they're
// supposed to match (hls_config.h's WORDS_PER_LINE) - e.g. if WORDS_PER_LINE
// is ever resized without updating hls/config/{kv260,u55c}.h. A plain C++
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
        num_write_outstanding=RISCV_GPGPU_MAXI_NUM_WRITE_OUTSTANDING
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
#pragma HLS PIPELINE II=1
        mem_req_t req = req_in.read();
        resp_out.write(mem.handleRequest(req, global_mem));
    }
}

}  // namespace riscv_gpgpu_hls
