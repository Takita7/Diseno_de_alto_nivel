# HLS Interface Contracts

**Task**: T021 (`specs/001-open-riscv-gpgpu/tasks.md`, Phase 4 / User Story 2)
**Status**: Draft v3 — supersedes the `Current_Work/interfaces.md` "on-chip only, no
`m_axi`" draft (v2), which was a wrong turn: it dropped external memory access
entirely, contradicting the golden model's own configuration
(`config/arch_config.yaml`: `global_memory_size: 0  # 0 means unlimited/external`)
and `MemoryHierarchy`'s read/write-fill path in
`models/systemc/src/memory/memory_hierarchy.cpp`, which always treats "global
memory" as a backing store behind the caches, not a bounded array. This draft
restores `m_axi` for that backing store.
**Golden reference**: `models/systemc/src/compute_unit/`, `simt_controller/`,
`memory/`, `top/` (see `docs/architecture/interfaces.md` for the SystemC-level
interfaces this document translates into synthesizable ports).

---

## 1. Scope and design decision

Two independently synthesizable IP blocks, `compute_pipeline` (§2) and
`memory_pipeline` (§3), connected by an explicit streaming interface rather than
one fused kernel. This keeps each block's resource footprint separately
measurable — important because `memory_pipeline` now carries both an on-chip
cache (BRAM) footprint *and* an `m_axi` interface, two separately-tunable knobs.

**Board scope unchanged**: KV260 and Alveo U55C only (AU15P out of scope, per
team decision — unrelated to the memory-architecture correction this draft
makes).

**What this draft corrects from v2:**
- **`m_axi` is back.** `memory_pipeline` has a real external-memory port backing
  the L2 miss path. Global memory is not a third on-chip tier; it is the actual
  external DDR/HBM the golden model's `global_memory_` map represents.
- **On-chip BRAM is now scoped to caches only**: shared memory (per-CU
  scratchpad), L1 (per-CU), L2 (shared). None of them are sized to hold the
  *entire* addressable memory anymore — they're sized as caches, with normal
  cache miss/fill/writeback behavior against `m_axi`.
- **Per-board external memory differs in kind, not just size**: KV260 reaches
  DDR4 through the PS's HP/HPC AXI ports (shared with the ARM cores, see
  `hls/constraints/kv260.tcl`); **U55C has no DDR at all** — it's HBM2-only, 16GB
  over up to 32 pseudo-channels (see `hls/constraints/u55c.tcl`). `memory_pipeline`'s
  `m_axi` port binds to whichever the board provides; this is a link-time
  decision (`v++ --connectivity.sp` for U55C), not an HLS C-synthesis-time one.

---

## 2. Top-level kernel: `compute_pipeline`

Unchanged in structure and unaffected by the memory correction — it never
touches memory directly, only issues requests to `memory_pipeline` over a
stream.

### 2.1 Responsibility
Ports `ComputeUnit::executeWarp()` + `SIMTController` (branch/join/barrier). One
warp per invocation, matching the SystemC model's per-warp granularity.

### 2.2 Signature
```cpp
void compute_pipeline(
    cu_id_t          cu_id,             // s_axilite - ADDED during T022: needed to
                                         // route mem_req_t/mem_resp_t back to the
                                         // right compute_pipeline instance once
                                         // memory_pipeline is shared across
                                         // multiple concurrent CUs (hls_types.h's
                                         // mem_req_t/mem_resp_t already carried
                                         // this field; the signature just hadn't
                                         // caught up yet)
    warp_id_t        warp_id,          // s_axilite
    thread_mask_t    active_mask_init, // s_axilite
    uint32_t         program_len,      // s_axilite

    hls::stream<instr_word_t>&  instr_in,      // ap_fifo / axis

    reg_t   regs[MAX_THREADS_PER_WARP][32],    // bram, ap_memory

    hls::stream<mem_req_t>&  mem_req_out,      // axis, to memory_pipeline
    hls::stream<mem_resp_t>& mem_resp_in,      // axis, from memory_pipeline

    hls::stream<warp_status_t>& status_out     // axis, WARP_COMPLETE | WARP_STALLED_AT_BARRIER(bid)
);
```

