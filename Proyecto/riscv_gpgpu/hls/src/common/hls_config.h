// hls_config.h - Fixed-size parameters for the HLS-synthesizable port
//
// Every value here is a compile-time constant: HLS requires static bounds
// (array sizes, loop trip counts, stream depths) where the SystemC golden
// model used dynamically-sized STL containers. Defaults below come from
// config/arch_config.yaml and docs/hls/interfaces.md SS5's sizing procedure;
// several are still open decisions (see docs/hls/interfaces.md SS6) and are
// flagged inline - do not treat these as final.
//
// Golden reference for every default: models/systemc/src/{compute_unit,
// simt_controller,memory}/*.{h,cpp}, config/arch_config.yaml.

#ifndef RISCV_GPGPU_HLS_CONFIG_H
#define RISCV_GPGPU_HLS_CONFIG_H

#include <cstdint>

// Per-board overrides (T024): selected by the build defining exactly one of
// RISCV_GPGPU_BOARD_KV260 / RISCV_GPGPU_BOARD_U55C (a Vitis project C-flag,
// T025/T026 scope) before this header is included. Neither defined (plain
// csim/testing, as in every tests/hls/*.cpp this project has) falls back to
// the KV260-sized defaults below, unchanged from before T024.
#if defined(RISCV_GPGPU_BOARD_KV260)
#include "../../config/kv260.h"
#elif defined(RISCV_GPGPU_BOARD_U55C)
#include "../../config/u55c.h"
#endif

namespace riscv_gpgpu_hls {

// ── compute_pipeline (per docs/hls/interfaces.md SS2.3) ──────────────────────
constexpr int MAX_THREADS_PER_WARP  = 32;   // SIMT width, fixed (matches WarpContext/ComputeUnit)
constexpr int NUM_REGS_PER_THREAD   = 32;   // matches WarpContext::regs[t].size() == 32

// OPEN DECISION (docs/hls/interfaces.md SS6.2): not yet sized against a real
// kernel corpus. 256 is a placeholder headroom guess (golden model's
// kernel_programs.h kernels are ~6-12 instructions); revisit once real
// benchmark kernels (T034) exist.
constexpr int MAX_PROGRAM_LEN       = 256;

constexpr int MAX_DIVERGENCE_DEPTH  = 8;    // matches SIMTController::DivergenceStack budget

// MAX_CONCURRENT_BARRIERS removed (docs/hls/interfaces.md SS2.5.4): sized
// the old host-orchestrated design's per-barrier_id arrival table (SS2.4,
// superseded). BarrierArbiter keeps one running arrival counter per
// kernel instead - grep-verified zero consumers of the old constant
// anywhere in hls/ or tests/hls/ before removing it.

// ── Address width (per docs/hls/interfaces.md SS4) ───────────────────────────
// Board-dependent (hls/config/{kv260,u55c}.h); falls back to KV260's 32 bits
// (4GB DDR4) if no board macro was defined. U55C's HBM binding (single vs.
// multiple pseudo-channels) is still a T025/T026 link-time decision that may
// need hls/config/u55c.h's value widened - re-check before U55C bring-up.
#ifndef RISCV_GPGPU_ADDR_BITS
#define RISCV_GPGPU_ADDR_BITS 32
#endif
constexpr int ADDR_BITS = RISCV_GPGPU_ADDR_BITS;

// ── Memory hierarchy (per docs/hls/interfaces.md SS3, arch_config.yaml) ──────
// Golden model's cache_line_size (128B, arch_config.yaml) was declared but
// never actually used by MemoryHierarchy (grep-verified: it caches individual
// words in a std::map, not lines) - effectively a fully-associative,
// unbounded, word-granularity cache. This HLS port uses REAL cache lines for
// m_axi burst efficiency, which means hit/miss rates will differ from the
// golden model even where functional results (register file contents) match
// bit-exact. See docs/hls/interfaces.md SS7 - hit-rate parity is not the
// correctness bar, functional (data) parity is.
constexpr int CACHE_LINE_BYTES  = 128;
constexpr int WORDS_PER_LINE    = CACHE_LINE_BYTES / 4;   // 32 words/line

// L1: per-CU, 16KB default (arch_config.yaml), WAYS=2 (SS6.3 proposed default)
constexpr int L1_WAYS           = 2;
constexpr int L1_SIZE_BYTES     = 16 * 1024;
constexpr int L1_LINES_TOTAL    = L1_SIZE_BYTES / CACHE_LINE_BYTES;      // 128
constexpr int L1_SETS_PER_WAY   = L1_LINES_TOTAL / L1_WAYS;              // 64

// L2: shared across CUs, 256KB default (arch_config.yaml), WAYS=4
constexpr int L2_WAYS           = 4;
constexpr int L2_SIZE_BYTES     = 256 * 1024;
constexpr int L2_LINES_TOTAL    = L2_SIZE_BYTES / CACHE_LINE_BYTES;      // 2048
constexpr int L2_SETS_PER_WAY   = L2_LINES_TOTAL / L2_WAYS;              // 512

// Shared memory (scratchpad, not a cache - always resident, no tags)
// DECIDED (docs/hls/interfaces.md SS10.11), not a placeholder: measured from
// real T020 csynth reports, compute_pipeline alone is 52% of KV260's LUT
// budget; two instances + memory_pipeline projects to 111% - infeasible.
// NUM_CUS=1 is KV260's real target until compute_pipeline's LUT footprint is
// optimized down. U55C is still genuinely open (no device support installed
// in this environment to measure against - SS10.11).
constexpr int NUM_CUS                    = 1;
constexpr int SHARED_MEM_SIZE_BYTES      = 48 * 1024;   // arch_config.yaml default
constexpr int SHARED_MEM_WORDS_PER_CU    = SHARED_MEM_SIZE_BYTES / 4;

// ── On-chip scheduler (docs/hls/interfaces.md SS2.5) ──────────────────────────
// Per-CU resident-warp-slot capacity. Successor to an earlier "MAX_WARPS_PER_
// BLOCK" proposal (SS10.5, deprecated) - same value, same golden-model
// max_warps default origin, different role: a hard per-launch capacity
// ceiling (a kernel with total_warps > NUM_CUS*MAX_WARPS_PER_CU is an invalid
// launch, SS10.6's hazard mitigation), not a barrier-group size. Barrier
// scope itself is global, matching the golden model exactly (SS10.6).
constexpr int MAX_WARPS_PER_CU           = 4;

}  // namespace riscv_gpgpu_hls

#endif  // RISCV_GPGPU_HLS_CONFIG_H
