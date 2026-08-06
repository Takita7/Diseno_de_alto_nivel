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

// Per-board overrides (T024): selected by the build defining
// RISCV_GPGPU_BOARD_KV260 (a Vitis project C-flag, T025/T026 scope) before
// this header is included. Undefined (plain csim/testing, as in every
// tests/hls/*.cpp this project has) falls back to the KV260-sized defaults
// below, unchanged from before T024. KV260 is the sole target board
// (docs/hls/interfaces.md SS14) - an Alveo U55C branch existed briefly and
// was removed.
#if defined(RISCV_GPGPU_BOARD_KV260)
#include "../../config/kv260.h"
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
// Board-dependent (hls/config/kv260.h); falls back to KV260's 32 bits
// (4GB DDR4) if no board macro was defined.
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

// docs/hls/interfaces.md SS16.36: WAYS 2->4 (matching L2), real cost
// verified via synthesis - the SS16.35 attempt was reverted only to avoid
// an open-ended rework loop mid-session, not because of any real problem
// with the change itself; real BRAM budget (SS16.27) comfortably allows
// it. 16KB -> 32KB (SS16.35) unchanged here.
constexpr int L1_WAYS           = 4;
constexpr int L1_SIZE_BYTES     = 32 * 1024;
constexpr int L1_LINES_TOTAL    = L1_SIZE_BYTES / CACHE_LINE_BYTES;      // 256
constexpr int L1_SETS_PER_WAY   = L1_LINES_TOTAL / L1_WAYS;              // 64

// L2: shared across CUs, 256KB default (arch_config.yaml), WAYS=4
constexpr int L2_WAYS           = 4;
constexpr int L2_SIZE_BYTES     = 256 * 1024;
constexpr int L2_LINES_TOTAL    = L2_SIZE_BYTES / CACHE_LINE_BYTES;      // 2048
constexpr int L2_SETS_PER_WAY   = L2_LINES_TOTAL / L2_WAYS;              // 512

// Shared memory (scratchpad, not a cache - always resident, no tags)
// 1 -> 2 DECIDED (docs/hls/interfaces.md SS16.37), not a placeholder: the
// original NUM_CUS=1 call (SS10.11) was made when compute_pipeline alone
// was 52% of KV260's LUT budget - since fixed (SS16.16's ALLOCATION
// sharing, LUT 51%->37%), and the real BRAM budget itself was later found
// to be double what earlier sessions assumed (SS16.27). Real 2-CU support
// needed a genuinely new architectural piece - barrierCore
// (barrier_arbiter.h) - since the global barrier can't simply be
// duplicated per-CU the way schedulerCore's own WarpSlot[] correctly is.
// KV260 is the sole target board (SS14) - this is not a per-board decision.
#ifndef RISCV_GPGPU_NUM_CUS
#define RISCV_GPGPU_NUM_CUS 12
#endif
constexpr int NUM_CUS                    = RISCV_GPGPU_NUM_CUS;
// When NUM_CUS > 8, a flat DATAFLOW region would exceed the HLS tool's limit of
// ~40 backwards channels (barrier_events + status_in + cu_mem_resp each of size
// NUM_CUS gives 3*16=48, cutting off cu_mem_resp_8..15). Hierarchical DATAFLOW
// splits into CLUSTER_SIZE-CU sub-regions, each well within the limit.
constexpr int CLUSTER_SIZE               = (NUM_CUS > 8) ? (NUM_CUS / 2) : NUM_CUS;
constexpr int NUM_CLUSTERS               = (NUM_CUS > 8) ? 2 : 1;
// 48KB -> 16KB (docs/hls/interfaces.md SS16.26) -> 32KB (SS16.35): the
// 16KB step matched the golden model's real, live-executed default
// (compute_unit.h:28) and freed real BRAM for the 2-CU question
// (§16.20/16.25). This step doubles it again for more scratchpad headroom
// per kernel, now that real BRAM budget is well understood (SS16.27) and
// comfortably has room - a capability increase, not a fidelity match.
#ifndef RISCV_GPGPU_SHARED_MEM_SIZE_BYTES
#define RISCV_GPGPU_SHARED_MEM_SIZE_BYTES (32 * 1024)
#endif
constexpr int SHARED_MEM_SIZE_BYTES      = RISCV_GPGPU_SHARED_MEM_SIZE_BYTES;
constexpr int SHARED_MEM_WORDS_PER_CU    = SHARED_MEM_SIZE_BYTES / 4;

// ── On-chip scheduler (docs/hls/interfaces.md SS2.5) ──────────────────────────
// Per-CU resident-warp-slot capacity. Successor to an earlier "MAX_WARPS_PER_
// BLOCK" proposal (SS10.5, deprecated) - same value, same golden-model
// max_warps default origin, different role: a hard per-launch capacity
// ceiling (a kernel with total_warps > NUM_CUS*MAX_WARPS_PER_CU is an invalid
// launch, SS10.6's hazard mitigation), not a barrier-group size. Barrier
// scope itself is global, matching the golden model exactly (SS10.6).
// 4 -> 8 DECIDED (docs/hls/interfaces.md SS16.25), not a placeholder: real
// csynth showed doubling this costs zero extra BRAM/LUT/DSP/FF (the
// execution datapath isn't replicated per warp - warps are dispatched
// sequentially through the same shared hardware - and regs_'s existing
// 2-BRAM-per-lane allocation had headroom to absorb the doubled depth,
// 128->256, without needing a 3rd BRAM primitive). A real, felt increase in
// resident-warp capacity for free; chosen over adding a 2nd CU, which a
// real BRAM projection showed likely doesn't fit (~214/144).
#ifndef RISCV_GPGPU_MAX_WARPS_PER_CU
#define RISCV_GPGPU_MAX_WARPS_PER_CU 8
#endif
constexpr int MAX_WARPS_PER_CU           = RISCV_GPGPU_MAX_WARPS_PER_CU;

}  // namespace riscv_gpgpu_hls

#endif  // RISCV_GPGPU_HLS_CONFIG_H