**Host contract for barrier resume** (implementation detail, not a new
decision): since `compute_pipeline` is stateless between invocations except
through `regs[][]` and whatever the host streams into `instr_in`, resuming a
warp after `WARP_STALLED_AT_BARRIER` means the host re-invokes with the same
`warp_id`/`cu_id`, the *same* `regs[][]` array (untouched since the stall -
`compute_pipeline` never resets it, matching `ComputeUnit::executeWarp()`
never resetting `ctx.regs` either), and `instr_in` fed starting at the
instruction immediately after the `BARRIER` that stalled it. This mirrors
`top.cpp`'s `simulationProcess()`, which saves the whole `WarpContext`
(registers + `pc` already advanced past `BARRIER`) and replays it unchanged.

**Host register setup contract, updated**: `GPGPUTop::buildWarpContext()`
(upstream commit `9c4dfea`) now also sets `regs[t][3] = local_warp_id` (the
warp's index within its kernel launch: 0, 1, 2...), alongside the existing
`r0=0`/`r1=global_tid`/`r2=unique address` convention. `compute_pipeline`
itself doesn't care what's in `regs[][]` before it runs — this is purely a
host-side setup responsibility, same as r0-r2 always were — but any host
code (or test) launching a kernel that reads r3 (e.g.
`kernels::parallelReduction()`, see §9) must set it. `tests/hls/
test_compute_pipeline.cpp`'s `initRegs()` and `test_pipeline_integration.cpp`
were updated to set it (default 0 for single-warp tests, matching the
zero-fill that already made single-warp tests pass by accident before this
convention existed).

**Memory contract for `mem_req_t`/`mem_resp_t`** (needed by T023, stated here
since `compute_pipeline` already assumes it): every `mem_req_t` gets exactly
one `mem_resp_t` back, including stores (`data` field don't-care on a store
response) - keeps request/response accounting symmetric so
`compute_pipeline`'s per-lane blocking read/write never has to special-case
store vs. load completion.

### 2.3 Fixed-size parameters
`MAX_THREADS_PER_WARP=32`, `MAX_PROGRAM_LEN` (still open, §6),
`MAX_DIVERGENCE_DEPTH=8`, `MAX_CONCURRENT_BARRIERS=4`. Per-lane execution
unrolled (`#pragma HLS UNROLL` over the 32-lane loop).

### 2.4 Barrier handling: host-orchestrated (not an on-chip barrier unit)

**Updated** — this section originally justified the host-orchestrated
decision partly by observing that the golden model "never exercises more
than one CU concurrently." That premise is no longer true: upstream commit
`9c4dfea` ("GPGPU READY") rewrote `GPGPUTop::simulationProcess()`
(`models/systemc/src/top/top.cpp`) to genuinely fan out across every compute
unit each iteration (`for cu_id in 0..num_compute_units: selectWarp(cu_id)`)
and resolve barriers **globally** — a barrier now fires when
`barrier_queue.size() == total_warps_` summed across *all* CUs, not per-CU.
The golden model is a real reference for concurrent multi-CU execution now,
where before it wasn't.

This does **not** change the decision below, because the contract was
already written to be CU-count-agnostic — re-stated here so the reasoning
holds without leaning on the now-outdated premise:

`compute_pipeline` returns a `WARP_STALLED_AT_BARRIER(bid)` status and its
full register state; the host (PS on KV260, XRT host app on U55C) tracks
arrivals per `barrier_id` across **all outstanding warps for the kernel**,
regardless of which `cu_id` reported them, and re-invokes once every warp has
arrived — this is exactly what `simulationProcess()` does today (global
`barrier_queue`, not per-CU), so the port's fidelity to the golden model is
if anything *closer* now than when this section was first written. What's
still deliberately out of scope for this milestone is *hardware* (not host
software) resolving the barrier — a real on-chip, multi-CU-aware barrier
unit — since that's a new piece of hardware design, not a port of anything
the golden model does (the golden model's global barrier check is still a
software loop, just one that now spans multiple `ComputeUnit` C++ objects
instead of one). Tracked as a stretch goal once the host-orchestrated path is
validated end-to-end.

**Verification note**: `tests/hls/test_pipeline_integration.cpp`'s
`ParallelReductionAcrossTwoWarpsWithBarrier` test is the first HLS-side test
to actually exercise this cross-warp contract (2 warps, both must stall at
the same `bid` before either resumes) — see §9's kernel coverage table.

---

## 3. Top-level kernel: `memory_pipeline` (corrected — on-chip caches + m_axi)

### 3.1 Responsibility
Ports `MemoryHierarchy::loadWord`/`storeWord`. Three on-chip cache tiers
(shared memory, L1, L2) plus a real external-memory port for what the golden
model calls "global memory":

| SystemC tier | HLS storage | Rationale |
|---|---|---|
| Shared memory (`std::vector<uint8_t>`, per-CU scratchpad) | **BRAM**, banked per compute unit | Single-cycle, per-lane-parallel access — not a cache, always resident |
| L1 (`std::map`, per-CU) | **BRAM**, N-way set-associative (§3.2) | Low-latency cache in front of L2 |
| L2 (`std::map`, shared across CUs) | **BRAM**, N-way set-associative, larger, shared | Second cache tier in front of external memory |
| Global memory (`std::map`, backed by nothing further in the *simulation* — but semantically external per `arch_config.yaml`) | **External DDR/HBM via `m_axi`** | Real backing store. L2 miss → burst read over `m_axi`; write-through writes go out over `m_axi` too (matches the golden model's write-through, no-write-allocate policy, `memory_hierarchy.cpp:112-127`) |

### 3.2 Cache microarchitecture: N-way set-associative, built from N parallel direct-mapped banks

Per user direction: do not implement a single fully-associative (CAM-like)
structure. Instead, each cache (L1 per-CU, L2 shared) is `WAYS` parallel banks;
**each bank, individually, is a plain direct-mapped array** (its own tag BRAM +
data BRAM, indexed by `(address >> line_shift) mod SETS_PER_WAY`). Associativity
comes from checking all `WAYS` banks in parallel (independent BRAM read ports →
free parallelism, no arbitration), not from a wider per-bank structure. This is
the standard way HLS/RTL caches get set-associativity cheaply:

```cpp
// Per cache (L1 or L2), WAYS instances of this pair:
static ap_uint<TAG_BITS>  tag [WAYS][SETS_PER_WAY];   // BRAM
static ap_uint<32>        data[WAYS][SETS_PER_WAY];   // BRAM
static bool               valid[WAYS][SETS_PER_WAY];  // BRAM (or packed into tag width)

// Lookup: WAYS parallel comparisons (unrolled), one per bank/way
bool hit = false; int hit_way = -1;
#pragma HLS UNROLL
for (int w = 0; w < WAYS; ++w) {
    if (valid[w][set_index] && tag[w][set_index] == addr_tag) { hit = true; hit_way = w; }
}
```

Replacement policy: **round-robin per set** across the `WAYS` banks (a
per-set `ap_uint` next-victim-way counter). Plain PLRU/true-LRU is not worth the
extra BRAM/logic at the way-counts this design needs (§3.4) — round-robin is
the standard cheap choice here and keeps every bank's access pattern identical
(needed for the parallel-bank structure to stay simple to verify).

`WAYS` is a separate tunable per tier (L1 vs L2) and per board — see §3.4. This
replaces `Current_Work/interfaces.md`'s `BIND_STORAGE`-tagged flat arrays with
this two-level (bank × set) structure; the `BIND_STORAGE` pragma still applies
per-bank (T024), just to more, smaller arrays now.

### 3.3 Signature (with m_axi)
```cpp
void memory_pipeline(
    hls::stream<mem_req_t>&  req_in,     // axis, from compute_pipeline
    hls::stream<mem_resp_t>& resp_out,   // axis, to compute_pipeline

    ap_uint<32>* global_mem              // m_axi, board-specific target:
                                          //   KV260: HP/HPC port -> PS DDR4
                                          //   U55C:  bound to one HBM pseudo-channel
                                          //          at v++ link time (T025/T026)
);
// #pragma HLS INTERFACE m_axi port=global_mem offset=slave bundle=gmem \
//     max_read_burst_length=<cache_line_words> max_write_burst_length=<cache_line_words>
// #pragma HLS INTERFACE s_axilite port=global_mem bundle=control
```
Internally: `shared_mem[NUM_CUS][SHARED_MEM_WORDS_PER_CU]` (BRAM, unchanged from
v2) plus the L1/L2 bank arrays from §3.2. No on-chip array stands in for global
memory anymore — misses that fall through L1 and L2 issue burst
reads/writes to `global_mem` over `m_axi`, sized to the cache line
(`cache_line_size`, currently 128B / `arch_config.yaml`).

### 3.4 Sizing: caches only, not the whole address space
Because global memory is external again, on-chip budget (§5) only needs to
cover shared memory + L1 banks + L2 banks — a much smaller ask than v2's
"fit everything on-chip" framing. This is the main practical win of the
correction: KV260's ~3.3MB on-chip budget no longer hard-bounds total
addressable memory, only cache capacity and hit rate.

---

## 4. Data type mapping

Unchanged from v1/v2 — `ap_uint<32>` registers, `ap_uint<6>` opcodes, packed
`Instruction`, etc. — with `ADDR_BITS` reverting to cover each board's real
external address space (not an on-chip capacity as v2 had it):

| SystemC | HLS | Notes |
|---|---|---|
| `Address` (`uint64_t`) | `ap_uint<ADDR_BITS>` | KV260: 32 bits (4GB DDR4, `kv260.tcl`). U55C: sized to whatever HBM pseudo-channel(s) `global_mem` is bound to at link time — 28 bits for one 256MB PC, wider if multiple PCs are interleaved. Finalize once T025/T026 pick the binding. |

---

## 5. Per-board on-chip memory budget (caches only now)

| | **KV260 (XCK26)** | **Alveo U55C (XCU55C)** |
|---|---|---|
| Block RAM (36Kb blocks) | 144 blocks (≈ 5.06 Mb raw) | ≈ 70.9 Mb total |
| UltraRAM (288Kb blocks) | 64 blocks (≈ 18 Mb raw) | ≈ 270.0 Mb total |
| Combined on-chip (vendor-quoted) | ≈ 26.6 Mb (~3.3 MB) | ≈ 43 MB |
| External memory via `m_axi` | 4GB DDR4 (shared w/ PS, via HP/HPC) | 16GB HBM2 (no DDR), up to 32×256MB pseudo-channels |
| Practical implication | Still size caches (shared mem + L1 + L2 banks) here first — plenty of headroom now that global memory isn't competing for the same budget | HBM's raw bandwidth (460GB/s aggregate) is the more interesting knob than capacity here; consider whether wider/more `m_axi` ports (multiple HBM PCs) beat a single port once bandwidth is measured |

**Sizing procedure:**
1. Pick a starting `NUM_CUS` and `MAX_THREADS_PER_WARP=32` (fixed).
2. Pick `WAYS` for L1 and L2 (start with something small and power-of-2, e.g.
   L1 `WAYS=2`, L2 `WAYS=4` — revisit after C-synthesis resource reports).
3. Budget shared-memory-per-CU and L1/L2 total capacity against
   `arch_config.yaml`'s existing defaults (48KB shared, 16KB L1, 256KB L2) as a
   starting point — these were sized for the golden model's untimed
   `std::map` caches, so hit-rate behavior needs to be re-checked against the
   real bank/set/way structure, not assumed to carry over unchanged.
4. Run C-synthesis, check actual BRAM utilization against §5's ceilings, iterate.
5. Separately, tune `m_axi` burst length / outstanding transactions against
   each board's external memory (DDR4 latency/bandwidth on KV260, HBM2 on
   U55C) — this is a new tuning axis v2 didn't have at all.

---

## 6. Open decisions requiring team sign-off before T022/T023 begin

1. **Structural split (two streaming-connected kernels)** — confirmed, unchanged.
2. **`MAX_PROGRAM_LEN`** — still open.
3. **`WAYS` for L1 and L2** — proposed defaults above (L1=2, L2=4); not yet
   validated against real resource/timing numbers.
4. **`m_axi` binding specifics**: KV260's HP vs HPC port choice (HPC is
   cache-coherent with the PS, HP is not — matters if the PS ever touches the
   same memory concurrently); U55C's pseudo-channel count/interleaving.
   Tracked for T025/T026 but affects `ADDR_BITS` (§4) now.
5. **Barrier scope** (per-GPU vs. global) — still open, independent of this
   correction.
6. **Round-robin vs. any smarter replacement policy** for the cache banks
   (§3.2) — round-robin proposed as the default; revisit only if hit-rate
   parity testing (§7) against the golden model shows it matters.
7. **`MAX_DIVERGENCE_DEPTH=8` may be undersized.** Discovered while building
   the HLS-safe divergence stack (`hls/src/simt_controller/divergence_stack.h`):
   `handleBranch` only ever splits *currently active* lanes (matches
   `SIMTController::handleBranch`'s golden behavior exactly), so depth is
   bounded by source nesting, not `log2(32)=5` — a "peel off one lane at a
   time" pattern (a chain of `if (tid==0) ... else if (tid==1) ... else`,
   equally expressible in the golden model) can push up to 31 times for a
   32-lane warp before a single join, comfortably overflowing a depth of 8.
   The stack now has an explicit sticky-overflow guard (test-covered in
   `tests/hls/test_hls_data_structures.cpp`) so this fails safe rather than
   corrupting state, but the depth itself needs sizing against real kernels
   (T034) once they exist, not left at the placeholder.

---

## 7. Verification contract (feeds T019)

Bit-exact register file parity against the SystemC golden model for every
kernel in `common/kernel_programs.h`. Cache hit/miss counts
(`getL1CacheHits()`/`getL1CacheMisses()` equivalents) are in scope, checked
per-way now that the cache is a real bank/set/way structure rather than a
`std::map`. **New vs. v2**: since `global_mem` is now a real bus, add `m_axi`
transaction-count checks (burst count, read/write byte totals) as an explicit
parity/sanity metric — there was nothing to count in the on-chip-only design.

---

## 8. Synthesis configuration and pragmas (T024)

**Environment caveat, stated once here rather than at every number below**:
`vitis_hls` is not available in the environment this was authored in — only
its headers (`ap_int.h`, `hls_stream.h`), which is what made the csim-style
`g++` testing throughout T019/T022/T023 possible at all. Everything in this
section is a principled, documented design choice, but **none of it is
validated against a real C-synthesis or co-simulation resource/timing
report**. Treat every burst/outstanding/partition number as provisional until
someone runs actual `vitis_hls` synthesis — that is real follow-on work, not
a formality.

### 8.1 Per-board config: `hls/config/{kv260,u55c}.h`

Macros, not `constexpr` — pragma argument lists are parsed by straight
preprocessor text substitution, not full C++ constant-expression evaluation,
so a named `constexpr` symbol is not reliably accepted there across tool
versions. Selected by defining exactly one of `RISCV_GPGPU_BOARD_KV260` /
`RISCV_GPGPU_BOARD_U55C` as a compiler flag (a Vitis project's
`add_files -cflags`, T025/T026 scope) before `hls_config.h` is included.
Carries `ADDR_BITS` (per §4/§6) and `m_axi` burst length /
num_read_outstanding / num_write_outstanding. No board macro defined (every
`tests/hls/*.cpp` in this repo) falls back to the KV260-sized defaults that
existed before T024 — csim behavior is unchanged by this section's work.

### 8.2 `BIND_STORAGE`: BRAM only, no URAM tier

Every on-chip array (`cache_bank.h`'s `tag_`/`valid_`/`data_`,
`memory_pipeline.h`'s `shared_mem_`) is now bound `type=RAM_2P impl=BRAM`.
No URAM binding anywhere — correct given the current design: v2's on-chip-
only draft had URAM as the "last resort" global-memory tier, but v3 moved
global memory entirely off-chip via `m_axi` (§1/§3), so there is no on-chip
tier large/cold enough to prefer URAM's higher density over BRAM's lower
latency. Revisit only if real synthesis shows KV260's combined on-chip
budget (§5) is tight enough that trading L2 latency for URAM's density is
worth it — not assumed here.

### 8.3 `ARRAY_PARTITION` — required, not optional, given pragmas already present

Two partitions were added, and both are **correctness-for-synthesis fixes**,
not optimizations layered on an already-correct design:

- **`cache_bank.h`'s `tag_`/`valid_`/`data_`, `dim=1 complete`** (the `WAYS`
  dimension). `lookup()`'s `LOOKUP_WAYS` loop (§3.2) already carried
  `#pragma HLS UNROLL` before T024 — reading all `WAYS` entries in the same
  cycle. Without partitioning that dimension, `tag_`/`valid_`/`data_` are each
  one array with (at most) 2 physical ports; the unrolled loop has nowhere
  near enough ports to actually run in parallel. The "N parallel banks, each
  independently portable" design §3.2 describes was only true in comment
  form until this partition existed.
- **`compute_pipeline.cpp`'s `regs` parameter, `dim=1 complete`** (the lane
  dimension). Same issue: `executeALU()`/`executeVector()` unroll over all
  `MAX_THREADS_PER_WARP=32` lanes, every one reading/writing `regs[t][...]`
  the same cycle.

**Not** partitioned, deliberately: `cache_bank.h`'s `FILL_WORDS`/
`READ_LINE_WORDS` loops (the `WORDS_PER_LINE=32` dimension) were changed
from `UNROLL` to `PIPELINE II=1` instead of also partitioning that dimension
— fully parallel line fill/read would mean `WAYS × SETS_PER_WAY ×
WORDS_PER_LINE` independent single-word memories, for a path (miss/L2-hit
fill) that isn't the common case. This now matches the sibling `m_axi` burst
loop (`memory_pipeline.h`'s `FETCH_LINE_WORDS`), which already used
`PIPELINE` for the same reason before T024 — T024 just made the two
consistent instead of leaving one UNROLLed and one PIPELINEd for no
principled reason.

### 8.4 Cross-check

`memory_pipeline.cpp` now has a `static_assert(RISCV_GPGPU_MAXI_MAX_READ_BURST_LEN
== WORDS_PER_LINE, ...)` (and the write-length equivalent) so the per-board
burst-length macros can't silently drift from the cache line size they're
supposed to match — a plain C++ compile-time check, so it fires on every
build (csim included), not only real synthesis.

---

## 9. Golden-model reconciliation and kernel test-coverage matrix

Tracks two things as the golden model (`models/systemc/`) and this port
diverge and get reconciled over time: (a) behavioral discrepancies found
between the two, and (b) which `common/kernel_programs.h` kernels have
actually been run through the HLS port vs. only read/assumed.

### 9.1 Discrepancies found against upstream, and their resolution

Found by diffing against `origin/init_gpgpu` after it advanced past the
commit this port was originally built from (`5a80f01` → `9c4dfea`, "GPGPU
READY"), then merged in (fast-forward, no local commits lost — all local
work was uncommitted working-tree changes in files upstream never touched).

| # | Discrepancy | Resolution |
|---|---|---|
| 1 | `SIMTController::handleBranch()` bug fix: the "all active lanes take the masked path" case used to leave every lane active (should mask them all to 0) — see `divergence_stack.h`'s `handleBranch()` comment for the full trace. | **Fixed** in `divergence_stack.h` to match the corrected 3-case golden logic. Regression-covered by `tests/hls/test_hls_data_structures.cpp`'s `AllLanesMaskedIsNotTreatedAsNoDivergence` (unit-level) and `test_pipeline_integration.cpp`'s `ParallelReductionAcrossTwoWarpsWithBarrier` (end-to-end) — both verified to actually fail against the pre-fix logic, not just pass incidentally. |
| 2 | `top.cpp` rewritten for genuine multi-CU fan-out + global barrier resolution (was single-CU only). | **Documentation fix, no code change needed** — §2.4's host-orchestrated barrier contract was already CU-count-agnostic ("all outstanding warps," not "all warps on CU 0"). Only the stated *rationale* was stale; corrected in §2.4. |
| 3 | New `regs[t][3] = local_warp_id` host register convention (`buildWarpContext()`). | Documented in §2.2's host contract. `compute_pipeline` doesn't consume it itself — purely a host/test setup responsibility, same tier as r0–r2. |
| 4 | Three new kernels added to `kernel_programs.h`: `parallelReduction()`, `fpGemm()`, `conv2d3x3()`. | All three now have HLS-side tests — see §9.2. |
| 5 | New `GPGPUTop::getNextWarpId()`/`readWord()` query methods. | Test-support only (used by the golden model's own `regression_test.cpp` Phase 11 to compute expected addresses and verify memory contents). No HLS-side equivalent needed — this port's tests read `regs[][]` and the `global_mem` backing buffer directly instead. |

### 9.2 Kernel coverage matrix

| Kernel (`kernel_programs.h`) | Golden-model test | HLS-side test | Notes |
|---|---|---|---|
| `intSaxpy` | Phase 4b (`Vector SAXPY`), Phase 9a | `test_compute_pipeline.cpp::IntSaxpy` | |
| `fpUniformSaxpy` | Phase 8b, 9b | `test_compute_pipeline.cpp::FpUniformSaxpy` | |
| `memoryRoundTrip` | Phase 9c | `test_compute_pipeline.cpp::MemoryRoundTrip` (mock memory), `test_pipeline_integration.cpp::MemoryRoundTripThroughRealMemoryPipeline` (real `memory_pipeline`) | Hit/miss *rate* diverges from golden by design — line-granular vs. word-granular cache, see §7. |
| `divergentOddEven` | Phase 6b, 9d | `test_compute_pipeline.cpp::DivergentOddEven` | |
| `barrierRoundTrip` | `benchmark_test.cpp` (10-warp launch) — **not** `regression_test.cpp`, corrected from an earlier draft of this table | `test_pipeline_integration.cpp::BarrierRoundTripPreservesMemoryAcrossStall` | Ported as a single warp (the golden usage is 10 warps across a multi-GPU system — out of scope here, this port is about the kernel's own-warp memory-across-barrier property, not multi-GPU load distribution). Complements `ComputePipeline.BarrierStallThenResume` (register-only, hand-written program, no memory) and `ParallelReductionAcrossTwoWarpsWithBarrier` (cross-*warp* memory visibility) — this one checks a single warp's own memory write survives its own stall/resume boundary. |
| `parallelReduction` | Phase 11a | `test_pipeline_integration.cpp::ParallelReductionAcrossTwoWarpsWithBarrier` | The multi-warp barrier + bug-fix regression test (§9.1 #1). |
| `fpSaxpy` | Phase 9e | *(covered indirectly — same `VFMUL`/`VFADD` opcodes as `fpUniformSaxpy`/`intSaxpy`'s tests)* | |
| `fpFmadd` | Phase 9f | *(covered indirectly — same `VFFMADD` opcode as `fpGemm`'s test)* | |
| `fpDivergentSaxpy` | *(none — grep-verified, neither `regression_test.cpp` nor `benchmark_test.cpp` references it)* | `test_compute_pipeline.cpp::FpDivergentSaxpy` | **Doc bug found**: the kernel's own comment ("Even threads compute FP SAXPY; odd threads are masked") is wrong for its actual instruction sequence — the real branch condition is `r0 & (r0+1) == 0`, which for sequential `r0[t]=t` gives a sparse fall-through set `{0,1,3,7,15,31}` (values of the form `2^k-1`, plus 0), not alternating even/odd; looks like a copy-paste from `divergentOddEven()`'s wording without re-deriving the bit trick. Verified by direct calculation before writing the test (not assumed from the comment) — expected values in the HLS test are derived from the real semantics, not the doc's claim. No golden execution exists to independently cross-check against, unlike every other row in this table. |
| `fpGemm` | Phase 12a | `test_compute_pipeline.cpp::FpGemm2x2TileK4` | |
| `conv2d3x3` | Phase 13a | `test_compute_pipeline.cpp::Conv2d3x3` | |

Every numeric expected value in the HLS-side tests listed above was taken
from the golden model's own hand-traced arithmetic or, where available (all
`test_compute_pipeline.cpp`/`test_pipeline_integration.cpp` entries),
cross-checked against `docs/verification/systemc_regression_2026-07-26.txt`'s
actual golden-model execution output — not independently invented. The one
exception is `fpDivergentSaxpy`: since no golden execution of it exists
anywhere to check against, its HLS test's expected values rest solely on
tracing the instruction semantics directly (verified by an independent
Python calculation before writing the test, but still a single source, not
a cross-check against a second, independent implementation the way every
other row in this table is).

As of this table's last update, every kernel in `common/kernel_programs.h`
has at least one HLS-side test.
