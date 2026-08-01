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

**§11.3/§12's "wrong golden reference" correction was investigated and then
RECONSIDERED - `compute_pipeline`'s `ComputeUnit::executeWarp()` target
stands.** A real, verified fork exists between `executeWarp()` (Virtual
ISA/SIMT) and `ComputeUnit::step()`/`executeRV32()` (real RV32I) - see
§11.3 for that finding, kept intact for the record. But the project's
decision, confirmed directly: the SystemC model's own documented
functionality (`models/systemc/README.md`'s opcode table, and `types.h`'s
`Opcode` enum itself - checked directly, matches this port's
`hls_types.h` value-for-value) is the source of truth for what this port
should implement, independent of whether that functionality happens to be
real-RISC-V-compliant. `hls/README.md`'s "port `ComputeUnit::step()`"
instruction is a different, not-currently-adopted target, not a
correction to apply here. §11.3 now carries the full reconsideration.

---

## 1. Scope and design decision

Two independently synthesizable IP blocks, `compute_pipeline` (§2) and
`memory_pipeline` (§3), connected by an explicit streaming interface rather than
one fused kernel. This keeps each block's resource footprint separately
measurable — important because `memory_pipeline` now carries both an on-chip
cache (BRAM) footprint *and* an `m_axi` interface, two separately-tunable knobs.

**Board scope: KV260 only.** Alveo U55C support was discarded permanently
(§14) — AU15P was already out of scope per an earlier, unrelated team
decision.

**What this draft corrects from v2:**
- **`m_axi` is back.** `memory_pipeline` has a real external-memory port backing
  the L2 miss path. Global memory is not a third on-chip tier; it is the actual
  external DDR/HBM the golden model's `global_memory_` map represents.
- **On-chip BRAM is now scoped to caches only**: shared memory (per-CU
  scratchpad), L1 (per-CU), L2 (shared). None of them are sized to hold the
  *entire* addressable memory anymore — they're sized as caches, with normal
  cache miss/fill/writeback behavior against `m_axi`.
- **External memory is 4GB DDR4**, reached through the PS's HP AXI port (see
  `hls/constraints/kv260.tcl`, and §10.11/§14 for why HP over HPC).

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

### 2.4 Barrier handling: host-orchestrated (not an on-chip barrier unit) — SUPERSEDED, see §2.5

**⚠ Superseded.** This section documents the host-orchestrated decision as
it stood through T024. §2.5 replaces it: barrier resolution (and warp
dispatch generally) moved on-chip, per `gpgpu_main`'s stated architecture
(§10.1). Kept below unedited for the historical record — the contract it
describes was real and tested (§9.2's coverage table) up to this point.

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

## 2.5 On-chip warp scheduler and dispatch FSM

Consolidates §10.6-§10.10 into a single interface contract, in the same
style as §2/§3 — written before any implementation code, per this
project's own practice for `compute_pipeline`/`memory_pipeline`. Full
reasoning/history for every decision below lives in §10; this section
states the current contract, not how it was reached.

### 2.5.1 Responsibility

Replaces host-orchestrated per-warp invocation (§2.4, superseded) with
autonomous on-chip sequencing: kernel launch, warp→CU assignment,
dispatch, and barrier resolution all happen in hardware. Ports
`WarpScheduler` (`models/systemc/src/scheduler/`) and the dispatch/barrier
portion of `GPGPUTop::simulationProcess()`
(`models/systemc/src/top/top.cpp`) — scoped down per §10.3's findings:
`WarpScheduler::stalled_queues_`/`markWarpStalled()` are dead code in the
golden model's real execution path and are not ported. Barrier semantics
are global and bit-faithful to `simulationProcess()`, within a declared
hardware capacity limit (§2.5.5).

### 2.5.2 Components

Four new pieces, plus changes to one existing block. `memory_pipeline`
itself is **unchanged**.

| Component | Role | Count | Full design |
|---|---|---|---|
| `CuDispatchUnit` | Holds one CU's resident warp state, dispatches to its `compute_pipeline`, applies barrier-release broadcasts | 1 per CU | §10.7 |
| `BarrierArbiter` | Kernel launch sequencing, capacity hazard check, global barrier arrival counting/release | 1, system-wide | §10.6, §10.7 |
| `MemArbiter` | N:1 request / 1:N response routing between the CUs and `memory_pipeline` | 1, system-wide | §10.9 |
| Per-CU program store | Replicated on-chip copy of the kernel's instructions - owned by `CuDispatchUnit` (`loadProgram()`/`programArray()`), corrected from an earlier draft that assigned this to `BarrierArbiter` | 1 per CU | §10.8 |
| `compute_pipeline` (changed) | Free-running/stream-driven instead of per-invocation `ap_ctrl_hs`; `instr_in` stream replaced by a local program-array read; `regs[][]` now slot-indexed | 1 per CU (unchanged count) | §10.7, §10.8 |

### 2.5.3 Signatures / types

```cpp
// One resident slot per warp a CU currently owns for the running kernel.
// Corrected during implementation (real code in hls/src/scheduler/
// cu_dispatch_unit.h, hls/src/common/hls_types.h): regs is NOT a WarpSlot
// member. compute_pipeline's regs[][] port needs one flat
// reg_t[MAX_WARPS_PER_CU][MAX_THREADS_PER_WARP][32] array to alias - an
// array of WarpSlot structs (one 4KB regs blob per element, mixed with
// small bookkeeping fields) doesn't compose into that shape. Register
// storage lives in CuDispatchUnit as its own separate flat member instead
// (struct-of-arrays). Also: an enum's underlying type must be a plain
// integral type, not an ap_uint<N> class (a real compile error caught
// against the real Vitis HLS headers) - State's underlying type is
// uint8_t, matching the WarpStatusCode convention already used elsewhere
// in hls_types.h.
struct WarpSlot {
    warp_id_t     warp_id    = 0;
    ap_uint<16>   resume_pc  = 0;   // program-store index to resume at after a stall
    barrier_id_t  barrier_id = 0;   // meaningful only if state == STALLED
    enum class State : uint8_t { EMPTY, READY, STALLED, DONE };
    State         state      = State::EMPTY;
    // regs removed - see CuDispatchUnit::regsArray()
};

// CuDispatchUnit -> compute_pipeline
struct warp_dispatch_t {
    ap_uint<3>    slot_id;           // which of this CU's MAX_WARPS_PER_CU slots
    warp_id_t     warp_id;
    thread_mask_t active_mask_init;
    ap_uint<16>   resume_pc;
};

// compute_pipeline -> CuDispatchUnit (+ event forwarded to BarrierArbiter)
struct warp_status_t {              // supersedes §2.2's warp_status_t
    WarpStatusCode code;             // COMPLETE | STALLED_AT_BARRIER
    barrier_id_t   barrier_id;       // meaningful only if STALLED_AT_BARRIER
    ap_uint<3>     slot_id;          // NEW vs. §2.2 - identifies the slot without a lookup
    ap_uint<16>    resume_pc;        // NEW, found during implementation (not in an
                                      // earlier draft of this struct): compute_pipeline
                                      // reads a shared program[] array by an internal
                                      // index now (§10.8), not a host-fed stream, so it
                                      // must report where to resume - the old
                                      // host-orchestrated design never needed this since
                                      // the host tracked stream position itself.
                                      // Meaningful only if STALLED_AT_BARRIER.
};

void compute_pipeline(
    hls::stream<warp_dispatch_t>& dispatch_in,               // NEW - replaces
                                                               // §2.2's per-invocation
                                                               // cu_id/warp_id/
                                                               // active_mask_init scalars
    instr_word_t   program[MAX_PROGRAM_LEN],                  // NEW - local BRAM,
                                                               // replaces §2.2's instr_in stream
    uint32_t       program_len,                               // s_axilite, set once at launch
    reg_t regs[MAX_WARPS_PER_CU][MAX_THREADS_PER_WARP][32],   // NEW - slot-indexed,
                                                               // aliases the owning
                                                               // CuDispatchUnit's WarpSlot.regs
    hls::stream<mem_req_t>&  mem_req_out,                     // unchanged from §2.2,
    hls::stream<mem_resp_t>& mem_resp_in,                     // now routed via MemArbiter
    hls::stream<warp_status_t>& status_out                    // updated shape, above
);
// memory_pipeline's signature (§3.3) is unchanged - MemArbiter sits outside it.
```

### 2.5.4 Fixed-size parameters

- `MAX_WARPS_PER_CU = 4` — successor to `MAX_WARPS_PER_BLOCK`
  (§10.5/§10.6's naming). Same value, same golden-`max_warps`-default
  origin, different role: a hard per-launch capacity ceiling (§2.5.5), not
  a barrier-group size.
- `NUM_CUS` — still open, §10.1/§10.4 step 8.
- `MAX_PROGRAM_LEN = 256` — still open (§6.2), now replicated per CU
  (§10.8) rather than held once.
- `MAX_CONCURRENT_BARRIERS = 4` — **finding, not yet resolved**: this
  constant sized the *old* host-orchestrated design's per-`barrier_id`
  arrival tracking (§2.3). `BarrierArbiter` doesn't need that — it keeps
  one running arrival counter per kernel, not a table indexed by
  `barrier_id` (§10.7), because valid kernels have all warps converge on
  the same barrier point at a time (the inherited assumption noted in
  §10.7). This constant is likely vestigial for §2.5's design; flagged
  here rather than silently dropped, left for the implementation pass to
  confirm before removing it from `hls_config.h`.

### 2.5.5 Barrier handling: global, hazard-mitigated (supersedes §2.4)

Barrier release requires every warp in the kernel to have arrived —
exactly `kernel_programs.h:18`'s documented semantics and
`simulationProcess()`'s implementation, not a redesign (§10.6). The one
thing this requires that the golden model gets for free (unbounded
software memory) is the hazard mitigation from §10.6's hazard table: a
kernel launch is only valid if `total_warps ≤ NUM_CUS × MAX_WARPS_PER_CU`
(`max_resident_warps`, §2.5.6); above that, the launch is rejected
(`status.fault`), not silently run into a hang. Within that limit,
`BarrierArbiter`'s `stalled_count_ == total_warps` release condition is
bit-faithful to `simulationProcess()`'s `barrier_queue.size() ==
total_warps_`.

**Inherited, non-enforced assumption** (ported as-is, not newly
introduced): neither the golden model nor this design checks that every
stalled warp shares the same `barrier_id` before releasing — `top.cpp`
reads `barrier_queue[0].bid` and clears that one; `BarrierArbiter` does the
analogous thing. A kernel that violates uniform barrier participation
(some warps finishing without ever reaching a barrier while others do)
hangs `simulationProcess()` in software too — an existing golden-model
requirement on valid kernels, not a gap this port introduces (§10.7).

### 2.5.6 Host register interface (supersedes §2.4's per-warp re-invoke contract)

See §10.10 for the full launch sequence. Register set:

| Register | Dir | Type | Purpose |
|---|---|---|---|
| `program_ptr` | W | `addr_t` (`m_axi`) | DRAM address of the compiled kernel's instructions |
| `program_len` | W | `uint32` | Instruction count (≤ `MAX_PROGRAM_LEN`) |
| `total_warps` | W | `warp_id_t` | `grid_x × grid_y`, precomputed host-side |
| `start` | W (strobe) | 1 bit | Begin launch sequence |
| `status` | R | bitfield | `busy` / `done` / `fault` |
| `max_resident_warps` | R (static capability) | `warp_id_t` | `NUM_CUS × MAX_WARPS_PER_CU` |
| IRQ line | — | — | Optional alternative to polling `status` |

Per-warp registers from §2.2 (`cu_id`, `warp_id`, `active_mask_init`,
per-invocation `program_len`) are gone from the host's view — computed
internally by `CuDispatchUnit` at launch (§10.7).

### 2.5.7 Verification status

**Implemented and passing, real execution not just compilation, as of this
session's implementation pass:**
- `hls/src/scheduler/cu_dispatch_unit.h` (`CuDispatchUnit`) - verified via a
  behavioral smoke test (launch assignment, FIFO dispatch order, barrier
  stall/release cycle). Not yet a formal `tests/hls/` GTest file.
- `hls/src/scheduler/barrier_arbiter.h` (`BarrierArbiter`) - verified via a
  smoke test built against real `CuDispatchUnit` instances (hazard
  mitigation, full barrier wave to completion, non-uniform-participation
  safety). Not yet a formal `tests/hls/` GTest file.
- `hls/src/scheduler/mem_arbiter.h` (`MemArbiter`) - verified via a smoke
  test using real `hls::stream` objects. `NUM_CUS=1` means true multi-source
  round-robin fairness isn't exercised yet - flagged, not silently assumed.
- `hls/src/compute_unit/compute_pipeline.{h,cpp}` - rewritten to this
  section's free-running/stream-dispatched shape. `tests/hls/
  test_compute_pipeline.cpp` (8 tests) and `tests/hls/
  test_pipeline_integration.cpp` (3 tests, including the real two-warp
  barrier test against a real `memory_pipeline`) - all real, pre-existing
  GTest files, every kernel program and expected value kept unchanged from
  before this rewrite, all still passing.
- Two real gaps found and fixed during this pass, not anticipated when this
  section was first drafted: `warp_status_t` needed a `resume_pc` field
  (compute_pipeline now reads a local `program[]` array by index rather
  than a host-fed stream, so it has to report where to resume - see
  hls_types.h), and `cu_id` had to come back as a scalar parameter rather
  than living in `warp_dispatch_t` (fixed per instance, not per-dispatch).
  Both are reflected in this section's signatures above.

**Step 6 (program store) also done**: `CuDispatchUnit::loadProgram()`/
`programArray()` (§10.8, reassigned there from `BarrierArbiter` during
implementation - see §10.8's correction). Verified end-to-end, not just as
an isolated copy: a smoke test loads `intSaxpy` through `loadProgram()`
from a simulated DRAM buffer and runs it on a real `compute_pipeline`
instance aliased to that same `CuDispatchUnit`, getting the same result
`test_compute_pipeline.cpp`'s `IntSaxpy` test already established - this is
the first point in this session where `CuDispatchUnit` and
`compute_pipeline` have actually run together, previewing step 7's
top-level wiring at small scale.

**Top-level wiring also done (§10.12)**: `GpgpuTop`/`schedulerStep()`
(`hls/src/scheduler/gpgpu_top.h`) tie `CuDispatchUnit`/`BarrierArbiter` to
real per-CU `compute_pipeline` instances. Capstone-verified: a two-warp
`parallelReduction` barrier kernel runs to completion through the entire
pipeline (scheduler + `CuDispatchUnit` + `BarrierArbiter` + `MemArbiter` +
real `compute_pipeline` + real `memory_pipeline`) with zero test-side
dispatch/barrier orchestration - the scheduler decides everything itself,
and the result matches the golden model exactly. See §10.12 for the full
account.

**Formal `tests/hls/` GTest coverage now exists** for every component in
§2.5.2: `test_cu_dispatch_unit.cpp` (8 tests), `test_barrier_arbiter.cpp`
(6), `test_mem_arbiter.cpp` (4), `test_gpgpu_top.cpp` (3, including the
autonomous two-warp barrier capstone) - wired into `tests/hls/CMakeLists.txt`
following the exact guarded pattern the existing four targets already use.
32/32 tests pass across all six `tests/hls/` binaries combined (the four
new ones plus the two rewritten in this pass). Two real bugs found and
fixed while formalizing these from the ad-hoc smoke tests: a test-authoring
mistake in `CuDispatchUnit`'s `BuildDispatch...` test (a slot left in an
unintended state), and a genuine dangling-reference bug in
`test_gpgpu_top.cpp` - detached background threads (`compute_pipeline`/
`mem_arbiter`/`memory_pipeline`, all free-running, never return) held
references to stack-local streams/`GpgpuTop` state that got destroyed the
moment their owning `TEST()` function returned; fixed by giving those
objects `static` (process) duration, matching the convention already used
elsewhere for objects background threads outlive. **Same latent pattern found in
`test_compute_pipeline.cpp`/`test_pipeline_integration.cpp`'s `CpFixture`
(built earlier in this implementation pass) - now also fixed**: every
`CpFixture cp;` call site changed to `static CpFixture cp;` (11 occurrences
across both files), same reasoning, re-verified passing (5 consecutive
clean runs each, no crashes) after the change.

**Not yet done**: no real `vitis_hls` synthesis of any of this has happened
yet, and the cross-kernel `regs[][]`/`program[]` BRAM-aliasing +
`program_ptr` `m_axi` binding remain genuine system-integration questions
(T025 scope - see §10.12). `NUM_CUS`/URAM/AXI-port decisions (§10.11) were
already made from the pre-existing T020 data, ahead of this implementation
pass, and don't block it.

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

    ap_uint<32>* global_mem              // m_axi, KV260 HP port -> PS DDR4
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
`Instruction`, etc. — with `ADDR_BITS` reverting to cover the board's real
external address space (not an on-chip capacity as v2 had it):

| SystemC | HLS | Notes |
|---|---|---|
| `Address` (`uint64_t`) | `ap_uint<ADDR_BITS>` | 32 bits (4GB DDR4, `kv260.tcl`). |

---

## 5. On-chip memory budget (caches only now) — KV260

| | **KV260 (XCK26)** |
|---|---|
| Block RAM (36Kb blocks) | 144 blocks (≈ 5.06 Mb raw) |
| UltraRAM (288Kb blocks) | 64 blocks (≈ 18 Mb raw) |
| Combined on-chip (vendor-quoted) | ≈ 26.6 Mb (~3.3 MB) |
| External memory via `m_axi` | 4GB DDR4 (shared w/ PS, via HP) |
| Practical implication | Still size caches (shared mem + L1 + L2 banks) here first — plenty of headroom now that global memory isn't competing for the same budget |

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
   KV260's DDR4 latency/bandwidth — this is a new tuning axis v2 didn't
   have at all.

---

## 6. Open decisions requiring team sign-off before T022/T023 begin

1. **Structural split (two streaming-connected kernels)** — confirmed, unchanged.
2. **`MAX_PROGRAM_LEN`** — still open.
3. **`WAYS` for L1 and L2** — proposed defaults above (L1=2, L2=4); not yet
   validated against real resource/timing numbers.
4. **`m_axi` binding specifics: decided — HP** (reversed from an earlier HPC
   lean; §10.11 has the reasoning). The U55C pseudo-channel question this
   item used to track is moot — U55C was discarded (§14).
5. **Barrier scope: final decision — global, matching the golden model
   exactly.** Reversed from an earlier block-scoped proposal (§10.3-§10.5,
   now deprecated — see §10.6). The hardware FSM suspends all warps until
   every warp in the kernel has arrived, exactly as
   `kernel_programs.h:18` documents and `simulationProcess()` implements.
   This project's task is to port the golden model's decisions, not
   replace them with new ones, even where the golden model's choice isn't
   the most hardware-efficient option. The one thing global scope
   genuinely requires that the golden model gets for free (unbounded
   software memory) is a hazard mitigation, not a scope change — see
   §10.6.
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
`vitis_hls` was not available in the environment this was originally
authored in — only its headers (`ap_int.h`, `hls_stream.h`), which is what
made the csim-style `g++` testing throughout T019/T022/T023 possible at all.
Everything in this section started as a principled, documented design
choice with **none of it validated against a real C-synthesis or
co-simulation resource/timing report**. **Update (T025)**: a real Vitis
2023.1 install is now configured (`scripts/setup-env.sh` auto-detects
`/tools/Xilinx`) and `tests/fpga/test_flow.tcl` passes real `csynth_design`
for both kernels on KV260 (§10.11 has the measured numbers) — burst/
outstanding/partition tuning below is still unvalidated (no co-simulation or
place-and-route run yet), but the "no `vitis_hls` at all" caveat itself no
longer applies.

### 8.1 Per-board config: `hls/config/kv260.h`

Macros, not `constexpr` — pragma argument lists are parsed by straight
preprocessor text substitution, not full C++ constant-expression evaluation,
so a named `constexpr` symbol is not reliably accepted there across tool
versions. Selected by defining `RISCV_GPGPU_BOARD_KV260` as a compiler flag
(a Vitis project's `add_files -cflags`, T025/T026 scope) before
`hls_config.h` is included. Carries `ADDR_BITS` (per §4/§6) and `m_axi`
burst length / num_read_outstanding / num_write_outstanding. No board macro
defined (every `tests/hls/*.cpp` in this repo) falls back to the KV260-sized
defaults that existed before T024 — csim behavior is unchanged by this
section's work. (`RISCV_GPGPU_BOARD_U55C` and `hls/config/u55c.h` existed
briefly and were removed — §14.)

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

---

## 10. On-chip WarpScheduler — design plan (open, targeted for next session)

**Status**: not started. This section is a plan, not a design yet — written
down so the next session (or anyone else) can pick this up without
re-deriving the context from chat history.

### 10.1 Why this changed

§2.4 scoped barrier/warp-sequencing logic as deliberately host-side software
for this milestone, with an on-chip scheduler as a stretch goal. That's now
being pulled forward: `gpgpu_main`'s `hls/README.md` explicitly plans
`warp_scheduler.cpp` as an HLS port ("small FSM, synthesizes cleanly"),
alongside `compute_pipeline.cpp`/`memory_pipeline.cpp` — not host software.
Decision: adopt the hardware-FSM scheduler, but **keep this project's own
two-kernel split and typed stream contracts** (`compute_pipeline` +
`memory_pipeline`, `mem_req_t`/`mem_resp_t`) rather than `gpgpu_main`'s
monolithic single-`gpgpu_top` design — ours is more elaborated and already
implemented/tested.

This also **merges with the multi-CU arbitration gap** already flagged in
§3.3/`memory_pipeline.h`: an on-chip scheduler managing multiple warps/CUs
and the global barrier queue *is* the missing piece that would route
requests from N `compute_pipeline` instances into `memory_pipeline`'s single
`req_in`/`resp_out` stream pair. Design them together, not separately.

**Consequence for the other open items**: `NUM_CUS`, the L2 URAM decision,
and the KV260 HP-vs-HPC `m_axi` port choice are intentionally left
unresolved until this scheduler is designed — the scheduler's shape
determines the real per-CU/shared resource split (needed to size caches
meaningfully) and the actual host↔PL handoff pattern the AXI-port argument
depends on (see the HP recommendation in this session's discussion, made
*conditional on* an autonomous on-chip scheduler existing).

### 10.2 Real local reference to port from

Unlike most of this project's HLS work, this does **not** need to reach into
`gpgpu_main` — the necessary golden-model logic already exists locally, on
`init_gpgpu`, and is the actual reference to read first:

- `models/systemc/src/scheduler/warp_scheduler.{h,cpp}` — `WarpScheduler`
  class. Real state: `ready_queues_`/`stalled_queues_` (`std::queue<WarpID>`,
  **one pair per CU**), `round_robin_indices_` per CU,
  `kernel_warp_counter_`. Real methods: `submitKernel()`, `selectWarp(cu_id)`,
  `markWarpComplete()`, `markWarpStalled()`, `hasReadyWarps()`,
  `isComplete()`. Three policies exist (`ROUND_ROBIN`/`PRIORITY`/`FIFO`);
  `gpgpu_main`'s README only mentions round-robin — confirm which this
  project actually wants before assuming round-robin is the target.
- `models/systemc/src/top/top.cpp`'s `simulationProcess()` (`SC_THREAD`,
  lines ~118-165+) — the actual global barrier mechanics this needs to
  become hardware state for: fans out one warp dispatch per CU per outer
  iteration, accumulates stalled warps into `barrier_queue` across **all**
  CUs, fires when `barrier_queue.size() == total_warps_` (every warp in the
  kernel, not per-CU), clears the barrier in every CU, and resumes.

**⚠ §10.3's Decision/Amendment and §10.5's design are DEPRECATED — reversed
in §10.6.** Kept below, unedited, for the historical reasoning: Finding A
(dead code) still stands as-is, and Finding B/the deadlock risk are still
real and are exactly why §10.6 exists. What's reversed is the *response* to
that risk (block-scoped barriers) — read §10.6 for the current decision
before implementing anything from §10.3-§10.5.

### 10.3 Findings from reading the full implementation, and a decision

Step 1 below (read `warp_scheduler.cpp` and the rest of `simulationProcess()`
in full, not just the header) is done. Two findings changed the plan.

**Finding A — `stalled_queues_` is dead code.**
`WarpScheduler::markWarpStalled()`/`stalled_queues_` exist on the class, but
`simulationProcess()` never calls `markWarpStalled()`. When a warp stalls,
`top.cpp` instead calls `scheduler_->markWarpComplete()` — detaching the warp
from the scheduler entirely (`top.cpp:146`'s `// detach` comment) — and
tracks it in a local `barrier_queue` vector inside `GPGPUTop`, not in the
scheduler. So the scheduler's real, exercised surface is smaller than its
interface suggests: `generateWarps()` (round-robin grid→CU assignment) +
`ready_queues_` + `selectWarp()` (pop-front). All barrier/stall bookkeeping
is `GPGPUTop`'s job, not `WarpScheduler`'s. This narrows what the FSM
actually needs to port from `WarpScheduler` itself — the stalled-queue
machinery does not need a hardware equivalent, because the software version
it would be ported from is unused.

**Finding B — barriers are whole-kernel-scoped in the golden model, not
per-CU.** `simulationProcess()`'s barrier fires only when
`barrier_queue.size() == total_warps_` (`top.cpp:157`) — every warp across
every CU in the entire grid, not just the warps on one CU. Real GPUs scope
barriers (`__syncthreads()`) to a single thread block, normally mapped to one
CU. The golden model's stricter global scope means one slow CU stalls every
other CU's warps too.

**Decision: adopt standard, per-CU barrier scope for the hardware FSM**, not
the golden model's global scope. Matches real GPU semantics, avoids one CU
blocking all the others, and needs a smaller counter (bounded by
`max_warps_per_cu` per CU, not `total_warps_` for the whole grid).

**Amendment (found while sizing this in §10.5): raw per-CU scope can
deadlock.** A CU can only hold a small, fixed number of warps' worth of
register state resident at once (`ComputeUnit::Config::max_warps`, golden
default 4). If a kernel launch assigns more warps to one CU than that (easy
with a big grid and few CUs), a barrier that waits for "every warp ever
assigned to this CU" can never fire: the warps beyond the resident limit
can't even start until a resident slot frees up, and it never does, because
the resident warps are frozen at the barrier waiting for those un-started
ones to arrive. Real GPUs avoid this by scoping barriers to a **block** —
a group sized to fit the resident limit, not to the whole per-CU warp count.
Decision refined accordingly: barrier scope is **per-block**
(`MAX_WARPS_PER_BLOCK = 4`, matching `max_warps`), not raw per-CU. See §10.5
for the resulting state design.

**Consequence for §7's parity contract**: this is a deliberate divergence
from the golden model, not a bug fix — needs to be flagged as such wherever
it's tested, not silently compared against golden-model output as if it were
still bit-exact. It's only observable when `NUM_CUS > 1` and a kernel's warps
on different CUs reach a barrier at different times; for `NUM_CUS == 1`
(today's only synthesized/tested configuration) the two scopes behave
identically, so no existing test is affected. Once `NUM_CUS` is decided
(§10.1) and multi-CU kernels are tested, the parity target for
barrier-related tests becomes "per-CU barrier semantics, hand-verified," not
"bit-exact against `simulationProcess()`" — add an explicit note to §7 when
that work starts.

### 10.4 Steps for the next session

1. ~~Read `warp_scheduler.cpp`'s implementation (not just the header) and the
   rest of `simulationProcess()` in full~~ — **done**, see §10.3.
2. ~~Design the HLS-safe state representation~~ — **done**, see §10.5. Turns
   out neither `ready_queues_` nor `stalled_queues_` needs a queue-shaped
   hardware equivalent at all — see §10.5 for why, and for the actual
   fixed-size state (`WarpSlot`/`BlockScheduler`) that replaces both.
3. ~~Decide the FSM ↔ `compute_pipeline` interface~~ — **done**, see §10.7.
   Free-running/stream-driven (matches `memory_pipeline`'s existing
   pattern), `warp_dispatch_t`/`warp_status_t` shape defined there.
4. ~~Decide the FSM ↔ `memory_pipeline` arbitration~~ — **done**, see §10.9.
5. Design on-chip barrier resolution — **reopened, see §10.6**. The
   §10.5 answer (block-scoped, ≤4-slot check) is deprecated along with the
   scope decision it depended on. §10.6 replaces this with global-scope
   arrival tracking plus a launch-time capacity check instead.
6. ~~Define what's left for the host~~ — **done**, see §10.10.
7. ~~Write the FSM's interface contract into a new §2.5~~ — **done**, see §2.5.
8. ~~Only after 1-7: revisit `NUM_CUS`, the L2 `URAM` move, and the KV260
   `m_axi` HP-vs-HPC choice~~ — **done**, see §10.11.

### 10.5 Resident-warp state design (step 2/5) — blocks, not queues — DEPRECATED, see §10.6

**Status**: design only, no code yet — matches step 7's stated ordering
(interface contract written down before implementation). Nothing in this
section exists as a `.h` file yet. **Superseded by §10.6**: block-scoped
barriers are reversed. The counter-based "no queue needed" insight below
still holds and carries over; the block-boundary/sequencing mechanics do
not — §10.6 has the replacement.

Barriers are scoped to a **block** of up to `MAX_WARPS_PER_BLOCK = 4` warps
(new constant, matches the golden model's existing but, in the live
`simulationProcess()` path, unenforced `max_warps_per_cu`/`max_warps`
default — §10.3's deadlock finding). A CU runs one block to completion
(every warp in it reaches `HALT`) before starting its next block.
**Consequence**: a kernel with more warps than `NUM_CUS × MAX_WARPS_PER_BLOCK`
runs in sequential waves per CU — this matches real GPU occupancy-limited
behavior, it is not a shortfall of this design.

**Why no queue is needed at all.** Both problems this step needed to solve —
"which warp does a CU dispatch next" (`ready_queues_`) and "which warps are
waiting at a barrier" (`stalled_queues_`/`barrier_queue`) — turn out not to
need a dynamic queue, because block→CU and warp→block assignment are fully
deterministic:

- Block `b`'s warps are global warp ids
  `[b·MAX_WARPS_PER_BLOCK, min((b+1)·MAX_WARPS_PER_BLOCK, total_warps_))` —
  computable from `b` alone.
- Block `b` is owned by CU `b % NUM_CUS` — computable from `b` and `NUM_CUS`
  alone (the same round-robin `generateWarps()` already does, just at block
  granularity instead of individual-warp granularity).
- So a CU's own block sequence is `b = cu_id, cu_id+NUM_CUS, cu_id+2·NUM_CUS,
  ...` — a single incrementing counter (`current_block_index_`) is the
  entire "what's next" state. No warp ids need to be stored or enqueued
  anywhere.
- Since a block never exceeds `MAX_WARPS_PER_BLOCK`, "who's arrived at the
  barrier" is a check over a compile-time-fixed 4-slot array, not a count
  against an unbounded `total_warps_`.

**The actual state (per CU), in design form:**

```cpp
// One resident slot per warp currently belonging to the CU's active block.
struct WarpSlot {
    warp_id_t     warp_id    = 0;
    ap_uint<16>   resume_pc  = 0;   // instr_in cursor to resume at after a stall
    barrier_id_t  barrier_id = 0;   // meaningful only if state == STALLED
    enum State : ap_uint<2> { EMPTY, READY, STALLED, DONE } state = EMPTY;
    RegisterFile  regs;             // reg_t[MAX_THREADS_PER_WARP][NUM_REGS_PER_THREAD]
                                     // - saved across a stall, same role as the
                                     // WarpContext the golden model's
                                     // barrier_queue saves today
};

class BlockScheduler {              // one instance per CU
    WarpSlot   slots_[MAX_WARPS_PER_BLOCK];
    ap_uint<3> active_count_        = 0;  // < MAX_WARPS_PER_BLOCK only for the
                                           // kernel's last (partial) block
    warp_id_t  current_block_index_ = 0;  // this CU's Nth block, not a global id
    // startBlock(), nextReadySlot(), recordResult(), blockComplete(),
    // blockAtBarrier(barrier_id_t&), releaseBarrier() - see prose below
};
```

- `startBlock()` — called once the previous block is fully `DONE`; computes
  the new block's first warp id and `active_count_` from
  `current_block_index_`/`cu_id`/`total_warps_`, resets all `active_count_`
  slots to `READY`.
- Dispatch loop — same round-robin shape as `simulationProcess()`'s inner
  loop, just scoped to ≤4 slots instead of `NUM_CUS` CUs: pick the next
  `READY` slot, run it through `compute_pipeline` (step 3 decides the exact
  call shape), record `DONE` or `STALLED(bid)`.
- Barrier check — trivial now: the block is "at the barrier" when every
  non-`DONE` slot is `STALLED` with the same `barrier_id`. Release: flip
  those slots back to `READY`, keeping their saved `regs`/`resume_pc`. This
  **is** step 5 (per-block barrier resolution) — solved by this same
  structure, not a separate one.

**Resource cost — why `MAX_WARPS_PER_BLOCK` isn't free to raise.** Each
`WarpSlot.regs` is `32 lanes × 32 registers × 4 bytes = 4KB`. Four slots =
16KB of resident register storage **per CU**, on top of the cache/shared-
memory budget (§5). This is a real BRAM cost that doesn't appear in §9's
resource table (which only covers a single stateless `compute_pipeline`
invocation) — re-run C-synthesis once this exists. It's also a concrete,
now-quantified reason `NUM_CUS` (§10.1) can't be picked independently of
this design: `NUM_CUS × 16KB` just for resident register storage, before
any cache sizing.

### 10.6 Reversal: global barrier scope restored, hazard analysis, final decision

**Decision**: block-scoped barriers (§10.3's Decision/Amendment, §10.5's
design) are reversed. The port adheres to the golden model's documented
barrier semantics exactly — `kernel_programs.h:18`: *"all warps suspend
until every warp in the kernel"* — global, whole-kernel scope, matching
`simulationProcess()`. Rationale: this project's task is to port the
golden model's decisions onto hardware, not to substitute new architectural
decisions for them, even where the golden model's choice isn't the most
hardware-efficient option available. One slow CU holding up every other
CU's warps at a barrier is an accepted, faithful-to-golden-model
performance characteristic, not a defect to design around.

**What restoring global scope does *not* remove**: the deadlock risk
identified in §10.3's Amendment. It's real regardless of barrier scope —
restoring global scope makes it worse, not better, since the participating
group is now the whole kernel instead of one CU's share of it. This isn't
a new architectural decision being introduced; it's a hazard analysis of
what "port the golden model faithfully" actually requires once storage is
finite. Documented formally below per standard hazard-analysis practice
(cause → effect → mitigation), since an unmitigated hang is a functional
hazard to the device, not a performance tradeoff.

**Hazard analysis:**

| Field | Description |
|---|---|
| **Hazard** | Device hangs indefinitely and never completes a kernel. |
| **Cause** | Global barrier release requires every warp in the kernel to have arrived. On-chip register storage for resident (dispatched-but-not-yet-complete) warps is finite. A kernel with more warps than the hardware can hold resident cannot make progress: warps beyond capacity can never be dispatched (no free slot), so they can never arrive, so the barrier condition can never become true. The golden model has no equivalent failure mode — software's `barrier_queue` (`std::vector<WarpContext>`) is unbounded, so this hazard is a genuine consequence of moving to bounded hardware, not a golden-model behavior being ported. |
| **Effect** | Kernel launch never completes. No error signaled, no recovery path — the device is stuck until externally reset. Severity: high (silent, total loss of forward progress) but narrow (only triggered by a kernel whose `total_warps_` exceeds hardware capacity — never triggered by any kernel that respects the limit). |
| **Mitigation (decided)** | Fixed, known hardware capacity for concurrently-resident warps, exposed as a **readable/host-checkable register** (not a silent hardcoded constant). A kernel launch with `total_warps_` exceeding that capacity is an invalid launch — out of scope for this hardware, the same way real GPUs have occupancy/resident-block limits. The barrier itself stays exactly as the golden model defines it; it only ever operates within a kernel size the hardware is guaranteed to hold entirely resident, so its behavior is bit-faithful to `simulationProcess()` for every kernel this hardware actually accepts. |
| **Mitigation (rejected)** | Spilling stalled warps' register state to external memory (`m_axi`) so kernels of unbounded size are supported with no exception. Rejected for now: real added infrastructure (a save/restore protocol, address management, extra DDR/HBM traffic) beyond what "port the existing decisions" calls for. Can be revisited later if an unbounded kernel size becomes an actual requirement — noted here so the option isn't lost, not pursued now. |

**Resulting design change from §10.5**: no block subdivision, no per-CU
sequencing of "one block to completion, then the next." Instead:

- At kernel launch, validate `total_warps_ ≤ NUM_CUS × MAX_WARPS_PER_CU`
  (the capacity register from the mitigation above; `MAX_WARPS_PER_CU`
  replaces `MAX_WARPS_PER_BLOCK` as the per-CU resident-slot count, same
  value, same golden-model-default-4 origin, different role — it's now a
  hard capacity ceiling checked once at launch, not a barrier-group size).
  A kernel that fails this check is not launched.
- Since every kernel the hardware accepts is now guaranteed to fit,
  **every** warp gets a resident `WarpSlot` for the kernel's entire
  duration — assigned round-robin to CUs exactly as `generateWarps()`
  does today (no block-index bookkeeping needed).
- Barrier arrival tracking reverts to global, matching
  `simulationProcess()`: fires when every resident slot **system-wide**
  (across all CUs, not just one) is `STALLED` at the same `barrier_id`.
- The `WarpSlot` struct itself (§10.5) is unaffected by this reversal —
  same fields, same role. What changes is only how slots get assigned
  (once, for the whole kernel, not per-block) and how the barrier check is
  scoped (system-wide, not per-CU's 4 slots).

This is a smaller, simpler design than §10.5's — reversing the block
decision removed complexity, it didn't add any back.

### 10.7 Corrected state design (§10.6 follow-through) + step 3 (FSM ↔ `compute_pipeline`)

**One structural consequence of restoring global scope, made concrete
here**: barrier resolution can no longer be a purely per-CU-local decision
(§10.5's design was self-contained per CU). Something needs a system-wide
view of arrivals. Rather than having every CU poll every other CU, this
adds one small new piece — a central arrival counter — instead of N
CUs each needing visibility into every other CU's state:

```
                 ┌─────────────────────────────────────────┐
                 │           BarrierArbiter (new)           │
                 │  total_warps_   (latched at kernel launch)│
                 │  stalled_count_ (++ on every STALLED event)
                 │  done_count_    (++ on every DONE event) │
                 │  launch_fault_  (set if total_warps_ >   │
                 │                  NUM_CUS*MAX_WARPS_PER_CU)│
                 └───────▲─────────────────────────┬─────────┘
                         │ {cu_id,slot_id,state}    │ release
                         │ event stream             │ broadcast
        ┌────────────────┴───┐               ┌──────┴──────────────┐
        │  CuDispatchUnit 0   │   . . .       │  CuDispatchUnit N-1  │
        │  slots_[MAX_WARPS_  │               │  slots_[MAX_WARPS_   │
        │         PER_CU]     │               │         PER_CU]      │
        └──────────┬──────────┘               └───────────┬──────────┘
                    │ warp_dispatch_t                       │
                    ▼                                       ▼
        ┌─────────────────────┐                 ┌──────────────────────┐
        │ compute_pipeline 0   │                 │ compute_pipeline N-1  │
        │ (free-running)       │                 │ (free-running)        │
        └──────────┬───────────┘                 └───────────┬───────────┘
                    │ warp_status_t (slot_id, DONE|STALLED(bid))
                    └── (fed back to its own CuDispatchUnit AND, as an
                         event, up to BarrierArbiter)
```

**`WarpSlot`** — unchanged from §10.5 (same fields: `warp_id`, `resume_pc`,
`barrier_id`, `state ∈ {EMPTY,READY,STALLED,DONE}`, `regs`). Reversing the
block decision didn't touch this struct, only how slots get assigned and
who decides when `STALLED → READY` happens.

**`CuDispatchUnit`** (renamed from `BlockScheduler` — "block" no longer
applies): `slots_[MAX_WARPS_PER_CU]` (`MAX_WARPS_PER_CU` replaces
`MAX_WARPS_PER_BLOCK` — same value, same golden-`max_warps`-default origin,
different role: a hard per-launch capacity ceiling, not a barrier-group
size). At kernel launch, every warp the CU owns gets assigned a slot for
the **whole kernel**, not one block at a time: `active_count_` and each
slot's `warp_id` are computed once from `(cu_id, total_warps_, NUM_CUS)` —
same round-robin arithmetic `generateWarps()` already uses. Dispatch loop
unchanged from §10.5 (round-robin over `READY` slots). `STALLED→READY`
transitions are no longer decided locally — they're driven by
`BarrierArbiter`'s broadcast.

**`BarrierArbiter`** (new, one instance, system-wide): latches
`total_warps_` at launch; **checks the §10.6 hazard mitigation here**
(`total_warps_ > NUM_CUS*MAX_WARPS_PER_CU` → set `launch_fault_`, don't
launch — the register-exposed capacity check, plus a hardware-side fault
flag as defense in depth beyond the host's own pre-launch check). Consumes
a `{cu_id, slot_id, new_state}` event from every `CuDispatchUnit` on every
`DONE`/`STALLED` transition; `stalled_count_ == total_warps_` → broadcast
release (matches `simulationProcess()`'s `barrier_queue.size() ==
total_warps_` exactly — global, bit-faithful); `done_count_ ==
total_warps_` → kernel complete.

**Inherited assumption, not a new constraint**: this only terminates for
kernels where *every* warp hits the same shared barrier before completing,
or *no* warp ever does. A kernel where some warps finish without ever
reaching a barrier while others do would hang `simulationProcess()` itself
in software (`stalled_count_`/`barrier_queue.size()` would never reach
`total_warps_`, and nothing else drives the loop forward) — this is an
existing property of the golden model we're porting faithfully, not a gap
introduced here. Worth a note in §7 once multi-warp-barrier kernels are
under test, so it's documented as a kernel-authoring requirement rather
than rediscovered as a surprise.

**Step 3 answer, using this structure**: `compute_pipeline` becomes
free-running/stream-driven (Option B from the original step-3 discussion —
matches `memory_pipeline`'s existing pattern, keeps both blocks
independently resource-measurable). Dispatch/status shape:

```cpp
struct warp_dispatch_t {
    ap_uint<3>    slot_id;           // which of THIS CU's MAX_WARPS_PER_CU slots
    warp_id_t     warp_id;
    thread_mask_t active_mask_init;
    ap_uint<16>   resume_pc;         // 0 for a fresh warp, else past the BARRIER
};
// warp_status_t (hls_types.h) gains slot_id so CuDispatchUnit knows which
// slot to update, and so the {cu_id,slot_id,state} event to BarrierArbiter
// doesn't need a separate lookup.
```

`regs[][]`'s `ap_memory` port aliases the owning `CuDispatchUnit`'s
`WarpSlot.regs` directly (no host-side buffer anymore) — exact wiring is
top-level RTL integration detail, deferred to T025 same as `m_axi` board
binding (§6.4).

**Still open, not resolved here**: step 4 (FSM ↔ `memory_pipeline`
arbitration). See §10.8 for the on-chip program store, resolved next.

### 10.8 On-chip program store

Every warp in a kernel runs the *same* program, loaded once by the host at
launch — not per-warp, the way the old per-invocation `instr_in` stream
implied. Sizing: `MAX_PROGRAM_LEN=256` instructions (§6.2, still an open
placeholder on its own, unrelated to this decision).

**Shared-with-arbitration vs. per-CU replicated — replicated wins here.**
A single shared program BRAM would need `NUM_CUS` simultaneous read ports
(compute_pipeline instances run independently; nothing keeps them reading
the same PC at the same time), meaning either arbitration/contention or a
multi-ported memory. Replicating the program per CU instead costs
`NUM_CUS × 256 instructions × ~sizeof(Instruction)` — a few KB per CU,
negligible next to the register-file cost already flagged (§10.5) — and
removes the contention problem entirely: each CU's `compute_pipeline` gets
exclusive, single-cycle access to its own copy. Same reasoning §3.2 already
used for cache banks (parallel independent structure over one arbitrated
one), applied here.

**Consequence for `compute_pipeline`'s signature**: `instr_in` (a stream)
is no longer the right shape — it turns into a plain local array read,
indexed by an internally-tracked `pc` starting at `resume_pc`:

```cpp
void compute_pipeline(
    hls::stream<warp_dispatch_t>& dispatch_in,          // §10.7
    instr_word_t   program[MAX_PROGRAM_LEN],            // NEW - this CU's
                                                          // local copy, BRAM
    uint32_t       program_len,                          // s_axilite, set
                                                          // once at launch
    reg_t regs[MAX_WARPS_PER_CU][MAX_THREADS_PER_WARP][32], // indexed by
                                                          // dispatch_in's
                                                          // slot_id - aliases
                                                          // CuDispatchUnit's
                                                          // WarpSlot.regs
    hls::stream<mem_req_t>&  mem_req_out,
    hls::stream<mem_resp_t>& mem_resp_in,
    hls::stream<warp_status_t>& status_out
);
```

**Loading it, corrected during implementation**: originally sketched as
`BarrierArbiter`'s job here (since it already gates launch on the §10.6
capacity check) — reassigned to `CuDispatchUnit` instead once actually
implemented: `BarrierArbiter` reaching into every CU's internals to write
its program store would break the `CuDispatchUnit`/`BarrierArbiter`
decoupling §10.7 deliberately established (both independently testable).
`CuDispatchUnit` already owns `regs_` for the same "per-CU state
`compute_pipeline`'s ports alias" reason (§10.7's correction) — the program
store is consolidated there too: `CuDispatchUnit::loadProgram(instr_word_t*
program_ptr, uint32_t program_len)` is a plain copy loop, called once per
CU by the top-level wiring (§10.4 step 7) at kernel launch, before any warp
dispatches. Still "broadcast, not a routed transfer" (every CU gets an
independent copy of the same source, no arbitration needed) — only *which*
class issues the copy changed. `program_ptr`'s own `#pragma HLS INTERFACE
m_axi` belongs at whichever top-level kernel function owns that port, not
inside `CuDispatchUnit` itself (a plain class, never a synthesis top level,
same as `DivergenceStack` before it) — deferred to T025 same as `m_axi`
board binding (§6.4) and the `regs[][]` wiring note in §10.7.

**Verified end-to-end**, not just as an isolated copy-loop check: a smoke
test loads a real kernel (`intSaxpy`) through `CuDispatchUnit::
loadProgram()` from a simulated DRAM buffer, then runs it through a real
`compute_pipeline` instance aliased to that same `CuDispatchUnit`'s
`programArray()`/`regsArray()` — the exact wiring pattern step 7's
top-level will use at full scale — and gets the same result
`test_compute_pipeline.cpp`'s `IntSaxpy` test already established.

---

### 10.9 Step 4: FSM ↔ `memory_pipeline` arbitration

**`memory_pipeline` itself needs no changes** — confirmed by reading its
real implementation, not assumed. Two things it already does, both built
in T023, ahead of when they'd be needed:

- `handleRequest()` already copies `cu_id`/`warp_id`/`lane_id` from every
  request straight into its response (`memory_pipeline.h:88-90`).
- Its own storage is already banked per CU where banking matters:
  `shared_mem_[NUM_CUS][...]`, `l1_caches_[NUM_CUS]`
  (`memory_pipeline.h:197-199`). Only `l2_cache_` is a single shared
  instance — matches §3.1's design table exactly, not a new decision.

What's actually missing is pure plumbing *outside* both existing blocks:
`memory_pipeline` has one `req_in`/`resp_out` pair; there are now
`NUM_CUS` independent `compute_pipeline` instances, each with their own
`mem_req_out`/`mem_resp_in`. Something has to merge N→1 going in and split
1→N coming back.

**Two properties, both confirmed from the real code, make this simple:**
1. `compute_pipeline`'s `executeMemOp()` writes one request and
   **blocks** reading its response before the next lane's request
   (`compute_pipeline.cpp:139-141`, `mem_resp_in.read()` right after
   `mem_req_out.write(req)`) — never more than one outstanding request
   per CU.
2. `memory_pipeline`'s own loop reads one request, fully services it,
   and writes the response before reading the next
   (`memory_pipeline.cpp:69-72`, plain `while(true)` — no request
   overlap internally).

Together: responses come back in exactly the order requests were
accepted, and no CU ever has more than one in flight. No reordering logic,
no per-CU outstanding-request tracking needed.

**Design: `MemArbiter`**, a new small block between the CUs and
`memory_pipeline`:
- **N→1 (requests)**: round-robin poll of the `NUM_CUS` `mem_req_out`
  streams (same fairness policy already used for cache-bank replacement
  and warp dispatch elsewhere in this design) — whichever has data gets
  forwarded to `req_in` untouched (`cu_id` is already set by the sending
  `compute_pipeline`).
- **1→N (responses)**: read `resp_out`, inspect `resp.cu_id`, forward to
  that CU's `mem_resp_in`. A plain switch/case demux — no reordering
  possible given the two properties above.
- Backpressure is automatic: a CU whose request hasn't been serviced yet
  is already blocked on its own `mem_resp_in.read()` (property 1), so it
  can never produce a second request for the arbiter to buffer.

No change to `memory_pipeline.cpp`/`.h`. Resource cost is small relative
to the rest of this design (some stream buffering plus an N-way mux/demux)
— not a new line item worth its own budget table entry.

---

### 10.10 Step 6: reduced host role and register set

Derived fresh from this design's own needs (§10.6-§10.9), not carried over
from `gpgpu_main`'s T050 register map — per the checklist's own instruction,
since that map was written for a different (monolithic, single-`gpgpu_top`)
design this project deliberately didn't adopt (§10.1).

**The payoff, stated plainly**: under the old host-orchestrated model
(§2.4, now superseded), the host had to re-invoke `compute_pipeline` once
per warp, including once per barrier resume — a per-warp, many-times-per-
kernel interaction. With the scheduler on-chip, the host launches once and
polls (or waits for one interrupt) once, per kernel. That's the actual
point of everything done in §10.6-§10.9.

**Register set:**

| Register | Dir | Type | Purpose |
|---|---|---|---|
| `program_ptr` | W | `addr_t` (`m_axi`) | DRAM address of the compiled kernel's instruction buffer |
| `program_len` | W | `uint32` | Instruction count (≤ `MAX_PROGRAM_LEN`) |
| `total_warps` | W | `warp_id_t` | `grid_x × grid_y`, precomputed host-side — matches how warp ids are actually consumed downstream (linear 0..total_warps-1, §10.7); no on-chip use for the 2D shape itself |
| `start` | W (strobe) | 1 bit | Pulse to begin the launch sequence (§10.8's broadcast program load, then `BarrierArbiter`'s §10.6 capacity check, then dispatch begins) |
| `status` | R | bitfield | `busy` / `done` / `fault` (`fault` = the §10.6 hazard mitigation tripped) |
| `max_resident_warps` | R (static capability) | `warp_id_t` | `NUM_CUS × MAX_WARPS_PER_CU` — lets the host validate `total_warps` *before* writing `start`, so the common case never needs to hit the hardware fault path at all |
| IRQ line | — | — | Optional, asserted alongside `done`/`fault` — alternative to polling `status` |

**Launch sequence:**
1. Compile the kernel, write its instructions to DRAM, note the address and length.
2. Read `max_resident_warps`; confirm `grid_x·grid_y ≤` it (§10.6's check, done host-side first — the hardware check is defense in depth, not the primary gate).
3. Write `program_ptr`, `program_len`, `total_warps`.
4. Write `start = 1`.
5. Poll `status` (or wait for the interrupt) until `done` or `fault`.
6. `fault` → the launch was invalid (shouldn't happen if step 2 was done correctly); no results were produced. `done` → read results back from wherever the kernel's own instructions wrote them (global/shared memory), same as today — no new register needed for this, addresses are already known from compiling the kernel.
7. Next kernel: repeat from step 1 — every `CuDispatchUnit`'s slots and `BarrierArbiter`'s counters reset fresh on each `start`.

Per-warp registers from the old §2.2 signature (`cu_id`, `warp_id`,
`active_mask_init`, per-invocation `program_len`) are gone from the host's
view entirely — they're now computed internally by each `CuDispatchUnit`
at launch time (§10.7), not written by software at all.

---

### 10.11 Step 8: `NUM_CUS`, L2→URAM, KV260 AXI port — revisited with real data

Unlike §10.7-§10.10 (design only — none of §2.5's new components are
implemented or synthesized yet), this step draws on **real** `vitis_hls`
synthesis reports already sitting in the repo from T020
(`build/fpga_smoke/kv260_{compute,memory}_pipeline/solution1/syn/report/
{compute,memory}_pipeline_csynth.rpt`) — the first point in this whole §10
arc grounded in measured numbers instead of a projection of something not
yet built.

**Measured, KV260 (`xck26-sfvc784-2LV-c`), `NUM_CUS=1` (today's only
synthesized config):**

| | BRAM_18K | DSP | FF | LUT |
|---|---|---|---|---|
| `compute_pipeline` alone | 0 | 260 (20%) | 38,141 (16%) | 61,772 (**52%**) |
| `memory_pipeline` alone | 176 (61%) | 0 | 6,594 (2%) | 6,471 (5%) |
| **Combined (today)** | 176 (61%) | 260 (21%) | 44,735 (19%) | 68,243 (58%) |
| Device total | 288 | 1,248 | 234,240 | 117,120 |

**`NUM_CUS`: decided — 1, for KV260.** `compute_pipeline` alone already
takes 52% of the chip's LUT budget. Projecting `NUM_CUS=2` (two
`compute_pipeline` instances + one shared `memory_pipeline`, per §2.5):
LUT = `2×61,772 + 6,471 = 130,015` → **111% of the LUT budget —
infeasible**, before even counting the new `CuDispatchUnit`/
`BarrierArbiter`/`MemArbiter` control logic from §2.5. FF (35%) and DSP
(42%) would both still fit comfortably; LUT alone decides this, no
marginal estimate needed. This directly confirms the resource concern
raised earlier this session: `compute_pipeline`'s cost — unrolled
32-lane ALU/vector/divergence logic — is what makes it LUT-expensive, and
*that* is the real optimization target once RTL exists, not the cache
hierarchy or the new FSM.

**U55C: was left genuinely open here** (no device support installed, no
measured baseline to project `NUM_CUS` from) — **superseded by §14: U55C was
discarded before that gap got filled.**

**L2 → URAM: still recommended, reprioritized down, not dropped.** With
`NUM_CUS=1` decided, BRAM has real headroom (61% today; §2.5's new
per-CU storage — `WarpSlot.regs` at 16KB plus the program store at
≈4-5KB, roughly 9 more `BRAM_18K` blocks — brings it to roughly 64%,
still comfortable). The URAM move was originally motivated by wanting
BRAM headroom to support *more* CUs; since LUT is what actually blocks
more CUs, not BRAM, it doesn't unlock anything today. It matters again
the moment `compute_pipeline`'s LUT footprint gets optimized down enough
to make `NUM_CUS=2` LUT-feasible — at which point BRAM becomes the next
constraint — but it's off the critical path for now.

**KV260 `m_axi` port: reversed to HP, from the earlier HPC lean.** The
earlier reasoning (before §2.5 existed) leaned HPC — cache-coherent with
the PS — specifically because that design had the host re-invoking
`compute_pipeline` once per warp, including every barrier resume: frequent,
fine-grained host/device interleaving where coherency genuinely mattered.
§2.5.6 eliminates that entirely: the host now touches the device at only
two well-separated points per kernel — writing launch registers, then (after
polling `status.done`) reading results back. Nothing genuinely accesses
the same memory concurrently anymore; it's sequenced, not overlapping.
That's the standard case for non-coherent HP plus one explicit software
cache-invalidate (or a non-cacheable buffer mapping) right before the
host's post-completion read — an ordinary, low-cost step on Zynq designs,
not something that needs hardware coherency to solve. HP is cheaper and
doesn't pay for a coherency mechanism this design no longer exercises.
Stated plainly because it contradicts the earlier "I agree with using
HPC" exchange — that answer was right for the design that existed then;
the design has genuinely changed since.

**§10.4's 8-step *design* plan is fully worked through (steps 1-8,
§10.3-§10.11).** What follows (§10.12) is real *implementation* against
that design, done in a separate session pass — its own procedure, numbered
independently of §10.4's steps.

---

### 10.12 Implementation: `CuDispatchUnit`/`BarrierArbiter`/`MemArbiter`/`compute_pipeline`/`GpgpuTop`, all built and verified

Real code now exists for every component in §2.5.2, each checked against
the real Vitis HLS headers (not just reasoned about) and verified via
actual execution, not only compilation:

| File | What | Verified how |
|---|---|---|
| `hls/src/common/hls_config.h`/`hls_types.h` | `MAX_WARPS_PER_CU`, `WarpSlot`, `warp_dispatch_t`, `warp_status_t` (+`slot_id`/`resume_pc`) | Compiles against real headers; `MAX_CONCURRENT_BARRIERS` removed (grep-confirmed zero consumers first) |
| `hls/src/scheduler/cu_dispatch_unit.h` | `CuDispatchUnit` - resident slots, dispatch, program store (§10.8, reassigned here from `BarrierArbiter`) | Smoke test: launch assignment, FIFO order, barrier cycle, program load through to a real `compute_pipeline` run |
| `hls/src/scheduler/barrier_arbiter.h` | `BarrierArbiter` - capacity hazard check, global arrival counting | Smoke test against real `CuDispatchUnit` instances: hazard mitigation, full barrier wave, non-uniform-participation safety |
| `hls/src/scheduler/mem_arbiter.h` | `MemArbiter` - N:1/1:N routing | Smoke test with real `hls::stream`s (fairness among >1 source not exercisable at `NUM_CUS=1`, stated plainly not glossed over) |
| `hls/src/compute_unit/compute_pipeline.{h,cpp}` | Rewritten to §2.5.3's free-running shape; every ALU/vector/memory/branch/join helper reused byte-for-byte from the original per-invocation version | **All 11 pre-existing `tests/hls/` GTests still pass, unchanged expected values** (`test_compute_pipeline.cpp` x8, `test_pipeline_integration.cpp` x3) |
| `hls/src/scheduler/gpgpu_top.h` | `GpgpuTop`/`schedulerStep()` - the orchestration loop tying `CuDispatchUnit`/`BarrierArbiter` to real per-CU dispatch/status streams; `gpgpu_scheduler()`, the free-running top-level wrapper | Capstone smoke test, below |

**Two more real findings from this pass**, beyond the `WarpSlot`/`enum`/
`constexpr` issues already noted in §2.5.7:
- `CuDispatchUnit` has no "dispatched, awaiting result" slot state (only
  `EMPTY`/`READY`/`STALLED`/`DONE`) - asking it for a ready slot twice
  without an intervening `recordResult()` would return the same slot
  twice. `simulationProcess()` never hits this (one dispatch per CU per
  round, synchronously) - the orchestration layer (`GpgpuTop::cu_busy_`)
  carries the same one-outstanding-per-CU discipline instead of adding new
  slot state, keeping `CuDispatchUnit` itself unchanged.
- The program-store load assignment correction (§10.8): moved from
  `BarrierArbiter` to `CuDispatchUnit` once actually building the code
  made the decoupling violation concrete.

**Capstone verification**: a test drives a real two-warp
`parallelReduction` kernel (the same barrier-heavy kernel
`test_pipeline_integration.cpp`'s `ParallelReductionAcrossTwoWarpsWithBarrier`
already covers) through the **entire** on-chip pipeline - `GpgpuTop` +
`schedulerStep` + `CuDispatchUnit` + `BarrierArbiter` + `MemArbiter` + real
`compute_pipeline` + real `memory_pipeline` - with **zero test-side
orchestration** of dispatch timing or barrier arrival/release (unlike
every earlier barrier test in this project, which manually sequenced
"dispatch, wait, dispatch, wait, resume both"). The scheduler decides
everything on its own via `schedulerStep()`'s loop. Result matches the
golden model exactly (register file and backing memory both checked,
same values `regression_test.cpp`'s Phase 11a and this project's own
earlier integration test already established). This is the concrete proof
that the architecture decided across §10.1-§10.11 - the whole point of
this session's redesign - actually works, not just that its pieces
compile individually.

**What remains before T025 (RTL generation)**:
1. Formalize the `CuDispatchUnit`/`BarrierArbiter`/`MemArbiter`/`GpgpuTop`
   smoke tests into real `tests/hls/` GTest files (currently ad-hoc scratch
   tests run during implementation, not checked into the test suite).
2. A real `vitis_hls` synthesis run of the complete result, to get actual
   (not projected) resource numbers for `NUM_CUS=1` plus everything in
   §2.5, and to confirm the §2.5.4 finding that `MAX_CONCURRENT_BARRIERS`
   is safe to remove from `hls_config.h`.
3. The cross-kernel `regs[][]`/`program[]` BRAM-aliasing and `program_ptr`
   `m_axi` binding questions this file's own header flags as genuine
   system-integration work (T025 scope, same category as board-specific
   `m_axi` binding, §6.4) - real hardware wiring between separately
   synthesized `compute_pipeline` instances and the scheduler's owned
   state, not resolvable at the C++/csim level this session worked at.

---

## 11. Deferred: software/HLS unification audit

**Status**: not started, deliberately. Decision (this session): keep both
sides moving independently and run one unification pass once software and
hardware are each further along, rather than reconciling now. Logged here
so the audit has a concrete starting checklist instead of starting from
scratch. Neither item below is a regression introduced by this session's
work - both are pre-existing forks in the golden-model repo that this
port has been correctly, faithfully targeting one side of all along.

**Two READMEs exist describing "the SystemC model" - only one is current,
confirmed by commit history, not guesswork.** `Proyecto/README.md` (repo
root) was last touched by commit `9c4dfea` "GPGPU READY" - the exact
commit `init_gpgpu`, and this entire HLS port, started from. It describes
only the Virtual ISA path and its own "Known limitations" table states "No
ISA decoder... A binary RISC-V decoder is outside the scope of the
SystemC model" - true *at that commit*, before the binary-execution mode
existed. `Proyecto/riscv_gpgpu/models/systemc/README.md` was last touched
by commit `6603937` "cleanup and updates," part of the
`gpgpu/codesign_dmedina` branch merged in afterward (`683a6c3`), which is
what actually added `integration/`
(`elf_loader.{h,cpp}`/`kernel_bridge.{h,cpp}`/`riscv_isa.h` - confirmed to
really exist with real content) alongside the original Virtual ISA path,
not replacing it. **`models/systemc/README.md` is the current, accurate
one** - cross-reference it, not the root-level one, which is stale and
should either be updated or removed during the unification pass.

**Authoritative source, read this first**: `models/systemc/README.md`
already documents the two-execution-mode split explicitly and clearly -
"Virtual ISA" vs. "Binary execution," an ISA note at the top stating
`VADD`/etc. "are **not** RISC-V V (RVV) instructions," and a "Known
limitations" entry confirming RVV is unimplemented, matching §11.1 below
independently. This isn't an undocumented gap - it just isn't
cross-referenced from `docs/architecture/isa.md`, `ARCHITECTURE.md`, or
this file, which is what actually caused the confusion this section exists
to resolve. Link it from those places during the unification pass.

### 11.1 Two incompatible program representations/execution models

The SystemC golden model contains **two independent execution paths**,
and the software toolchain and this HLS port target different ones:

- **The SIMT path** — `WarpScheduler`/`ComputeUnit`/`SIMTController`/
  `GPGPUTop` (`models/systemc/src/{scheduler,compute_unit,simt_controller,
  top}/`). This is what `docs/hls/interfaces.md` has cited as "Golden
  reference" from the start, and what every kernel in
  `kernel_programs.h` and every HLS test in `tests/hls/` is written
  against. Programs are a custom, project-internal `Instruction` struct
  (`pc`/`opcode`/`rs1`/`rs2`/`rd`/`imm`/3 flags, `hls_types.h`'s
  `instr_word_t`) with a made-up opcode numbering
  (`ADD=0x00`...`VADD=0x40`...`BARRIER=0x70`) - never assembled from or
  decoded out of any real instruction encoding. Only ever hand-built in
  C++ (`kernel_programs.h`'s `makeInstr()`).
- **The binary-execution path** — `riscv_isa.h`'s real RV32 decoder +
  `kernel_bridge.cpp`, which loads and executes real ELF binaries. This is
  what the actual software chain targets: `software/llvm/backend/
  llvm_backend.cpp` compiles via `clang -target riscv32-unknown-elf
  -march=rv32gc -mabi=ilp32` (scalar RV32GC - **no RVV, no vector
  extension at all**, confirmed by reading the actual compile command);
  `driver/src/loader.cpp`'s `KernelLaunchArgs` carries an `entry_symbol`
  for ELF symbol resolution, matching `kernel_bridge.cpp`'s loading
  scheme, not the SIMT path's warp-centric launch model.

**Not just an encoding gap - a paradigm gap.** Even setting the binary
format aside, `compute_pipeline`'s vector opcodes (`VADD`/`VFMADD`/etc.)
model a SIMT/GPU-warp execution style: 32 independent per-lane scalar
register files with `VBRANCH`/`VJOIN` masking. Real RVV's model (single
hart, configurable-length vector registers, `vsetvli`/LMUL grouping) is
architecturally different even as a concept, not merely a different
encoding of the same idea - so real RVV adoption is itself a design
project, not a drop-in fix, regardless of the decision below.

**RVV status, corrected**: not literal text in `isa.md`/`spec.md`/`plan.md`,
and not yet implemented anywhere in the repo (`models/systemc/README.md`:
"RVV support is a future work item") - but **confirmed as the intended
target**, not an open three-way choice. Both project READMEs open by
describing this as a "Ventus-inspired RISC-V GPGPU" - Ventus is a real,
published RISC-V GPGPU architecture that uses RVV as its actual SIMT
execution mechanism, and that stated lineage is what settles this,
independent of whether the word "RVV" appears in the task tracker. The
custom Virtual-ISA opcodes (`VADD`/`VBRANCH`/etc.) and `software/README.md`'s
"custom SIMT instruction encodings are pending" note are earlier
placeholders/scaffolding standing in for real RVV, not a competing
direction - §12 step 5 no longer treats this as open.

**Independently confirmed, not just inferred from README wording** (checked
directly): `Proyecto/docs/diagrams/diagramas_segundo_lvl.puml` is a real
architecture diagram, not prose - `Instruction Fetch → Instruction Decode →
Warp Scheduler → SIMT Controller → Compute Units → RVV Execution Unit →
Register File`, with a literal component box named "RVV Execution Unit."
`Proyecto/docs/Papers/` contains two Vortex papers
(`Vortex_OpenCL_Compatible_RISC-V_GPGPU.pdf`, `vortex_micro21_final.pdf`) -
Vortex is a second real, published RVV-based open-source RISC-V GPGPU
alongside the Ventus inspiration, not the same project cited twice. The
diagram's `Warp Scheduler`/`SIMT Controller`/`Barrier` boxes map directly
onto `CuDispatchUnit`/`BarrierArbiter`/`divergence_stack.h` (T022c) -
confirms that part of this session's work is correctly scoped;
`Instruction Fetch`/`Decode`/`RVV Execution Unit` map onto T022b exactly.
Also checked exhaustively: zero real RVV encoding anywhere in the repo
today (`git grep` for any `-march` flag containing `v`, `zve32x`,
`vsetvli`, etc. across the whole branch - none). Architecture is
consistently documented as RVV-based across three independent sources;
nothing implements it yet.

**Re-verified once more, directly against `gpgpu/main`'s own
`Proyecto/README.md`** (the specific file a later team check pointed at as
authoritative) **and its own `compute_unit.cpp`, not just inferred**: same
conclusion, not a different one. That README's own "Known limitations"
table states "No ISA decoder... a binary RISC-V decoder is outside the
scope of the SystemC model," and its own opcode table calls the vector ops
"RVV-**style**" (not "RVV") - it agrees with §11.1's finding once read past
the opening line, it doesn't contradict it. `gpgpu/main`'s actual
`executeVector()` confirms directly: plain arithmetic on
`const Instruction&` struct fields (`ctx.regs[t][instr.rd] = a + b`), no
bit-field decode, no vector register file, no `vsetvli`/LMUL/SEW - the
same custom scheme found on every other branch checked. "RVV-based"
correctly describes the intended architecture (§11.1's diagram/papers
evidence); it was never a claim that the current SystemC model implements
real RVV, on any branch, by any of its own documents' own wording.
Confirms T022b's scope, changes nothing about
the plan.

**A second, narrower mismatch inside the binary-execution path itself**:
`models/systemc/README.md` and `riscv_isa.h`'s own header comment both say
to compile with `-march=rv32im` ("Does NOT decode RVC (compressed)
instructions"), but `software/llvm/backend/llvm_backend.cpp` actually
compiles with `-march=rv32gc` - which adds compressed instructions and
float/double, none of which `riscv_isa.h` claims to decode. Independent of
the Virtual-ISA-vs-binary question above; worth checking whether any
compiled kernel actually emits a compressed or F/D instruction the decoder
can't handle, or whether this has simply never been exercised.

**Consequence**: a kernel compiled by the team's real toolchain today
produces a binary this HLS port's `compute_pipeline` cannot execute at
all - not incorrectly, there is simply no decode path from real RV32GC
machine code into `instr_word_t`. Also affects launch-parameter shape:
`KernelLaunchArgs`' CUDA-style `grid_x/y/z` × `block_x/y/z` doesn't map
onto `GPGPUTop::launchKernel(grid_x, grid_y, ...)`'s convention, where
`grid_x × grid_y` directly enumerates warps (fixed 32 threads/warp, no
separate block concept) - the two sides don't even agree on what
"grid"/"block" mean.

**What the unification audit needs to decide**: either (a) a real compiler
backend emitting the SIMT path's `instr_word_t` encoding directly, (b) a
hardware or host-side decoder translating real RV32GC (+ some vector
extension, RVV or custom) into `instr_word_t`, or (c) redesigning
`compute_pipeline`'s decode stage to consume real RISC-V machine code
instead of the custom encoding - each a substantial project, not a small
patch. Not decided here; flagged for that later pass.

### 11.2 Memory-tier sizing doesn't match the golden model's real defaults

`hls_config.h`'s `SHARED_MEM_SIZE_BYTES`/`L1_SIZE_BYTES`/`L2_SIZE_BYTES`
(48KB/16KB/256KB) were sized against `config/arch_config.yaml` - but
**nothing in the codebase actually reads that yaml file** (grep-verified:
zero references anywhere in `models/`, `config/`, `software/`,
`runtime/`). It's descriptive documentation, never wired to anything live.
The golden model as `regression_test.cpp`/`benchmark_test.cpp` actually
instantiate and run it (i.e., what produced every expected value this
port's tests check against) explicitly sets shared/L1/L2 to
**16KB/32KB/512KB** - `GPGPUTop::Config`'s real C++ defaults, different
numbers on all three tiers.

**Checked, not just flagged**: the shared-vs-cached address boundary in
both models is `address < shared_mem_size`. Every address this port's
`tests/hls/` suite actually exercises (`0x1000`, `0x10000`-range,
`0x200000`) falls outside the disputed 16KB-48KB gap on both sides, so
this has not caused any wrong test result *yet*. It's real latent risk,
not realized harm: a future kernel touching an address in that gap, or
any L1/L2-capacity-sensitive test, could disagree with the real golden
model. Decided (this session): leave `hls_config.h`'s sizing as-is for
now rather than reconcile immediately; revisit at the same unification
pass as §11.1, since `NUM_CUS`/resource-budget numbers (§10.11) are
already sized against the current (yaml-based) constants and would need
re-checking together, not this constant in isolation.

### 11.3 RECONSIDERED — `compute_pipeline`'s target stands, correction not applied

**Final decision (confirmed directly): keep `ComputeUnit::executeWarp()` as
the target. No code changes needed.** This section originally concluded
the opposite - kept below unedited for the record, since the underlying
fork it found is real and stays useful for the later unification audit.
What changed is the verdict, not the facts:

- **Verified, precisely, not just re-asserted**: `hls_types.h`'s `Opcode`
  enum matches `types.h`'s real `Opcode` enum value-for-value (`ADD=0x00`
  through `HALT=0xFF`, every entry, checked directly against `gpgpu/main`,
  not the README's transcription of it) - `compute_pipeline` already
  implements exactly what the golden model's own source file defines.
- **Decision**: the SystemC model's own documented functionality is the
  standard this port complies with, independent of whether that
  functionality happens to be real-RISC-V/RVV-compliant. `models/systemc/
  README.md` is itself one specific snapshot, not necessarily the most
  current framing of that functionality - but `types.h` (checked directly
  above) is unambiguous either way.
- **`hls/README.md`'s "port `ComputeUnit::step()`" instruction** (this
  section's original finding) is acknowledged as real text on
  `gpgpu/codesign_dmedina`, but is not adopted as a requirement for this
  port - it describes a different target (real RV32I binary execution),
  not a correction to the Virtual-ISA path this port has always used.
- **Practical consequence**: T022 (tasks.md) reverts to done, T022b is
  closed rather than pursued, T025 (RTL generation) is unblocked. Real
  RV32I/RVV compliance, if ever pursued, is a separate, non-blocking
  future effort - not a prerequisite for taking the current port through
  Vitis HLS.

**Original finding, kept intact below for the record:**

**This is not deferred like §11.1/§11.2 - it needs fixing before further
HLS work continues.** `models/systemc/README.md`'s own two-mode framing
(§11.1) left it ambiguous which mode HLS should target. It isn't ambiguous:
`Proyecto/riscv_gpgpu/hls/README.md` (on `gpgpu/codesign_dmedina`, the
branch holding the authoritative software-side documentation - confirmed
by direct diff against `gpgpu/main`, see below) states plainly, under
"Phase 1 — Compute pipeline (T022)":

> "Port `ComputeUnit::step()` + `executeRV32()` from
> `src/compute_unit/compute_unit.cpp`."

That is the **binary RV32I execution path**. This port's
`compute_pipeline.cpp` - both the original T022 version and this session's
free-running rewrite (§2.5) - is instead a direct port of
`ComputeUnit::executeWarp()`, the **Virtual ISA/SIMT custom-opcode path**.
Every "Golden reference" citation throughout this document (§2, §3, §10)
points at the wrong one of the two `ComputeUnit` entry points relative to
the project's own documented plan.

**Corroborating evidence, not just one document**: `Proyecto/riscv_gpgpu/
README.md`'s top-level pipeline diagram draws a single path - compiled ELF
→ `KernelBridge` → RV32I fetch/decode/execute → *(future)* FPGA via
`hls/`→`rtl/`→`fpga/` - no branch for the Virtual ISA path at all.
`software/README.md` states the call chain explicitly: `host_api →
runtime → driver → KernelBridge (simulation) or FPGA AXI driver
(hardware)`. Both agree with `hls/README.md`.

**Reuse assessment - most of this session's work stands.** Verified by
reading the actual unmerged diff (`codesign_dmedina`'s tip, `f26f576`,
"Start of final stage - codesign for reconvergence semantics" - not yet on
`gpgpu/main`, see §11.4), not assumed:

| Component | Verdict | Why |
|---|---|---|
| `CuDispatchUnit`/`BarrierArbiter`/`MemArbiter`/`GpgpuTop` (§2.5, §10.12) | **Fully reusable** | `hls/README.md`'s own Phase 3 calls for porting `WarpScheduler::selectWarp()` as "a small FSM" - independent of which `ComputeUnit` variant it dispatches to. Nothing here assumed the custom opcode format. |
| `memory_pipeline`/`MemArbiter` request/response protocol | **Fully reusable** | Orthogonal to instruction encoding (confirmed already in §10.9). |
| `divergence_stack.h` (SIMTController port) | **Reusable, small additive extension needed** | `f26f576`'s real diff adds exactly one new optional parameter to `SIMTController::handleBranch()`: `reconvergence_pc = 0` ("virtual-ISA path uses explicit VJOIN instead" - direct quote from the diff's own comment), plus a `getReconvergencePC()` query. Backward-compatible, not a redesign. `divergence_stack.h` needs the same shape of extension. |
| `compute_pipeline`'s 32-lane-parallel-with-masking *structure* | **The valuable part** | This is exactly the SIMT capability the binary-execution path is documented as missing (`models/systemc/README.md`: "Binary mode: single-thread per CU... SIMT lane masking and divergence events are not generated"). Not wasted work - it's the missing piece the other path needs. |
| `compute_pipeline`'s *decode stage* (the custom `Instruction` struct / `Opcode` enum, `hls_types.h`'s `instr_word_t`) | **Needs rebuilding** | Must become real RV32I bit-field decode (`opcode = instr & 0x7F`, etc., matching `riscv_isa.h`'s real decoder) instead of consuming a hand-built C++ struct. The execute-stage arithmetic once decoded (`regs[t][rd] = a+b`) mostly carries over. |
| `hls_config.h`/`hls_types.h`'s general shape (constants, `reg_t`/`addr_t`/etc.) | **Mostly reusable** | `instr_word_t`'s specific shape changes; the rest (memory/scheduler types) doesn't depend on decode format. |

### 11.4 `gpgpu/main` is missing the commit that motivated §11.3

Checked directly, not assumed: `git merge-base --is-ancestor
origin/gpgpu/codesign_dmedina origin/gpgpu/main` fails - `gpgpu/main` does
**not** have all of `codesign_dmedina`. The one missing commit is exactly
`f26f576` (§11.3's reuse-assessment evidence), which also touches
`kernel_bridge.cpp`, adds `ComputeUnit::getCurrentPC()` ("Used by
`KernelBridge` for per-warp PC-divergence detection"), and adds new
CMake infrastructure (`cmake/RiscvKernel.cmake`,
`cmake/FindSystemC.cmake`, benchmark `CMakeLists.txt` files) not present
on `gpgpu/main` at all. **Do not treat `gpgpu/main` as containing
everything relevant** - pull `codesign_dmedina` in directly (or wait for
it to merge) before starting §12's realignment work, not just cite it
from a distance.

---

## 12. Realignment strategy — NOT CURRENTLY ADOPTED, see §11.3

**§11.3 reconsidered the correction this section implements a plan for.**
`compute_pipeline`'s `ComputeUnit::executeWarp()` target stands; this
9-step plan is not scheduled. Kept below, unedited, in case real
RV32I/RVV compliance becomes a requirement later (the later unification
audit §11 already anticipated) - at that point this is a ready-made
starting plan, not something to re-derive from scratch.

**Decision (original, superseded by §11.3)**: fix documentation and plan
now; do the actual rework in a fresh session, to keep this correction from
being rushed alongside everything else already in flight. This section is
that plan - concrete enough that the next session can start step 1
immediately rather than re-deriving context.

1. **Pull `codesign_dmedina` (or at least commit `f26f576`) into the local
   working state before anything else.** §11.4 - the real target
   (`ComputeUnit::step()`/`executeRV32()`) and its reconvergence-PC
   extension aren't fully available via `gpgpu/main` alone.
2. **Read `ComputeUnit::step()`/`executeRV32()` in full** (not a summary)
   - this is now the actual golden reference for `compute_pipeline`'s
   execute stage, same rigor this project has applied everywhere else
   (§10.3's "read the full implementation, not summaries" precedent).
3. **Read `f26f576`'s `simt_controller.cpp` diff in full** (this session
   only read the `.h` - see §11.3's table), specifically how
   `reconvergence_pc` actually gets computed/supplied by the binary-mode
   fetch loop, before designing `divergence_stack.h`'s extension.
4. **Extend `divergence_stack.h`** with `reconvergence_pc`/
   `getReconvergencePC()`, mirroring `f26f576`'s shape as closely as
   sensible. Small, additive - not a rewrite (§11.3).
5. **Redesign `compute_pipeline`'s decode stage**: real RV32I bit-field
   decode (`opcode`/`rd`/`rs1`/`rs2`/`imm` extraction matching
   `riscv_isa.h`) feeding the existing per-lane execute logic. **Decided
   (§11.1): real RVV, not an open three-way choice** - this project is
   explicitly "Ventus-inspired" per both project READMEs, and Ventus is a
   real published RISC-V GPGPU that uses RVV as its actual SIMT execution
   mechanism. So the scalar RV32I base decode is straightforward
   (lockstep, all 32 lanes execute the same instruction, matching the
   real-GPU pattern and `riscv_isa.h`'s existing decoder), but the vector
   lanes need a **real RVV decode stage** - `vsetvli`/LMUL-aware, not the
   current `VADD`/`VBRANCH`-style custom opcodes, which are earlier
   placeholders for this, not a competing design. This is real, substantial
   new design work (RVV encoding is genuinely more complex than the
   current custom scheme - configurable vector length, element width,
   grouping) - budget for it accordingly, don't treat it as a rename of
   existing opcodes. Sketch against `kernel_bridge.cpp`'s actual per-warp
   dispatch loop before committing to specifics.
6. **Reconcile register/ABI convention**: this port's `r0=0`/`r1=global_tid`/
   `r2=addr`/`r3=local_warp_id` convention (§2.2) vs. real RV32I ABI
   (`x0=zero`, `a0-a7` args) - `models/systemc/README.md`'s binary-mode
   register table is the reference.
7. **`CuDispatchUnit`/`BarrierArbiter`/`MemArbiter`/`GpgpuTop` audit**:
   confirm none of them actually depend on the custom `instr_word_t` shape
   beyond the program-store array type (§10.8) - expected to need only
   that type swapped for a raw 32-bit-word array, not structural changes,
   but verify by reading, not assuming (§11.3's table is this session's
   best assessment, not a verified fact for these specific files).
8. **Tests**: `kernel_programs.h`-based hand-built `Instruction` tests
   (§9, all of `tests/hls/`) need real compiled RV32I kernels as a
   supplement or replacement once decode changes - existing tests validate
   the wrong input format going forward, even though the golden-model
   comparisons they were checked against remain valid data points.
9. **Only after 1-8**: re-run `NUM_CUS`/URAM/AXI-port (§10.11) and the
   `MAX_CONCURRENT_BARRIERS` removal (§2.5.4) sanity-check against
   whatever resource picture the corrected `compute_pipeline` produces -
   §10.11's numbers were measured against the pre-correction
   `compute_pipeline` and may no longer hold.

---

## 13. RV32I + custom-opcode encoding (adopted pivot — implemented, §13.12)

### 13.1 Relationship to §11/§12 — this is a narrower, different move

**Do not confuse this with §12's plan.** §12 was a wholesale golden-reference
switch: retarget `compute_pipeline` at `ComputeUnit::step()`/`executeRV32()`
(the *binary execution path*, §11.1), pull in `codesign_dmedina`, and design
a **real RVV** vector decode stage. §11.3 reconsidered that and it is **not
adopted** — `ComputeUnit::executeWarp()` (the *virtual-ISA/SIMT path*, §11.1)
stands as the golden reference, unchanged.

This section is a different, self-contained decision: give the **existing**
opcode semantics (`Opcode` enum in `hls_types.h`, exactly mirroring
`types.h`, executed by `executeALU`/`executeVector`/`executeMemOp`/
`executeBranch`/`executeJoin` exactly as already ported) a **real,
spec-legal RV32I instruction encoding**, instead of the current
host-constructed `Instruction` struct being the on-chip program
representation directly. Standard ops (`ADD`, `ADDI`, `LW`, `BEQ`, ...) get
real RV32I opcodes. GPGPU-specific ops that have no RV32I equivalent
(`VADD`, `VBRANCH`, `BARRIER`, ...) get real RISC-V **custom-opcode space**
(`custom-0`..`custom-3`, reserved by the spec for exactly this purpose — see
RISC-V ISA Manual Vol. I, opcode map). Nothing about *what* the machine
executes changes; only *how instructions are stored/transmitted on-chip*
does.

**Why this is a small, bounded change and not a rewrite** (confirmed by
reading the actual call sites, not assumed):
- `Instruction` (the decoded struct: `opcode`/`rs1`/`rs2`/`rd`/`imm`/
  `is_vector`/`is_memory`/`is_branch`) stays **exactly as it is today**.
  `executeALU`/`executeVector`/`executeMemOp`/`executeBranch`/`executeJoin`
  all consume this struct already and need **zero changes**.
- `CuDispatchUnit::loadProgram()`/`programArray()` (`cu_dispatch_unit.h:168-
  185`) only ever copy `instr_word_t` elements opaquely — they never
  inspect instruction fields. **Zero changes needed there.**
- The only places that touch instruction *representation* directly are:
  `hls_types.h` (where `instr_word_t` is defined), a new codec, the one
  fetch line in `compute_pipeline.cpp`'s `executeOneWarp()`, and whatever
  builds `program[]` arrays (today: `tests/hls/test_compute_pipeline.cpp`'s
  `toHls()`, and the equivalent in `test_pipeline_integration.cpp`/
  `test_hls_data_structures.cpp`).

### 13.2 Architectural split: decoded form vs. wire/storage form

Two distinct types, doing two distinct jobs:

| Type | Role | Shape | Status |
|---|---|---|---|
| `Instruction` (`instr_word_t` today) | **Decoded, internal.** What every execute-stage function reads. | Struct: `opcode`, `rs1`, `rs2`, `rd`, `imm`, `is_vector`, `is_memory`, `is_branch` | Unchanged |
| `raw_instr_t` (NEW — `instr_word_t` repointed to this) | **On-chip storage/wire form.** What `program[MAX_PROGRAM_LEN]` actually holds. | `ap_uint<32>`, real RV32I/custom bit encoding | New |

`decodeInstruction(raw_instr_t) -> Instruction` runs once per fetched
instruction, inside `executeOneWarp`'s loop, immediately before dispatch —
replacing today's `const Instruction& instr = program[i];` with
`const Instruction instr = decodeInstruction(program[i]);`. Everything after
that line is untouched.

`encodeInstruction(Opcode, rd, rs1, rs2, imm) -> raw_instr_t` is the
inverse, used wherever a `program[]` array is built (today: only test code
and, symmetrically, whatever compiler/assembler eventually targets this
core — out of scope here). Its parameter order deliberately matches
`makeInstr()` (`types.h:104`) so callers change from
`makeInstr(Opcode::ADDI, rd, rs1, rs2, imm)` to
`encodeInstruction(Opcode::ADDI, rd, rs1, rs2, imm)` with no reordering.

### 13.3 Standard RV32I formats used (reference)

Only the formats actually needed. Bit positions are the real RV32I spec
positions — not invented.

```
R-type (register-register: ADD/SUB/AND/OR/XOR/SLT, and both custom groups)

 31           25 24     20 19     15 14  12 11      7 6      0
+---------------+---------+---------+------+---------+--------+
|    funct7     |   rs2   |   rs1   |funct3|   rd    | opcode |
+---------------+---------+---------+------+---------+--------+
        7            5         5       3        5         7

I-type (register-immediate: ADDI, LW, JALR; custom-1's BARRIER reuses this)

 31                    20 19     15 14  12 11      7 6      0
+------------------------+---------+------+---------+--------+
|      imm[11:0]         |   rs1   |funct3|   rd    | opcode |
+------------------------+---------+------+---------+--------+
            12                5       3        5         7

S-type (store: SW)

 31           25 24     20 19     15 14  12 11      7 6      0
+---------------+---------+---------+------+---------+--------+
|  imm[11:5]    |   rs2   |   rs1   |funct3|imm[4:0] | opcode |
+---------------+---------+---------+------+---------+--------+

B-type (branch: BEQ/BNE) — imm bits scrambled, LSB implicit 0 (2-byte align)

 31 30      25 24     20 19     15 14  12 11      8 7  6      0
+--+-----------+---------+---------+------+---------+-+--------+
|12|imm[10:5]  |   rs2   |   rs1   |funct3|imm[4:1] |11| opcode |
+--+-----------+---------+---------+------+---------+-+--------+

U-type (upper immediate: LUI)

 31                                    12 11      7 6      0
+----------------------------------------+---------+--------+
|              imm[31:12]                |   rd    | opcode |
+----------------------------------------+---------+--------+

J-type (jump: JAL) — imm bits scrambled, LSB implicit 0

 31 30        21 20 19        12 11      7 6      0
+--+-------------+--+-----------+---------+--------+
|20|  imm[10:1]  |11|  imm[19:12]|   rd    | opcode |
+--+-------------+--+-----------+---------+--------+
```

### 13.4 Standard-opcode mapping (real RV32I, unmodified spec encoding)

| Opcode(s) | Major opcode | Binary | Format | Selector |
|---|---|---|---|---|
| `ADD` | OP | `0110011` | R | funct3=`000`, funct7=`0000000` |
| `SUB` | OP | `0110011` | R | funct3=`000`, funct7=`0100000` |
| `AND` | OP | `0110011` | R | funct3=`111` |
| `OR`  | OP | `0110011` | R | funct3=`110` |
| `XOR` | OP | `0110011` | R | funct3=`100` |
| `SLT` | OP | `0110011` | R | funct3=`010` |
| `ADDI` | OP-IMM | `0010011` | I | funct3=`000` |
| `LUI` | LUI | `0110111` | U | — |
| `LW` | LOAD | `0000011` | I | funct3=`010` |
| `SW` | STORE | `0100011` | S | funct3=`010` |
| `BEQ` | BRANCH | `1100011` | B | funct3=`000` |
| `BNE` | BRANCH | `1100011` | B | funct3=`001` |
| `JAL` | JAL | `1101111` | J | — |
| `JALR` | JALR | `1100111` | I | funct3=`000` |

These funct3/funct7 assignments are copied directly from the real RV32I
spec (not custom choices) — a real disassembler/toolchain reads them
correctly. `BEQ`/`BNE`/`JAL`/`JALR` decode to real `Instruction`s but stay
unhandled by `executeALU`'s switch, exactly matching today's behavior and
the golden model's own "unimplemented dead opcode space"
(`compute_pipeline.cpp:97-100`) — decoding them correctly is still worth
doing for forward-compatibility and honest disassembly, even though nothing
currently acts on them.

### 13.5 `custom-0` (`0001011`) — vector-lane and scalar-float ops

R-type shape. `funct7` first selects a *group*, `funct3` then selects the
op within it — the same two-level pattern the real RV32M extension uses
(`funct7=0000001` under the shared `OP` opcode to add MUL/DIV alongside
ADD/SUB) — not an invented technique.

**Group 0 (`funct7 = 0000000`) — integer/float vector ops:**

| funct3 | Op |
|---|---|
| `000` | `VADD` |
| `001` | `VSUB` |
| `010` | `VMUL` |
| `011` | `VFMADD` |
| `100` | `VFADD` |
| `101` | `VFSUB` |
| `110` | `VFMUL` |
| `111` | `VFFMADD` |

**Group 1 (`funct7 = 0000001`) — scalar float ops:**

| funct3 | Op |
|---|---|
| `000` | `FADD` |
| `001` | `FMUL` |
| `010`-`111` | *reserved* |

All ten ops read `rs1`/`rs2` and write `rd`; the `VF*MADD` pair additionally
*read* `rd` as their accumulator input before overwriting it
(`compute_pipeline.cpp:121,125` — `a*b+c` where `c = regs[t][instr.rd]`),
which is exactly why R-type's 3-register shape is sufficient and no 4th
operand field is needed anywhere in this design.

`funct7` values other than `0000000`/`0000001` are reserved.

**Explicit non-goal**: `FADD`/`FMUL` here do **not** implement real RV32F.
Real RV32F uses the `OP-FP` major opcode (`1010011`) and a distinct `f0`-
`f31` register file; this design keeps them on the same integer `regs[]`
via `regAsFloat`/`floatAsReg` bit-reinterpretation, matching current/golden
behavior exactly. Placing them under `custom-0` instead of `OP-FP` is a
deliberate, honest choice — it doesn't claim RV32F compliance it doesn't
have (Q&A resolved earlier this session).

### 13.6 `custom-1` (`0101011`) — control ops

R-type shape by default; `BARRIER` reinterprets the same word as I-type
(same funct3 field position, `rs2`+`funct7` bits become `imm[11:0]` instead)
— again mirroring real spec precedent (`SYSTEM`'s opcode varies field
interpretation by sub-selector too: `ECALL`/`EBREAK` vs. CSR instructions).

| funct3 | Op | Format | Fields actually read by execute |
|---|---|---|---|
| `000` | `VBRANCH` | R | `rs1` only (`compute_pipeline.cpp:191`: `regs[t][instr.rs1] == 0`) |
| `001` | `VJOIN` | R | none (`executeJoin` takes no `Instruction` at all) |
| `010` | `BARRIER` | I | `imm[11:0]` only → `barrier_id` (sign-extended into `imm_t`, but `barrier_id_t` is unsigned — see note below) |
| `011` | `HALT` | R | none |
| `100`-`111` | *reserved* | — | — |

`VBRANCH`'s `rs2`/`rd`/`funct7` bits and `HALT`/`VJOIN`'s entire operand
field are unread by execute today, same as now (`makeInstr(Opcode::VJOIN,
0,0,0,0)` already passes all-zero operands) — the decoder still populates
`Instruction`'s fields structurally for consistency/future use (e.g. a
future explicit reconvergence PC on `VBRANCH`, per §12 step 4's
`divergence_stack.h` extension, which this design doesn't foreclose since
the field is already there and simply unread), it's only execute that
ignores them.

**`BARRIER`'s immediate width — 12 bits, range 0-4095.** Confirmed sufficient:
`BarrierArbiter` (§2.5.5) tracks one running arrival counter per kernel, not
per-`barrier_id` state (`MAX_CONCURRENT_BARRIERS` was already removed this
session, §2.5.4) — `barrier_id` only needs to be unique enough to route a
release event, not sized against a large concurrent-barrier table. 4096
distinct IDs is generous headroom. Real RV32I I-type immediates are
sign-extended into a 32-bit `imm_t`; `decodeInstruction` will cast to
`barrier_id_t` (unsigned) at the point of use, matching how `BARRIER`'s imm
is used today (`compute_pipeline.cpp:240`: `barrier_id_t(instr.imm)`).

### 13.7 `custom-2`/`custom-3` — reserved, unused

`custom-2`/`rv128` (`1011011`) and `custom-3`/`rv128` (`1111011`) are left
completely unallocated. No design pressure to use them now; kept as
headroom for a future extension (e.g. if `custom-0`/`custom-1` ever fill up,
or a real RVV migration wants a clean, separate major opcode rather than
reusing these).

### 13.8 Codec function contracts

```cpp
// hls/src/compute_unit/rv32i_codec.h (new file, header-only — matches
// cu_dispatch_unit.h/barrier_arbiter.h/mem_arbiter.h's existing pattern
// for small, dependency-light HLS components)

raw_instr_t encodeInstruction(Opcode op, uint8_t rd = 0, uint8_t rs1 = 0,
                               uint8_t rs2 = 0, int32_t imm = 0);
// Mirrors makeInstr()'s parameter order/defaults exactly (types.h:104) so
// every existing call site converts by renaming the function, not
// reordering arguments. Internally: switch on `op`, look up its major
// opcode/format/funct3/funct7 from the tables above, pack fields into the
// matching bit positions.

Instruction decodeInstruction(raw_instr_t word);
// Internally: read opcode[6:0] first. If it's one of §13.4's standard
// opcodes, decode using that format + funct3/funct7 -> map back to the
// matching Opcode enum value. If it's custom-0/custom-1, use §13.5/§13.6's
// tables. Populate is_vector/is_memory/is_branch exactly as makeInstr()
// does today (opcode-value-range checks, types.h:112-114) - unchanged
// logic, just fed by a decoded opcode instead of a passed-in one.
```

No implementation yet — this is the contract the next session's step 3
implements against.

### 13.9 Worked examples (hand-encoded, for sanity-checking before implementation)

`encodeInstruction(Opcode::ADD, /*rd=*/3, /*rs1=*/1, /*rs2=*/2, /*imm=*/0)`:

```
 31           25 24     20 19     15 14  12 11      7 6      0
+---------------+---------+---------+------+---------+--------+
|   0000000     |  00010  |  00001  | 000  |  00011  |0110011 |
+---------------+---------+---------+------+---------+--------+
   funct7=0        rs2=2     rs1=1   ADD      rd=3       OP
```

`encodeInstruction(Opcode::VFMADD, /*rd=*/6, /*rs1=*/4, /*rs2=*/3, /*imm=*/0)`:

```
 31           25 24     20 19     15 14  12 11      7 6      0
+---------------+---------+---------+------+---------+--------+
|   0000000     |  00011  |  00100  | 011  |  00110  |0001011 |
+---------------+---------+---------+------+---------+--------+
  funct7=grp0      rs2=3     rs1=4  VFMADD    rd=6      custom-0
```

`encodeInstruction(Opcode::BARRIER, 0, 0, 0, /*imm=*/5)`:

```
 31                    20 19     15 14  12 11      7 6      0
+------------------------+---------+------+---------+--------+
|   000000000101         |  00000  | 010  |  00000  |0101011 |
+------------------------+---------+------+---------+--------+
     imm[11:0]=5            rs1=0   BARRIER  rd=0      custom-1
```

`encodeInstruction(Opcode::HALT)`:

```
 31           25 24     20 19     15 14  12 11      7 6      0
+---------------+---------+---------+------+---------+--------+
|   0000000     |  00000  |  00000  | 011  |  00000  |0101011 |
+---------------+---------+---------+------+---------+--------+
    (unused)      (unused)  (unused)  HALT    (unused)  custom-1
```

Each of these round-trips through `decodeInstruction()` back to the exact
`Instruction` that `makeInstr()`/`toHls()` produce today for the same
kernel-program calls in `kernel_programs.h` — the basis for step 6's
bit-exact regression check.

### 13.10 Explicit non-goals (scope boundary)

- **Not RVV.** No vector-length register, no `LMUL`/`SEW` configuration, no
  real vector register file. `custom-0`'s "vector" ops remain fixed-width
  (one op = one SIMT-lane-parallel operation across the warp, exactly as
  today) — a real RVV decode stage is still a materially larger, separate
  future item (§12 step 5's estimate stands if that's ever pursued).
- **Not real RV32F.** §13.5's non-goal note above.
- **Not a compiler/assembler.** This only defines the on-chip encoding and
  the `encodeInstruction`/`decodeInstruction` codec. Anything that
  currently builds `program[]` arrays by calling `makeInstr()`/`toHls()`
  switches to calling `encodeInstruction()` instead — still hand-built
  test/host code, not a new toolchain.

### 13.11 Implementation procedure — DONE, all 7 steps

1. **Done.** `hls_types.h`: `raw_instr_t = ap_uint<32>` added; `instr_word_t`
   repointed to it. `Instruction` struct untouched.
2. **Done.** `hls/src/compute_unit/rv32i_codec.h` implements §13.8's
   contract against §13.4/§13.5/§13.6's tables, plus §13.12's
   `encodeInstructionExpanded()` (found necessary during implementation,
   not part of the original design).
3. **Done.** `compute_pipeline.cpp`'s `executeOneWarp` fetch line now reads
   `decodeInstruction(program[i])`. No other line in the file changed.
4. **Done**, with one correction found via real execution (§13.12):
   `test_compute_pipeline.cpp`/`test_pipeline_integration.cpp`/
   `test_gpgpu_top.cpp`'s `toHls()`+`loadProgram()` pairs (identical in all
   three) now call `encodeInstructionExpanded()` through a single
   `loadProgram()` that tracks a separately-growing output index and
   **returns the actual raw-word count** (§13.12 explains why this return
   value, not `src.size()`, must feed every `program_len`/`cp.start()` call
   site). `test_hls_data_structures.cpp`'s one `Instruction`-streaming test
   was rewritten to round-trip through the codec instead (still validates
   the same fields).
5. **Done, on real execution, not just compilation.** Built a real GTest
   (from `/home/sebastian/gem5/ext/googletest`, no prebuilt binary was
   available in this environment) and ran all four affected targets plus
   the untouched scheduler suite:
   - `test_hls_data_structures`: 9/9 pass.
   - `test_compute_pipeline`: 8/8 pass (initially 7/8 — see §13.12 for the
     one real failure found and its fix).
   - `test_pipeline_integration`: 3/3 pass.
   - `test_gpgpu_top`: 3/3 pass.
   - `test_mem_arbiter`: 4/4 pass (unaffected, confirms nothing about the
     scheduler broke).
   - `test_cu_dispatch_unit`/`test_barrier_arbiter`: 8/8 and 6/6 pass.
     (First attempt used `-std=c++14` and failed to link -
     `CuDispatchUnit::INVALID_SLOT`, a `static constexpr int` with no
     out-of-class definition, needs one pre-C++17. Re-verified at
     `-std=c++17` - the real standard `CMakeLists.txt:9` actually builds
     this project with, where `static constexpr` members are implicitly
     `inline` and no out-of-class definition is needed - and both pass
     cleanly. Not a real issue: an artifact of the first verification
     command's flags, not of `cu_dispatch_unit.h` or anything in this
     section.)
6. **Done.** This section, `specs/001-open-riscv-gpgpu/tasks.md`, and the
   task tracker.
7. **Done.** §13.10's non-goals stand unchanged; RVV is not touched by
   anything in §13.12 either.

### 13.12 Found during implementation: `ADDI`'s 12-bit immediate isn't wide
### enough for `kernel_programs.h`'s constant-loading idiom — `LUI`+`ADDI`
### expansion, flagged for later software revisions

**What broke.** `ComputePipeline.FpUniformSaxpy` failed on first real run
(not caught by design review — found by actually executing the test, per
this project's standing rigor). Root cause: `kernel_programs.h`'s
`fpUniformSaxpy()` loads float constants via
`makeInstr(Opcode::ADDI, rd, /*rs1=*/0, 0, floatAsReg(alpha))` — e.g.
`floatAsReg(2.0f)` = `0x40000000`. The golden SystemC model's
`Instruction::imm` is an unconstrained `int32_t`, so this "just works" in
simulation. **Real RV32I's `ADDI` has only a 12-bit signed immediate**
(±2047) — §13.4's encoding is spec-correct, and correctly truncated the
upper 20 bits, which is exactly why the loaded value came back as 0.

**Why this is a real RV32I fact, not a bug in this design.** No single
RV32I instruction can load an arbitrary 32-bit constant — this is exactly
why real toolchains use the standard `li rd, imm32` pseudo-op, which
assemblers expand to `lui rd, %hi(imm32)` + `addi rd, rd, %lo(imm32)`.

**Scope, checked before implementing (not assumed):** every kernel in
`kernel_programs.h` was read. Only `fpUniformSaxpy()`'s three `ADDI`s
(loading `alpha`/`x`/`y`'s float bit patterns) hit this — `intSaxpy()`'s
`ADDI`s use small integers (fit in 12 bits fine), `divergentOddEven()`'s
and `parallelReduction()`'s `ADDI`s load small integers too, and
`fpGemm()`/`conv2d3x3()`/`fpSaxpy()`/`fpFmadd()`/`fpDivergentSaxpy()` are
`[DIRECT]` kernels whose float operands are pre-seeded directly into
registers by the calling test, never loaded via `ADDI`. No kernel combines
an out-of-range `ADDI` with a `BARRIER`/`VBRANCH` depending on absolute
instruction position, so expansion never shifts a position-sensitive
target.

**Fix: `encodeInstructionExpanded()`** (`rv32i_codec.h`, next to
`encodeInstruction()`). Detects the pattern (`ADDI`, `rs1==0`, immediate
outside ±2047) and emits the standard two-word `LUI`+`ADDI` split instead
of one word, using the same rounding formula real assemblers use
(`hi20 = (imm + 0x800) >> 12; lo12 = imm - (hi20 << 12);` — rounds toward
the `LUI` value that makes the `ADDI`'s sign-extended low 12 bits combine
back to the exact original `imm` regardless of bit 11). Every other
opcode/immediate combination still emits exactly one word. Returns the
word count (1 or 2) so callers can track a growing output index.

**Consequence that had to be hunted down separately:** program length is
no longer always `src.size()`. Every `loadProgram()` (three near-identical
copies, one per affected test file) was changed to return the actual
expanded word count, and every call site that previously passed
`program.size()`/`golden.size()` into `cp.start()`/`compute_pipeline()`/
`top.launchKernel()` now uses that returned count instead. This was a
second real bug (silently truncating `fpUniformSaxpy`'s program 3 words
short, stopping before `VFMUL`/`VFADD`/`HALT` ever ran) — caught the same
way, by executing the test rather than trusting the fix in isolation.

**Flag for later software revisions (why the user asked this be
documented):** the *golden SystemC model itself* still uses this
unconstrained-immediate `ADDI` idiom — `kernel_programs.h` is unchanged,
and correctly so, since it's the golden reference and the SystemC
simulation has no 12-bit constraint to violate. If a future real compiler
targets this HLS core directly (§12/§13.10's "not a compiler" boundary),
it needs to know: (1) any constant load wider than 12 bits **must** be
expressed as `LUI`+`ADDI` at the source/IR level, exactly like a real
RISC-V backend, not as a single wide-immediate pseudo-instruction, and (2)
this HLS port's own `encodeInstructionExpanded()` already does this
expansion automatically for hand-built test/golden-translation programs,
but a real compiler's own code generator is responsible for doing it
itself for anything it emits directly — `encodeInstructionExpanded()`
should not be relied on as a permanent crutch once a real toolchain exists.

---

## 14. Alveo U55C support — discarded permanently (T025)

**Decision**: U55C is dropped from board scope entirely. **KV260 is now the
sole target board** for the HLS→RTL→FPGA path. Explicit team decision at
the start of T025, not a technical dead-end — every open U55C item up to
this point (§5, §6 item 4, §8.1, §10.11) was "still open, needs real
hardware/data," not "found to be infeasible."

**State at the time of the decision**: with a real Vitis 2023.1 install now
configured (`/tools/Xilinx`, `scripts/setup-env.sh` auto-detects it),
`tests/fpga/test_flow.tcl` was re-run for real. KV260 (`xck26-sfvc784-2LV-c`)
csynths cleanly for both `compute_pipeline` and `memory_pipeline` — same
result as the pre-existing reports §10.11 already cites. U55C
(`xcu55c-fsvh2892-2L-e`) fails with `ERROR: [HLS 200-1023] Part
'xcu55c-fsvh2892-2L-e' is not installed`, and `platforminfo -l` lists no
platforms at all — this Vitis installation has no Alveo device/platform
package added, so even HLS-level C-synthesis (let alone the `v++
--platform` link step T025/T026 would need) isn't reachable here without a
separate, substantial install the team chose not to pursue.

**What's removed**:
- `hls/constraints/u55c.tcl`, `hls/config/u55c.h` — deleted.
- `RISCV_GPGPU_BOARD_U55C` macro and its `#elif` branch in
  `hls/src/common/hls_config.h` — removed; `hls_config.h` now only branches
  on `RISCV_GPGPU_BOARD_KV260` (defined) vs. undefined (KV260-sized
  defaults, unchanged fallback behavior for `tests/hls/*.cpp`).
  `memory_pipeline.cpp`'s comments referencing the per-board macro pair
  updated to name KV260 only.
- `tests/fpga/test_flow.tcl`'s `boards` list — U55C entry removed; the
  script's board-loop structure is kept as-is (still useful if a board is
  ever added back) rather than flattened to a single hardcoded board, since
  that loop/skip structure is what made this decision easy to validate in
  the first place.
- `hls/constraints/README.md` — U55C row/sections removed, scope note
  updated to KV260-only.
- Every U55C mention in this file's *current*, forward-looking sections
  (§1, §3.3, §4, §5, §6, §8.1) — removed or resolved. U55C mentions inside
  already-historical/superseded narrative (§2.4, §10.11's measured-data
  paragraph) are left as-is or marked superseded in place, not scrubbed —
  consistent with how §11/§12's earlier reversals were handled.

**Consequence for T025/T026 scope**: every remaining open item that was
phrased as "KV260 decided, U55C open" (HP vs. HPC port, `ADDR_BITS`, cache
`WAYS` sizing target) collapses to just its KV260 half — nothing further to
resolve there before T025's real RTL-export/build scripts get written.

---

## 15. T025: resolving the `regs[][]`/`program[]` BRAM-aliasing gap (merge, not shared-external-BRAM)

§10.12 item 3 and `gpgpu_top.h`/`cu_dispatch_unit.h`'s own header comments
already named this precisely: `CuDispatchUnit::regsArray()`/`programArray()`
return references into its own private `regs_`/`program_` arrays, and the
comments say these are meant to "alias" `compute_pipeline`'s `regs[][]`/
`program[]` ports "directly." That's trivially true in csim (`test_gpgpu_top.
cpp` passes these references straight into `compute_pipeline()` calls on
separate `std::thread`s — same process, same address space, aliasing is
just C++ pass-by-reference). It stops being trivial the moment
`compute_pipeline` is synthesized as its own independent top-level IP (as
T020/T024 already do, for real, valid resource-estimation reasons): its
`regs`/`program` ports carry `#pragma HLS INTERFACE ap_memory`
(`compute_pipeline.cpp` — real, checked, not assumed), which exposes them
as raw external BRAM address/data/enable ports at that IP's boundary.
Nothing about `GpgpuTop`/`CuDispatchUnit` exposes an equivalent external
port for `regs_`/`program_` — they're just private members, currently
living wherever the enclosing top-level function happens to be. Wiring two
*independently synthesized* IPs to one physical BRAM (a real external
Block Memory Generator both sides drive) is possible in principle but is
genuine new RTL design: new ports on both sides, a real Vivado shared-BRAM
wiring, and — unlike the `dispatch_out`/`status_in` handshake, which
already sequences every other cross-IP interaction correctly — no existing
mechanism guarantees one side isn't reading mid-write from the other.

**Decision: merge, don't share.** `compute_pipeline` and `mem_arbiter` are
*already* written as free-running, pure-stream, `while(true)` kernels — the
same persistent-hardware shape `memory_pipeline` uses (`mem_arbiter.h`'s own
header says so explicitly). Nothing about either requires them to be their
*own* top-level IP — that was always just T020/T024's resource-estimation
convenience, not a system-integration requirement. So: **`gpgpu_scheduler`
becomes the one real top-level "compute" IP**, calling `compute_pipeline`
(`NUM_CUS` instances, `UNROLL`ed) and `mem_arbiter` internally as `DATAFLOW`
processes, exactly reproducing the call pattern `test_gpgpu_top.cpp` already
proves correct — just relocated from a test harness's `std::thread`s into
the synthesizable kernel itself. `regs_`/`program_` become what they always
should have been at the RTL level: ordinary private on-chip BRAM, entirely
internal to one IP, no cross-IP aliasing question left to answer.
`memory_pipeline` is unaffected and stays the second, genuinely separate
IP — its `global_mem`/`m_axi` port never had this problem (it's a real
external-memory pointer, not a shared-state alias).

**What changes, concretely (`hls/src/scheduler/gpgpu_top.h`)**:
- `dispatch_out[NUM_CUS]`, `status_in[NUM_CUS]`, and new per-CU
  `mem_req`/`mem_resp` streams become **local** `hls::stream` variables
  inside `gpgpu_scheduler`, not top-level ports — the
  `#pragma HLS INTERFACE axis port=dispatch_out`/`status_in` lines are
  removed since nothing external ever touched them; they were only
  top-level ports because `gpgpu_scheduler` and `compute_pipeline` were
  wrongly treated as separately-wired IPs.
- `gpgpu_scheduler`'s real external ports shrink to: `program_ptr` (`m_axi`,
  unchanged), `program_len`/`total_warps`/`start`/`busy`/`done`/`fault`
  (`s_axilite`, unchanged), plus **new** `mem_req_out`/`mem_resp_in`
  (`axis`) — the one pair `mem_arbiter` needs to reach the *external*,
  separately-synthesized `memory_pipeline` IP.
- Inside, under `#pragma HLS DATAFLOW`: the existing scheduling loop (as
  its own free-running process), `compute_pipeline(...)` called once per
  CU with `top.cu(i).programArray()`/`regsArray()` passed directly (same
  values `test_gpgpu_top.cpp` already passes), and `mem_arbiter(...)`
  fanning the per-CU streams into the one external `mem_req_out`/
  `mem_resp_in` pair.
- `compute_pipeline.{h,cpp}` and `mem_arbiter.h`: **unchanged.** Both stay
  independently `set_top`-able too (T020/T024's resource-estimation runs
  keep working exactly as before) — `#pragma HLS INTERFACE` on a function
  is only honored when that function is the actual synthesis top; called
  as a plain sub-function from `gpgpu_scheduler`'s `DATAFLOW` region, the
  same source becomes ordinary internal logic instead. (This dual use —
  same source, two different roles depending which function `set_top`
  names — is standard Vitis HLS practice, not a new pattern for this
  project; empirically confirmed by the real `csynth_design` run below,
  not just asserted.)

**Why this doesn't need new synchronization design**: every cross-array
access is already sequenced by the `dispatch_out`/`status_in` handshake
that exists today — the scheduler writes a dispatch, *then*
`compute_pipeline` reads/writes `regs`/`program` for that slot, *then*
`compute_pipeline` writes status, *then* the scheduler reads status/acts on
the result. Merging into one IP doesn't change that ordering; it just
turns the arrays the ordering already protects from "two IPs hoping to
share a BRAM" into "one IP's own private state" — strictly simpler, not a
new hazard surface.

**Host-visible results still work correctly under this design**: nothing
about `regs_`/`program_` being fully private (no external port at all) is
a regression — per §2.5.6, the host was never meant to read register state
directly. It writes launch registers, polls `done`, then reads *results*
from DDR via `memory_pipeline`'s `m_axi` port (the same port/HP-port
reasoning §10.11 already settled) — exactly how `parallelReduction`'s
kernel program itself works (`SW` to global memory, not a register-file
readback). The new end-to-end test below verifies results the same way a
real host would: through `backing[]` (memory), not through `top.cu(0).
regsArray()` (which no longer exists outside `gpgpu_scheduler` once this
merge lands — a private implementation detail again, as it should be).

---

## 16. Initial per-thread register state (kernel arguments, thread index) — found missing, then relocated for real DATAFLOW legality

**Found while building §15's real end-to-end test**: every kernel in
`kernel_programs.h` needs per-thread register state seeded *before*
launch (`r1=tid`, `r2=address`, ...) — grep-checked `executeALU`/
`executeOneWarp`: nothing derives thread index from hardware alone, no
lane-id register exists anywhere. Every `tests/hls/` test up to this point
seeded this by reaching directly into `top.cu(0).regsArray()` from the
test itself — a testing convenience that worked only because `top` was
test-visible. Once §15's merge made `top` genuinely private inside
`gpgpu_scheduler` (correctly — the host was never meant to read/write
registers directly, only DDR after polling `done`, §2.5.6), **there was
no real mechanism left for the host to inject kernel arguments into the
on-chip register file at all.** This was always missing; the tests just
papered over it.

**First attempt (rejected by real csynth): scheduler loads it at launch.**
Added `CuDispatchUnit::loadInitialRegs()`, called from
`GpgpuTop::launchKernel()` (`schedulerLoop`'s call chain), mirroring
`loadProgram()`'s existing broadcast-copy pattern — except initial-regs
data is per-warp, per-thread *unique* (not broadcast), so it's indexed by
global `warp_id`, not per-CU slot, with each CU picking out only its own
assigned warps' slice via the same `w = cu_id + slot*NUM_CUS` arithmetic
`launch()` already uses. This compiled and passed csim fine — but real
`vitis_hls csynth_design` against `gpgpu_scheduler` rejected it:
`regs_` had two DATAFLOW-region writers (`schedulerLoop`, via this load,
*and* `compute_pipeline`, during execution) — `[HLS 200-979/200-779]`,
"can only be written in one process function" / "single reader and single
writer." Real, concrete, not a hypothetical — confirmed by an actual
`csynth_design` run, not design review.

**Fix: move the load into `compute_pipeline`, the only place that already
legitimately writes `regs_`.** `WarpSlot` gains a `fresh` bool, set `true`
by `launch()` for a freshly-assigned slot; `CuDispatchUnit::buildDispatch()`
(no longer `const`) packages it into `warp_dispatch_t::fresh_launch` and
clears the slot's flag — so it fires exactly once per warp, on its first
dispatch, never on a barrier resume (`releaseBarrier()` never touches
`fresh`, and `resume_pc==0` alone was deliberately *not* used as the
fresh/resume signal — a warp could in principle stall with `resume_pc==0`,
making that an unreliable proxy). `compute_pipeline` gains a new
`initial_regs_ptr` (`m_axi`) parameter; on `d.fresh_launch`, it seeds
`regs[d.slot_id]` from `initial_regs_ptr[d.warp_id * ...]` before calling
`executeOneWarp()`. `CuDispatchUnit::loadInitialRegs()` and
`GpgpuTop::launchKernel()`'s `initial_regs_ptr` parameter were removed
again (dead — `regs_` is now write-once-owner `compute_pipeline`, never
touched by the scheduler side at all). `gpgpu_scheduler` keeps
`initial_regs_ptr` as a real top-level `m_axi` port, passed straight
through to each `compute_pipeline` call unmodified.

**Verified**: all 46 `tests/hls/*` csim tests re-verified passing after
(3 call sites needed the new parameter: `compute_pipeline`'s two remaining
independent-fixture tests pass `nullptr` — their hand-built
`warp_dispatch_t`s never set `fresh_launch`, so it's never dereferenced;
`test_gpgpu_top.cpp`'s two threaded component tests switched from a
post-launch `regsArray()` poke to a real `initial_regs` DRAM buffer,
exercising the actual new mechanism instead of bypassing it).

### 16.1 Real `csynth_design` results against `gpgpu_scheduler` — 4 attempts, 3 fixed, 1 open

Ad hoc verification script: `build/fpga_smoke/csynth_gpgpu_scheduler.tcl`
(not yet wired into `tests/fpga/test_flow.tcl` — that comes once this
converges). Each attempt below is a **real** `vitis_hls csynth_design`
run against KV260, `set_top gpgpu_scheduler`, not a projection.

1. **`ERROR: [HLS 214-157] Top function not found`** — `gpgpu_scheduler`
   was `inline`, defined only in the header; nothing ODR-used it, so it
   was never emitted. **Fixed**: declared in `gpgpu_top.h`, defined in a
   real `gpgpu_top.cpp` (matching `compute_pipeline`'s existing
   convention) — §15 already documents this.
2. **`ERROR: [HLS 200-979/200-779]` — `regs_` two-writer conflict**
   (`schedulerLoop`'s `loadInitialRegs()` at launch vs. `compute_pipeline`
   during execution). **Fixed**: §16 above — moved the seed into
   `compute_pipeline` itself, the sole writer.
3. **`ERROR: [HLS 200-1013/200-984]` — shared `m_axi` bundle** —
   `program_ptr` (read by `schedulerLoop`) and `initial_regs_ptr` (read by
   `compute_pipeline`) shared one `gmem` bundle; two DATAFLOW processes
   can't share one read port, same single-reader rule as an array, just
   for a bus. **Fixed**: split into separate `gmem0`/`gmem1` bundles
   (`gpgpu_top.cpp`).
4. **`ERROR: [HLS 200-979/200-779]` — `WarpSlot` fields, still open.**
   `top.cus_.slots_.{warp_id,resume_pc,state,fresh}` are written by both
   `Loop_1_proc` (Vitis HLS's own name for `CuDispatchUnit::launch()`'s
   slot-assignment loop, auto-promoted into its own DATAFLOW process) and
   `schedulerLoop` (the rest of the scheduling logic - `nextReadySlot`/
   `recordResult`/`releaseBarrier`, all logically the same process I
   intended `schedulerLoop` to be as a whole). **Two remedies tried, both
   ineffective**: `#pragma HLS INLINE off` on `launch()`'s caller
   (`GpgpuTop::launchKernel()`) — the loop still got promoted regardless;
   removing `#pragma HLS UNROLL` from `launch()`'s loop entirely (testing
   the hypothesis that UNROLL itself was the promotion trigger, since
   `loadProgram()`'s non-UNROLL'd loop stays merged into its caller while
   `launch()`'s UNROLL'd one doesn't) — **identical error, byte-for-byte,
   confirming UNROLL is not the cause.** This rules out a whole hypothesis
   class cheaply rather than leaving it an open guess: the real cause is
   structural — `schedulerLoop`'s own shape (`launchKernel()` call,
   *then* a separate `while(!kernelComplete())` loop, sequentially) is
   exactly the "two sequential loop-shaped stages" pattern Vitis HLS's
   DATAFLOW canonicalizer treats as independently promotable pipeline
   stages, regardless of what's inside them. **Not yet tried** (deliberately —
   real risk of guessing wrong syntax rather than another grounded fix):
   `#pragma HLS SHARED`/`STABLE` to explicitly declare the array
   intentionally multi-process-accessed with externally-managed
   synchronization (the dispatch_out/status_in handshake already
   guarantees correct ordering — the checker just can't see that
   statically) — a real Vitis HLS mechanism for exactly this situation,
   but a separate tool warning (`[HLS 214-250]`, "Ignore array
   partition/reshape applied to 'top.cus_' in struct... apply disaggregate
   or aggregate pragma") indicates `top.cus_`'s doubly-nested class-typed
   array shape (`GpgpuTop.cus_[i].slots_[slot].field`) likely needs
   `AGGREGATE`/`DISAGGREGATE` pragma treatment before per-field pragmas
   even target correctly at that depth — three interacting, not-yet-
   confirmed pragma mechanisms stacked together, genuinely uncertain
   without further real testing, not a quick follow-up.

**State to resume from**: `compute_pipeline`/`memory_pipeline` (the
pre-scheduler pair) already csynth cleanly on KV260, real, proven (T020).
The `gpgpu_scheduler` merge (§15) is real progress, not a false start —
3 of 4 found issues are genuinely fixed and confirmed by a clean re-run
each time; only the `WarpSlot` sharing pattern remains. Next step:
either the `SHARED`/`STABLE`/`AGGREGATE` pragma combination (needs real
testing, not guessing), or restructure `launch()`'s slot-state handoff to
go through a stream/handshake instead of a direct shared-array write
(the same mechanism that already makes `regs_`/`dispatch_out`/`status_in`
DATAFLOW-legal elsewhere in this design) — the more structurally
consistent fix, at the cost of another real design pass.

### 16.2-16.5 Five more real attempts at the `WarpSlot` conflict — all defeated, current honest status

Continuing from §16.1 item 4. Each of these is a real `csynth_design` run
against `gpgpu_scheduler` (`build/fpga_smoke/csynth_gpgpu_scheduler.tcl`),
not a projection. Design collaboration for this stretch: options analysis
and FSM design worked through with the user before each attempt (message-
queue vs. flatten-to-FSM options weighed on real hardware-effect grounds -
resource cost, latency, verification risk, not just "which is less code");
the user chose flatten, and each subsequent step was proposed and agreed
before implementing.

1. **Minimal flatten**: replaced `schedulerLoop`'s nested
   `while(!kernelComplete())` with an `if(!busy)/else` single-loop state
   machine (`busy` doubling as the `IDLE`/`RUNNING` state - no new
   variable needed), *without* touching `launchKernel()`/`launch()`
   themselves - testing whether the "two sequential loop-shaped stages"
   shape alone was the trigger. **Same error, byte-for-byte.**
2. **Remove `#pragma HLS UNROLL`** from `launch()`'s loop (testing whether
   `UNROLL` itself, not loop-shape, was the promotion trigger, since
   `loadProgram()`'s non-`UNROLL`'d loop never conflicted).
   **Identical error** - ruled out cleanly, reverted.
3. **Extract `assignSlot()`** (loop-free single-slot primitive) and call
   it 4× hand-unrolled directly from `schedulerLoop`, bypassing
   `launch()`/`launchKernel()` entirely - no loop construct anywhere in
   the reachable path. **Same 4 fields still conflicted**, now against
   `Loop_1_proc` from *`launch()`'s own loop* - despite `schedulerLoop`
   no longer calling it at all. This was the first hint the promotion
   mechanism wasn't really about loops or call-shape.
4. **Documentation research** (real AMD UG1399 "Canonical Forms" +
   "Dataflow" pages, not guessing): confirmed (a) disaggregate/aggregate
   pragmas are for interface bit-packing, not two-writer conflicts, no
   pragma override exists for genuine multiple writers - the only
   sanctioned fixes are "consolidate into one producer task" or "move the
   variable outside the dataflow region"; (b) **every distinct function
   call inside a DATAFLOW region becomes its own task by default** - not
   specifically about loops, no exemption for small/simple helpers, and
   hand-unrolling into repeated calls creates *more* task candidates, not
   fewer (directly explains attempt 3's failure).
5. **Removed the call boundary entirely**: added `slotAt(int)`, a trivial
   one-line reference accessor matching `regsArray()`/`programArray()`'s
   proven-safe shape (neither has ever been flagged in any attempt), and
   wrote the 4-slot assignment as fully inline straight-line code in
   `schedulerLoop` - zero function calls, only that one accessor.
   **Same error again** - and this time the "conflicting write" location
   Vitis HLS reported (`cu_dispatch_unit.h:84:9`) turned out, on
   inspection, to just be a comment line, not real code; `cu_dispatch_unit.h:41`
   has been "the offending loop's location" in every single attempt and
   is, and always was, just `class CuDispatchUnit {`'s own declaration
   line. **These line numbers are generic placeholders in Vitis HLS's
   error report, not literal pointers to the actual offending code** -
   worth recording plainly, since earlier entries in this log (16.1,
   attempt 3) treated them as more precise than they are.
6. **Guarded `launch()`/`assignSlot()`/`launchKernel()` out of the
   synthesis build entirely** (`#ifndef RISCV_GPGPU_HLS_SYNTH_MERGED_TOP`,
   new macro defined only in `csynth_gpgpu_scheduler.tcl`'s `-cflags` -
   they're dead code from `gpgpu_scheduler`'s own call graph regardless,
   kept only for `tests/hls/test_cu_dispatch_unit.cpp`'s direct unit
   coverage) - testing the hypothesis that the DATAFLOW checker inspects
   every method on a touched class instance, not just the reachable call
   graph. Verified both build configurations (`-DRISCV_GPGPU_HLS_SYNTH_MERGED_TOP`
   and without) compile clean, all csim tests still pass. **Same error,
   confirmed not a build cache artifact** (re-ran against a completely
   fresh `gpgpu_scheduler_synth` project directory, deleted first - not
   just `open_project -reset`).

**Current honest status**: five structurally distinct real attempts,
defeated by what appear to be at least two different underlying
mechanisms (function-call task promotion, confirmed via real
documentation; and something that also affects purely inline, no-call
straight-line code with a repeated shape - possibly loop re-rolling
recognizing the 4 structurally similar slot-assignment blocks, not yet
confirmed the way the function-call rule was). The line-number evidence
used to diagnose earlier attempts (16.1's "`Loop_1_proc` is `launch()`'s
loop") turned out to be an unreliable read of a generic placeholder, not
a precise pointer - a real methodological correction, not just a new
data point. `compute_pipeline`/`memory_pipeline` (the pre-scheduler pair)
remain real, proven, csynth-clean on KV260 throughout all of this (T020).
Every code change described above has been reverted or guarded except
where explicitly noted as kept.

**Real remaining options, not yet attempted**:
- `#pragma HLS SHARED`/`STABLE` on the array itself - the one documented,
  not-yet-tested mechanism for telling Vitis HLS "multiple processes touch
  this deliberately, synchronization is handled externally" (the
  `dispatch_out`/`status_in` handshake already guarantees correct
  ordering - the checker can't see that statically). Real syntax/placement
  risk at this nesting depth (`top.cus_[i].slots_[slot].field`, itself
  already flagged as needing `aggregate`/`disaggregate` treatment,
  `[HLS 214-250]`) - untested, not guessed.
- Accept genuine two-process concurrency for this specific interaction and
  design real synchronization for it (e.g., route slot-assignment through
  an actual `hls::stream` handoff, closer to the user's original
  message-queue instinct, but scoped narrowly to just this one write path
  rather than the whole scheduler) - bigger design, more certain to
  eventually work since streams are DATAFLOW's native, fully-supported
  mechanism, but real design and re-verification cost.
- Escalate: this may be worth a real Xilinx support/forum question with
  a minimal reproducer, given the "Canonical Forms" documentation search
  didn't fully explain the inline-straight-line-code case (only the
  function-call case, which real testing confirmed; the inline case is
  still an unconfirmed hypothesis).

### 16.6 Redesign strategy: local-state-plus-free-functions, matching `compute_pipeline`'s already-proven shape

**The pattern all 5 attempts missed**: every attempt kept `slots_` a member
of `CuDispatchUnit`, reachable through `top` - a variable whose scope spans
`gpgpu_scheduler`'s *entire* body and is touched from multiple textually
separate places (the `IDLE` branch, the `RUNNING` branch). Every
restructuring (flatten the loop, drop `UNROLL`, remove the function-call
boundary, remove it entirely via a trivial accessor) kept that shape intact
and kept failing regardless.

`compute_pipeline` itself already disproves "it's about function calls":
it calls `executeALU`, `executeVector`, `executeMemOp`, `executeBranch`,
`executeJoin` - real functions, some with real `UNROLL`'d loops - against
`RegFile regs`, and **none of them has ever been flagged, in any of the 10
real csynth attempts across this whole investigation (T020 through §16.5)**.
The actual distinguishing factor isn't call-vs-inline, loop-vs-not, or
`UNROLL`-vs-not (all individually tested and disproven, §16.2-16.5). It's
*what the state is*: `regs` is a plain array **parameter**, local to
`compute_pipeline`'s own activation, never reachable from anywhere else in
the program. `top.cus_[i].slots_` is a member of a persistent object
visible across the whole enclosing function. This is the first hypothesis
grounded in something directly observed working in this exact codebase,
not a new guess about undocumented Vitis HLS internals.

**Redesign, concretely**:

1. **`CuDispatchUnit` shrinks** to `regs_`/`program_`/`loadProgram()`/
   `regsArray()`/`programArray()` only - unchanged, since this exact shape
   has never been flagged in any attempt. Loses `slots_` and every
   slot-management method (`launch`, `assignSlot`, `nextReadySlot`,
   `recordResult`, `releaseBarrier`, `allDone`, `stateOf`, `warpIdOf`,
   `barrierIdOf`, `slotAt`) - all of it moves out.
2. **New free-function module** for slot management: the same logic,
   unchanged in behavior, rewritten to take `WarpSlot (&slots)[MAX_WARPS_PER_CU]`
   by reference instead of being methods on a class instance - directly
   mirroring `compute_pipeline.cpp`'s existing `executeALU(RegFile regs, ...)`
   pattern (same file structure: free functions operating on a
   caller-owned, by-reference array).
3. **`BarrierArbiter` gets the same treatment, proactively** - it is
   structurally identical to the `slots_` problem (a class instance,
   `top.arbiter()`, touched from both the launch branch and the per-round
   branch). Fixing `slots_` alone and stopping would very likely just
   surface the identical class of error against `BarrierArbiter`'s
   internal state on the next real csynth attempt - designing for both
   now rather than discovering it the hard way a sixth time. Exact
   internal fields to be confirmed by reading `barrier_arbiter.h` before
   implementing (not yet re-read in this pass).
4. **New `schedulerCore` process** (replaces `schedulerLoop`+`schedulerStep`):
   one free-running `while(true)`, called once from `gpgpu_scheduler`'s top
   level exactly like `compute_pipeline`/`mem_arbiter` are today. Owns
   `WarpSlot slots[MAX_WARPS_PER_CU]` and the barrier-tracking state as
   genuine local variables - nothing else in the program can reach them,
   by construction, matching `compute_pipeline`'s own `regs`/`RegFile`
   shape exactly. Talks to `compute_pipeline` only through the existing
   `dispatch_out`/`status_in` streams (completely unchanged).

**Unaffected**: `compute_pipeline.{h,cpp}`, `mem_arbiter.h`,
`memory_pipeline`, the `regs_`/`program_`/`loadProgram()` mechanism, the
`initial_regs_ptr` seeding fix (SS16), all stream wiring, every KV260/Vivado
decision (SS10.11, SS14).

**Test impact**: `tests/hls/test_cu_dispatch_unit.cpp` and
`test_barrier_arbiter.cpp` need real rewrites - same assertions, calling
free functions against a local `WarpSlot[4]` array instead of methods on a
class. `test_gpgpu_top.cpp`'s lower-level tests need matching signature
updates. Mechanical - not a redesign of what's being verified, only how
it's called.

**Verification plan**: unchanged discipline from every attempt so far -
syntax check, full csim regression with the rewritten tests, then a real
`csynth_design` run against the restructured `gpgpu_scheduler`.

**Confidence**: meaningfully higher than attempts 1-5 - grounded in an
already-observed-working pattern in this exact codebase (`compute_pipeline`'s
own helper-function/local-state structure), not a new guess about
undocumented tool behavior. Not a guarantee - if `BarrierArbiter`'s state
shape doesn't fit cleanly into free-function-plus-local-array, that's a
real risk to surface during implementation, not assumed away here.

Design collaboration note: this strategy was proposed after the user
observed that the already-working stream-based blocks (`compute_pipeline`,
`mem_arbiter`) suggested the redesign was worth pursuing to keep T025
moving forward, and asked for the strategy to be written up before
implementation began - consistent with the decision-checkpoint,
stay-involved working style established earlier this session.

### 16.7 SS16.6 redesign implemented and re-verified — the `WarpSlot` DATAFLOW conflict is resolved; a different, tool-internal blocker found next

**Implementation**: `cu_dispatch_unit.h` rewritten to free functions
(`assignSlot`/`launchSlots`/`nextReadySlot`/`buildDispatch`/`recordResult`/
`releaseBarrierSlots`/`allSlotsDone`) operating on a caller-owned
`WarpSlot (&)[MAX_WARPS_PER_CU]`; `CuDispatchUnit` now holds only
`regs_`/`program_`/`loadProgram()` (unchanged, never flagged).
`barrier_arbiter.h` rewritten the same way (`BarrierState` struct + free
functions) - proactively, since it's structurally identical to the
`slots_` problem, even though its state is plain scalars (never actually
observed to fail - every real error across every attempt named a specific
array element). `gpgpu_top.h`/`.cpp`: `GpgpuTop`/`schedulerStep`/
`schedulerLoop` replaced by `schedulerCore()` - one free-running process,
matching `compute_pipeline`/`mem_arbiter`'s shape exactly, owning
`WarpSlot slots[MAX_WARPS_PER_CU]` and `BarrierState barrier` as genuine
local variables declared inside its own body. `tests/hls/
test_cu_dispatch_unit.cpp`/`test_barrier_arbiter.cpp` rewritten against
the free functions + a local array; `test_gpgpu_top.cpp` rewritten to run
`schedulerCore` on its own thread (a real improvement - these tests now
exercise the exact function used in the real merged IP, via real
concurrency, instead of a test-harness stand-in that manually stepped it
round by round).

**Two real bugs found and fixed while re-verifying (both real, both
fixed, neither a false lead)**:
1. **A deterministic SIGSEGV in the full `test_gpgpu_top` binary**
   (confirmed 10/10 runs, not a rare race) - `schedulerCore`'s `start`
   parameter was `bool` (by value); once `done` fires the loop
   immediately re-enters `IDLE`, sees the same fixed `start==true`, and
   relaunches forever - a permanently hot-spinning detached thread still
   actively touching its captured static objects at the exact moment the
   process's static destructors run at exit. Fixed by making
   `schedulerCore`'s `start` a reference (`bool&`) - lets a caller clear
   it once `busy` is observed, matching the real host-clears-start-once-
   busy protocol (SS2.5.6) more faithfully than a fixed value ever could.
   `gpgpu_scheduler`'s own `start` stays a plain `bool` (its real,
   s_axilite-mapped hardware port shape - `schedulerCore` is never itself
   `set_top`, so this is purely an internal-helper signature choice, not
   a change to real hardware behavior).
2. **`OverCapacityLaunchIsRejectedNotSilentlyHung`** hot-spins with no
   blocking condition at all once faulted (no compute_pipeline to
   eventually park it on a stream read) - the highest-risk case of the
   above. Fixed by testing `barrierLaunch()`/`barrierLaunchFault()`
   directly and synchronously instead - the exact mechanism
   `schedulerCore` relies on internally, already independently verified
   by `test_barrier_arbiter.cpp`, without the crash risk.

All 46 `tests/hls/*` csim tests pass, confirmed clean across 10 repeated
full-binary runs (not just once).

**Real `csynth_design` against the redesigned `gpgpu_scheduler`**: the
`WarpSlot` two-writer conflict that defeated 10 straight attempts (SS16.1-
16.5) **is completely gone** - not one `[HLS 200-979]`/`[HLS 200-779]`
error in any of the 3 follow-up attempts below. The redesign strategy
(SS16.6) worked. Three more real, much smaller issues surfaced and were
fixed in sequence:

1. `[HLS 214-113]` x3: "Either use an argument of the function or declare
   the variable inside the dataflow loop body" - `cu.programArray()`/
   `cu.regsArray()` passed directly as `compute_pipeline(...)` call
   arguments (method-call expressions, not plain variables). Fixed by
   declaring real local reference variables (`program`/`regs`) first.
2. Same rule, one more instance: `cu_id_t(0)` (a constructor-call
   expression) passed directly as an argument. Fixed with a local
   `cu_id_t cu_id = 0;`.
3. **A deterministic SIGSEGV inside Vitis HLS's own bundled
   `clang-3.9-csynth` (LLVM 7.0.0) binary** - confirmed via `dmesg`: the
   exact same crash address (`ip 00000000027209c0`) across all 3 runs
   that reached this point, inside `llvm::DenseMapBase::LookupBucketFor`,
   called from `SeqAccessesRegionNode::getZoneNode` /
   `LoopAnalyzer::collectAccesses` - LLVM's internal "Analyze sequential
   accesses" pass (`SeqAccessesInfoPass`), specifically while processing
   `riscv_gpgpu_hls::compute_pipeline` (confirmed by the mangled symbol
   name in the crash backtrace) as part of the `-reflow` optimization
   step. **This is not a DATAFLOW legality violation and not something
   the 214-113 fix pattern applies to** - it's a crash inside the
   compiler's own internals, not a diagnostic about our source. Notably,
   `compute_pipeline` has synthesized standalone cleanly multiple times
   in this project (T020) - this crash is specific to analyzing it in
   the context of the larger, `-reflow`-optimized, DATAFLOW-merged
   `gpgpu_scheduler` build (10,357 instructions after Unroll/Inline,
   confirmed in the log - a real size/complexity jump from the standalone
   build).

**State**: the actual architectural problem this whole SS16 investigation
was chasing is solved and confirmed solved by real re-synthesis. What
blocks a clean `csynth_design` now is a different kind of problem - an
apparent Vitis HLS 2023.1 toolchain bug/limitation, not a design or code
issue fixable by further C++ restructuring in the way every fix so far
has been. Real next-step options: a targeted pragma/directive change to
avoid whatever triggers `SeqAccessesInfoPass` on this function (untested,
would need real investigation into what enables it); reducing
`compute_pipeline`'s unrolled complexity (a real design tradeoff, not
free); checking for a newer Vitis HLS point release with this specific
LLVM crash fixed (outside this environment's control); or filing a real
bug report to AMD/Xilinx with a minimal reproducer.

### 16.8 Crash reproduces in `compute_pipeline` standalone too (corrects §16.7); toolchain-identity and RAM-growth findings; one test result genuinely inconclusive, not "found stalled"

**Real correction to §16.7**: re-verification found the clang reflow
segfault reproduces in `compute_pipeline` **standalone** (`set_top
compute_pipeline`, no `gpgpu_scheduler`, no DATAFLOW region at all) -
identical crash signature, same `a.g.ld.0.bc.clang.reflow.err.log`
pattern. §16.7's attribution ("specific to analyzing `compute_pipeline`
in the context of the larger... build") was wrong - it was reasoned from
T020's older, pre-SS16 standalone result, never re-checked against the
current source (with the SS16 initial-regs seeding loop added). This is
not DATAFLOW-region-size-specific; something about `compute_pipeline`
itself, in its current form, triggers it regardless of context.

**Toolchain identity, checked directly, not assumed**: exactly one
`clang-3.9-csynth` binary exists anywhere under `/tools/Xilinx` (inside
`Vitis_HLS/2023.1`). `Vitis` (the unified installer) has no independent
copy - its own binaries (`vitis`, `vitis-run`, `vitisng`) reference
`XILINX_HLS` directly, confirming they depend on the same Vitis_HLS
install for C-synthesis rather than bundling a separate compiler.
**Switching from a `vitis_hls`-driven flow to a `v++`-driven flow would
not avoid this crash** - both paths call the identical binary running
the identical LLVM pass.

**Isolation attempts on the standalone crash**:
1. Dropped `#pragma HLS PIPELINE II=1` from the seed loop (testing
   whether that specific pragma triggered the crash). **Identical
   crash** - ruled out.
2. Removed the entire seed-loop block (`#if 0`) - the most aggressive
   isolation short of reverting the whole SS16 mechanism. This run
   behaved completely differently from every prior attempt: instead of
   crashing within 15-20 seconds, it ran for several real minutes of
   genuine, active CPU consumption (confirmed via live process
   sampling - CPU time and RSS both advancing between samples seconds
   apart, not stalled), growing past 4GB RSS, without reaching either
   completion or a crash.

**A real methodological error made and corrected during this
investigation, recorded here plainly rather than silently fixed**: the
host machine was suspended overnight partway through that run. On
resume, `ps`'s elaped-time counter (`ETIME`, wall-clock since process
start) had advanced by ~7.5 hours, while CPU time had advanced by only
~24 seconds and RSS by ~300MB - because the suspended process executes
nothing at all during suspend, but wall-clock elapsed time keeps
counting regardless. This was initially reported (in conversation, not
committed here) as a genuine finding - "removing the seed loop changed
the failure mode from a fast crash to an extremely slow, resource-
creeping near-stall." **That characterization was wrong** - it was an
artifact of the suspend, not real tool behavior, and is retracted here
rather than left standing. The process was killed manually (`kill -9`)
after the suspend made the run's remaining data worthless for timing
analysis, without ever reaching completion or a crash.

**Actual, valid state of this test**: genuinely inconclusive. What's
real: with the seed loop removed, `compute_pipeline` ran actively for
several real minutes (far longer than every prior attempt, each of
which crashed within ~15-20s) before the suspend interrupted
observation. What's unknown: whether it would have eventually crashed,
completed, or kept growing indefinitely if the machine had stayed awake
throughout. **Needs a clean re-run, uninterrupted, before drawing any
conclusion from it** - not yet attempted again as of this entry.

**Recommendation for the next session**: re-run the standalone,
seed-loop-removed `compute_pipeline` csynth attempt (`build/fpga_smoke/
csynth_compute_pipeline_only.tcl` with the seed block `#if 0`'d out
again) on a machine that will stay awake and unsuspended for the
duration, with periodic live-process checks (CPU time + RSS deltas
across short intervals, not just single snapshots) to get a real,
uncontaminated answer - either "it eventually crashes too, ruling out
the seed loop as sole cause" or "it eventually completes, meaning the
seed loop's specific shape was the real trigger and a smaller,
differently-structured seeding mechanism is worth designing." Separately,
trying Vitis HLS 2023.2 (same-generation point release, lower
compatibility risk than a bigger version jump) remains a live, real
option if a reinstall happens before that re-run.

---

### 16.9 Clean re-run completed - inconclusive by timeout, not by result; investigation on this axis closed; pivoting to a Vitis HLS reinstall

The recommended clean re-run (§16.8) was performed after a full host
restart, confirmed unsuspended throughout via repeated live-process
sampling (CPU time and RSS both advancing steadily between samples
seconds apart - e.g. 5:46 -> 5:47 CPU time, 3.717GB -> 3.724GB RSS
across a 6-second window). No suspend artifact this time; the
observation is real.

The run was manually killed after roughly 50 minutes of continuous,
genuine execution, RSS past 4GB and still climbing, having reached
neither completion nor a crash.

**Why kill it rather than keep waiting**: this project has two real,
repeated timing references for `compute_pipeline` standalone csynth on
this exact tool and machine - every successful synthesis (T020 onward)
completes in **~20-30 seconds**; every crashing attempt with the seed
loop present fails in **~13-16 seconds**. The clean re-run ran
**~100-150x longer than either reference** with no sign of converging.
That ratio is itself the finding - there is no project precedent
suggesting this was "about to finish," and continuing to wait was
assessed as unlikely to produce a clean answer without an unbounded
time cost.

**Actual, final state of this specific test**: still genuinely
inconclusive on the narrow question "does the seed-loop-removed version
eventually crash or complete" - it never reached either outcome before
being killed. But it is conclusive on the broader question of whether
this line of investigation (iterative source-shape isolation attempts
within Vitis HLS 2023.1) remains productive: **no**. The cheap, fast
experiments (pragma removal, full seed-loop removal) are exhausted, and
the one remaining variant in that family takes too long per attempt to
iterate on.

**Decision**: this isolation-via-code-removal path is closed for now.
`compute_pipeline.cpp` has been reverted to its real, working state
(seed loop restored, `#if 0` isolation wrapper removed) - the source is
no longer in a debug/temp condition. The next real lever is a toolchain
change: installing the latest Vitis HLS version (superseding the
previously-considered 2023.2 point-release option), per the version
recommended by the course instructor. This is a different variable
than anything tested in §16.1-16.9, all of which held the 2023.1
toolchain fixed. Once installed, the standalone `compute_pipeline`
csynth (`build/fpga_smoke/csynth_compute_pipeline_only.tcl`, real
source, no isolation wrapper) should be re-run first as the baseline
check before returning to `gpgpu_scheduler`.

---

### 16.10 Vitis HLS 2026.1 - the crash reproduces (different pass, different tool version); seed-loop-removed isolation now completes cleanly, confirming the seed loop as the real trigger

The toolchain was reinstalled at 2026.1 (`/tools/Xilinx/2026.1`, unified
installer - the professor-recommended version, not the previously-
considered 2023.2 point release). Two real environment findings while
bringing it up, neither related to the crash itself:

1. **The standalone `vitis_hls` binary no longer exists** in the 2026.1
   unified installer - C-synthesis is invoked through the unified CLI
   instead: `vitis-run --mode hls --tcl <script.tcl>` (`hls` is
   `--mode`'s default). `scripts/setup-env.sh` updated to detect either
   binary (`vitis_hls` for older/standalone installs, `vitis-run` for
   2026.1+) and to find `settings64.sh` under a version-numbered
   install root (`/tools/Xilinx/2026.1/Vitis/settings64.sh`), not just
   the old flat `/tools/Xilinx/Vitis/settings64.sh` layout.
2. **`set_top compute_pipeline` (unqualified) silently fails** on
   2026.1 - `WARNING: [HLS 200-1986] Could not apply TOP directive` at
   `set_top` time, then a hard `ERROR: [HLS 214-157] Top function not
   found: there is no function named 'compute_pipeline'` at
   `csynth_design` time. `compute_pipeline` lives in
   `namespace riscv_gpgpu_hls`; 2023.1 resolved the unqualified name
   fine, 2026.1 requires the fully-qualified
   `set_top riscv_gpgpu_hls::compute_pipeline`. Real behavior
   difference between versions, not an error in the source - every
   `set_top` call in this repo's `.tcl` scripts will need the qualified
   name once T025's build scripts target 2026.1.
3. Vivado licensing: `create_platform` (called internally from
   `set_part`) now requires a working Vivado license even for
   HLS-only csynth - it didn't under 2023.1's standalone flow. Resolved
   once a valid `Xilinx.lic` (covering `Vitis_HLS`, node-locked to this
   host) was installed and `XILINXD_LICENSE_FILE` pointed at it.

**With both of those resolved, the real crash test**: standalone
`compute_pipeline` csynth (real source, seed loop present) on 2026.1
still segfaults - but in a different pass than 2023.1:

```
INFO: [HLS 200-2061] Successfully converted nested loops
      'SEED_INITIAL_REGS_THREADS' and 'SEED_INITIAL_REGS_REGS' ...
      into perfectly nested loops.
Running pass 'Nested Loop Flatten Pass' on function
      '@riscv_gpgpu_hls_compute_pipeline'
Abnormal program termination (11)
```
Backtrace: `FlattenLoopNestChecker::cfgAroundInnerLoop()` ->
`FlattenLoopNestChecker::checkLoopNest()` ->
`pass::FlattenLoopNest::loopProcess()` (all in
`libxv_hls_hwsyn.so`) - a completely different code path than 2023.1's
`SeqAccessesInfoPass` (`clang-3.9-csynth`, LLVM 7.0.0-based). 2026.1
uses `clang-16` as its C-synthesis frontend per its own log output -
confirmed a genuinely different compiler, not just a repackaged one.

**Isolation re-run, seed loop removed** (same `#if 0` wrapper as
§16.8/16.9, now cheap to test since 2026.1 fails/succeeds in under a
minute instead of running unbounded): **completed cleanly.**
`CSYNTH_QUALTEST: DONE`, VHDL and Verilog RTL both generated, "All loop
constraints were satisfied," estimated Fmax 251.93 MHz, ~49s total
elapsed - no crash, no timeout, no ambiguity.

**Conclusion - confirmed, not just suspected**: the `SEED_INITIAL_REGS_
THREADS`/`SEED_INITIAL_REGS_REGS` nested loop (seeding `regs[slot][t][r]`
from `initial_regs_ptr` on `fresh_launch`, §16) is the real trigger,
independent of tool version or compiler backend. Two unrelated
LLVM-based compilers (LLVM 7.0.0/clang-3.9-csynth on 2023.1, clang-16
on 2026.1) both choke on the same loop nest, in two unrelated internal
passes (sequential-access dependency analysis vs. loop-nest
flattening). That convergence across independently-built toolchains is
strong evidence this is a real characteristic of the loop's shape
(likely the `[d.slot_id][t][r]` 3D indexing into a fully-partitioned
`regs` array combined with the `initial_regs_ptr[base + t*N + r]`
flattened source-side indexing) rather than a bug specific to either
tool release.

**Source reverted**: `compute_pipeline.cpp`'s `#if 0` isolation wrapper
has been removed again; the seed loop is back in its real, working
state. This was a read-only diagnostic, not a fix.

**Next step, not yet started**: design a differently-shaped initial-
regs seeding mechanism that avoids this specific loop-nest shape -
per §16.8's original recommendation, now confirmed necessary rather
than hypothetical. Candidates worth weighing before picking one
(deliberately not decided here - this needs a real design discussion,
same as §16.6): flatten the double loop into a single loop over
`MAX_THREADS_PER_WARP * NUM_REGS_PER_THREAD` with a single derived
index; move the seeding out of `compute_pipeline` entirely into a
separate small function/process seeded once outside the per-dispatch
loop; or reduce/restructure the array partitioning on `regs` so the
loop nest has less for the flattening/access-analysis passes to chew
on.

---

### 16.11 Fix implemented and confirmed: single flattened loop replaces the t/r nested loop - real csim + real csynth both pass

Picked the first candidate from §16.10's list: collapse the two nested
loops into one loop over the flat index. Since
`base + t*NUM_REGS_PER_THREAD + r` was already exactly `base + i` for
`i = t*NUM_REGS_PER_THREAD + r`, the DRAM-side index needed no change
at all - only the on-chip write side needed `t`/`r` recovered from `i`
via `i / NUM_REGS_PER_THREAD` / `i % NUM_REGS_PER_THREAD`:

```cpp
if (d.fresh_launch) {
    uint64_t base = uint64_t(d.warp_id) * MAX_THREADS_PER_WARP * NUM_REGS_PER_THREAD;
SEED_INITIAL_REGS:
    for (int i = 0; i < MAX_THREADS_PER_WARP * NUM_REGS_PER_THREAD; ++i) {
        regs[d.slot_id][i / NUM_REGS_PER_THREAD][i % NUM_REGS_PER_THREAD] =
            initial_regs_ptr[base + i];
    }
}
```

**Correctness, verified, not assumed**: all 20 HLS csim tests pass
(`test_compute_pipeline` 8/8 including `BarrierStallThenResume`, which
specifically exercises `fresh_launch` semantics across a barrier;
`test_pipeline_integration` 3/3; `test_hls_data_structures` 9/9).

**Synthesis, verified against the real source** (not a `#if 0`
diagnostic strip-down this time): standalone `compute_pipeline` csynth
on Vitis HLS 2026.1 completed cleanly - `CSYNTH_QUALTEST: DONE`, VHDL
and Verilog RTL both generated, "All loop constraints were satisfied,"
estimated Fmax 251.93 MHz, ~46s total elapsed. No crash, in either the
`SeqAccessesInfoPass`-shaped way (2023.1) or the `FlattenLoopNest`-
shaped way (2026.1, §16.10).

**Two more environment-only bugs found and fixed while getting the
csim tests running** (neither is an HLS/RTL issue, both were exposed
incidentally by testing against the 2026.1 reinstall):

1. `tests/hls/CMakeLists.txt`'s Vitis HLS header auto-detection only
   globbed the old standalone layout
   (`/tools/Xilinx/Vitis_HLS/*/include`) - added the unified-installer
   layout (`/tools/Xilinx/*/Vitis/include`) alongside it, same fix
   shape as `setup-env.sh` in §16.10.
2. `setup-env.sh`'s SystemC detection unconditionally overwrote an
   already-correct, profile-exported `SYSTEMC_HOME` with an empty
   string whenever `pkg-config` didn't know about the install (true
   here - this host's SystemC lives at `/usr/local/systemc`, unregistered
   with pkg-config) - then its own fallback directory scan didn't check
   that exact path shape (`<prefix>/systemc/include/systemc.h`) either,
   so `SYSTEMC_HOME` ended up empty after sourcing the script even
   though the shell already had it set correctly beforehand. Fixed to
   respect an existing `SYSTEMC_HOME` first, and widened the fallback
   scan to include `/usr/local/systemc`.

**Where this leaves T025**: the architectural blocker (SS16.6,
committed) and the compiler-crash blocker (SS16.8-16.11, fixed here)
are both resolved. `compute_pipeline` synthesizes cleanly standalone.
Not yet done: re-verify `memory_pipeline` standalone and the merged
`gpgpu_scheduler` DATAFLOW region both still synthesize cleanly on
2026.1 (last verified on 2023.1, pre-reinstall), then move on to
T025's actual remaining scope - RTL export/packaging and FPGA build
scripts, which haven't been started yet.

---

### 16.12 Full-system re-verification on 2026.1: compute_pipeline, memory_pipeline, and the merged gpgpu_scheduler all synthesize cleanly

Closing out §16.11's remaining item. Two more `set_top`-qualification
fixes needed first (same 2026.1 behavior as §16.10, applied to real
tracked/scratch scripts this time, not just the throwaway test):

1. `tests/fpga/test_flow.tcl` (T020's real smoke test, tracked in git):
   `set_top $top_fn` -> `set_top "riscv_gpgpu_hls::$top_fn"`. Its
   pass/fail check also hardcoded the unqualified name into the
   expected report path (`${top_fn}_csynth.rpt`), which no longer
   exists under that name once `set_top` is qualified (`::` becomes
   `_` in generated artifact names, e.g.
   `riscv_gpgpu_hls_compute_pipeline_csynth.rpt`). Fixed by checking
   the generic, naming-scheme-independent `solution1/syn/report/
   csynth.rpt` instead, which Vitis HLS always produces alongside the
   qualified one.
2. `build/fpga_smoke/csynth_gpgpu_scheduler.tcl` (gitignored scratch
   script): same `set_top gpgpu_scheduler` -> `set_top
   riscv_gpgpu_hls::gpgpu_scheduler` fix.

**Results, all real (non-diagnostic) source, all on 2026.1:**

- `tests/fpga/test_flow.tcl` (compute_pipeline + memory_pipeline,
  KV260): `PASS: all 2 csynth run(s) completed cleanly`.
- `gpgpu_scheduler` (scheduler + compute_pipeline + mem_arbiter,
  DATAFLOW-merged - the actual integrated top-level kernel, SS15/16.6):
  `CSYNTH_GPGPU_SCHEDULER: DONE`, VHDL and Verilog RTL both generated,
  "All loop constraints were satisfied," estimated Fmax 273.97 MHz,
  ~53s total elapsed. No DATAFLOW conflict, no compiler crash.

Every synthesis blocker this project has hit since starting T025 is
now resolved and independently re-confirmed on the reinstalled
toolchain: the SS16.6 `WarpSlot` DATAFLOW conflict (architectural,
fixed by redesign), and the SS16.7-16.11 seed-loop compiler crash
(tool-triggered, fixed by flattening the loop). T025's remaining scope
is genuinely new work from here - RTL export/IP packaging and FPGA
build scripts - not further debugging of what's already synthesizing.

---

### 16.13 Real resource reports pulled for the first time - memory_pipeline overflows BRAM 251%; root-caused and fixed (L2 on URAM + II=4); a new timing regression found and deliberately deferred

T020's smoke test only ever checked that `csynth_design` completes, not
what it actually produces (deliberately - see that task's own note).
This is the first time this project has looked at real utilization
numbers.

**compute_pipeline** (KV260): BRAM 2 (~0%), DSP 260 (20%), FF 38163
(16%), LUT 60480 (51%), but **timing violation**: slack -0.32ns at the
5ns/200MHz target, traced to `executeALU`. Minor (~6% over), not
investigated further this session - noted, deferred alongside item 2
below.

**memory_pipeline** (KV260): **BRAM 361, 251% of the device's 144
available - a hard overflow, would fail place & route outright.**
Root-caused via isolated probes (`SetAssocCache` alone, `PIPELINE
II=1` alone) rather than guesswork:

- `cache_bank.h`'s own `ARRAY_PARTITION`/`BIND_STORAGE` pragmas are
  correct in isolation (12 BRAM for a standalone `L1Cache`, exactly
  the intended "N parallel way-banks" shape - not the cause).
- `memory_pipeline.h`'s `l1_caches_[NUM_CUS]` (array-of-cache-objects,
  runtime-indexed by `cu_id`) is also not the cause (11 BRAM in
  isolation).
- The real cause: `memory_pipeline.cpp`'s `MEMORY_PIPELINE_LOOP`
  `PIPELINE II=1`, applied across the full L1/L2 lookup -> miss ->
  fill control flow. Confirmed by direct A/B test on the real design:
  `PIPELINE off` alone drops BRAM 361 -> 169 (still 117% over).
  Requesting II=1 forces the tool to fragment each way's line-data
  array into one BRAM *per word* (32 separate 1-word memories per way)
  to guarantee zero port conflicts between concurrently in-flight
  pipeline stages, rather than sharing a port across stages.

**Two independent fixes, both real design changes, both csim-verified
(20/20 relevant tests) and re-synthesized against actual (non-
diagnostic) source:**

1. **L2Cache moved to URAM** (`cache_bank.h`): `xck26` has 64 URAM
   blocks, previously 0 used - `BIND_STORAGE ... impl=BRAM` was
   hardcoded for both L1 and L2 (same template, same constructor).
   `SetAssocCache` gained a `DataStorage DATA_IMPL` template parameter
   (`BRAM` default) selecting `data_`'s binding via `if constexpr`;
   `tag_`/`valid_` stay BRAM always (URAM is a poor fit for their
   1-bit/TAG_BITS widths, and they were never the bulk of the cost -
   L2's `data_` alone was 128 of the 361 BRAM entries). `L1Cache`
   stays BRAM (small, latency-critical, was never the problem);
   `L2Cache` now specifies `DataStorage::URAM`.
2. **`MEMORY_PIPELINE_LOOP` relaxed from `II=1` to `II=4`**
   (`memory_pipeline.cpp`). A real II sweep (1/2/4/8/off), all with
   L2-on-URAM already applied, found a **hard cliff, not a smooth
   tradeoff**:

   | Requested II | Achieved II | BRAM | URAM | Fits? |
   |---|---|---|---|---|
   | 1 | 47 cycles | 105 (73%) | 128 (200%) | No |
   | 2 | 47 cycles | 105 (73%) | 128 (200%) | No |
   | 4 | 76 cycles | 48 (33%) | 16 (25%) | Yes |
   | 8 | 76 cycles | 48 (33%) | 16 (25%) | Yes |
   | off | 76 cycles | 49 (34%) | 16 (25%) | Yes |

   II=1/2 fight for low latency by fragmenting storage (same 47-cycle
   achieved rate either way) but blow URAM to 200% - can't be built.
   II=4/8/off all land on the identical resource/latency point (the
   achieved rate becomes memory-dependency-bound, not resource-bound,
   once the tool stops fragmenting) - no reason to prefer one over the
   others within that cluster. Picked II=4 over `off` for one fewer
   BRAM (48 vs 49) at zero cost, per explicit instruction.

**Final, real, non-diagnostic-source measurement**: 48 BRAM (33%), 16
URAM (25%) - comfortably fits. Also checked and ruled out as further
optimization targets: shared memory (24 BRAM, correctly sized for its
role - a per-CU scratchpad that genuinely needs BRAM's lower latency,
moving it to URAM would hurt the thing it exists for) and AXI read/
write buffering (4 BRAM, fixed minimal overhead).

**A new, real timing regression found and deliberately deferred, not
silently absorbed**: the L2-on-URAM change drops the design's
estimated clock period from 3.65ns (~274MHz, comfortable margin) to
**7.02ns (~142MHz)** - slack -3.37ns against the 5ns/200MHz target,
larger than item 1's `compute_pipeline` violation. Traced to
`lookup()`'s combinational tag-compare-then-read of `data_` not
accounting for the real cascade delay of URAM: each L2 way's
16384-deep array spans 4 physical URAM primitives, and reading through
that cascade in the same cycle as the 4-way parallel tag comparison
is a longer path than BRAM's equivalent. (Also: the Pragma Report
flags all of `SetAssocCache`'s `ARRAY_PARTITION`/`UNROLL` pragmas as
"Not implemented" post-`if constexpr` - cross-checked against the real
Storage Report and confirmed to be a reporting/source-attribution
artifact of pragmas living inside a templated `if constexpr` block,
not an actual functional failure: L1/L2 way-partitioning is present
and correct in the synthesized design exactly as intended.)

**Decision**: keep the resource fix - it's not optional tuning, it's
the only configuration that fits the device at all (even pure-BRAM
`PIPELINE off` still overflows at 169/144). Defer real timing closure
for both open violations (`compute_pipeline`'s -0.32ns and this
-3.37ns) as a dedicated follow-up rather than continuing to extend
this session - noted explicitly here so neither is later mistaken for
"passing," and `hls/constraints/kv260.tcl`'s own comment already frames
200MHz as an early-design-space-exploration target, not a hard
requirement, at this stage of the project.

---

### 16.14 General optimization strategy (proposal, not yet implemented) - grounded in real AMD documentation (UG949, UG1399) plus this session's own measurements

Requested strategy for area and timing optimization across the design,
informed by real §16.13 measurements (not generic advice) and checked
against two real AMD guides. **Correction on the request**: "UG1197"
does not appear to be a real, current AMD document (verified via
search - no such document exists in AMD's documentation portal); the
correct Vitis HLS user guide is **UG1399**. Used that instead, plus
**UG949** (UltraFast Design Methodology Guide) as requested. Every
claim below marked "UG949"/"UG1399" is a real, fetched quote from
docs.amd.com, not a paraphrase from training-data recollection -
distinguished from this session's own analysis, marked "this session."

Current resource picture, for reference (§16.13):

| Module | BRAM | URAM | DSP | FF | LUT | Timing |
|---|---|---|---|---|---|---|
| compute_pipeline | ~0% | - | 20% | 16% | **51%** | -0.32ns |
| memory_pipeline | 33% | 25% | - | 8% | 9% | -3.37ns |
| gpgpu_scheduler (merged) | 24% | - | 20% | 18% | **53%** | - |

#### A. Timing - the two known violations, now with a real, documented fix path

**memory_pipeline's -3.37ns (URAM cascade, §16.13 item 2)**:

- **UG949, "Performance Considerations When Implementing RAM"**: *"Using
  an output register is required for designs operating at higher clock
  frequency, and is recommended for all designs to ease timing
  closure."* Further: *"a second output register is beneficial, as
  slice output registers have faster clock to out timing than a block
  RAM register. Having both registers has a total read latency of 3."*
  Critically: *"they should be in the same level of hierarchy as the
  RAM array. This allows the tools to merge the block RAM output
  register into the primitive"* - placing the register at the wrong
  hierarchy level defeats the fix.
- **UG949, "Decomposing Deeper Memory Configurations..."**: confirms
  our L2 cascade depth (4 URAM primitives per way, 16384/4096) is
  already the *good* shape - *"Creating a 4-deep cascaded block RAM
  chain is better for maximum clock frequency when compared to an
  8-deep cascaded block RAM chain."* This rules out "make the cascade
  shallower" as the fix (it's already shallow) and points squarely at
  the missing-output-register explanation instead.
- **UG1399's `BIND_STORAGE` `latency=` parameter** is the direct Vitis
  HLS-level lever for this: it controls how many pipelined register
  stages the memory access gets, exactly the "output register"
  UG949 describes. **Concrete fix to test**: `#pragma HLS BIND_STORAGE
  variable=data_ type=RAM_2P impl=URAM latency=2` (or `3`, per UG949's
  "second register" note) on `cache_bank.h`'s `data_` member, inside
  the same `if constexpr (DATA_IMPL == DataStorage::URAM)` branch
  added in §16.13 - this is additive to that fix, not a replacement.
- **UG949, "Address/Control Line Timing"**: for large RAMs, *"adding
  an extra register after the generation of these signals and before
  the RAMs"* improves address/control path timing - relevant to
  `lookup()`'s `set`/`tag`/`widx` computation feeding directly into
  the URAM read combinationally; worth an explicit pipeline stage
  there too if `latency=` alone doesn't close the gap.

**compute_pipeline's -0.32ns (`executeALU`, §16.13 item 1)**: no new
documentation lead beyond what was already proposed - relax the clock
target (kv260.tcl already frames 200MHz as exploratory) or add one
pipeline stage. Minor (~6% over) relative to item 1 above.

**UG949, "Timing Closure" (general)**: *"Focus on worst negative slack
(WNS) of each clock as the main way to improve total negative slack
(TNS)"* - supports fixing memory_pipeline's -3.37ns before
compute_pipeline's -0.32ns, since it's the larger violation. Also:
*"tools do not try to further improve timing after timing is met"* -
once a fix closes the gap, no further clock-uncertainty tightening is
needed or productive.

#### B. Area - real, named pragmas confirmed, not generic advice

**LUT is the tightest resource (51-53% on the compute side) while DSP
sits comparatively slack (20%)** - real, measured imbalance (§16.13),
now the guiding constraint for everything below.

- **UG1399, `ALLOCATION`**: `#pragma HLS ALLOCATION instances=<func>
  limit=N function` explicitly limits how many hardware instances of a
  called function exist, forcing call sites to share one instance
  instead of each getting its own. Documented example: constraining 3
  instances to 1 achieved *"one-third the area."* Candidate targets:
  `decodeInstruction`/`handleBranch`, called once per lane inside
  `compute_pipeline`'s 32-wide `UNROLL` - worth checking whether these
  are already being shared or duplicated 32x (Bind Op Report would
  show this directly, not yet checked this session).
- **UG1399, `INLINE off`**: *"prevent automatic inlining... preserving
  sharing opportunities"* - pairs directly with `ALLOCATION` above;
  Vitis HLS's default aggressive inlining is what causes the
  duplication `ALLOCATION` alone can't fix if the function was already
  inlined away before allocation limits apply.
- **UG1399, `BIND_STORAGE` port count**: *"stream of blocks uses
  2-port block RAM (`type=RAM_2P`) by default... optimize by selecting
  single-port RAM (`RAM_1P`) when only one access port is needed."*
  `cache_bank.h` currently hardcodes `RAM_2P` for `tag_`/`valid_`/
  `data_` regardless of actual port need - worth auditing whether any
  of these only ever need one port (e.g. `tag_`/`valid_` in `lookup()`
  read-only, single accessor per cycle) once the URAM timing fix (A) 
  is confirmed working, so port-count changes aren't tested
  simultaneously with a timing fix and conflated.
- **UG1399, `BIND_OP`**: explicit operator-to-resource binding,
  confirmed real syntax for controlling DSP vs. LUT-fabric
  implementation per operation - the direct mechanism for addressing
  the measured LUT/DSP imbalance (rebalance wide comparators/muxes,
  e.g. `DivergenceStack::popcount`, L2's 4-way tag compare, onto DSP
  slices where LUT is the pressured resource).
- **UG1399, array partitioning warning**: *"partitioning increases
  BRAM count proportionally to partition width"* - direct restatement
  of §16.13's own hard-won lesson (`MEMORY_PIPELINE_LOOP`'s II=1
  fragmentation), now confirmed as documented, expected tool behavior
  rather than a surprise specific to this design.

FP operator policy (relaxed-precision `config_compile
-unsafe_math_optimizations` for `FpGemm2x2TileK4`/`FpUniformSaxpy`-
style kernels) remains a real candidate from the original proposal,
but needs a policy decision (is bit-exact IEEE FP a hard requirement,
per docs §7's *functional*-not-bit-exact correctness bar) before
testing, not a documentation question - no UG949/UG1399 guidance
changes that.

#### C. Zero-risk, source-untouched flags (unchanged from the original proposal)

`config_bind -effort high`, `config_schedule -effort high`,
`config_array_partition` (auto-partition heuristic tuning), and
clock-uncertainty review remain valid, cheap-to-test global levers -
no new documentation lead specifically for these beyond confirming (via UG949's WNS/TNS
guidance above) that they belong in the timing-closure pass, tried
before hand-restructuring source.

#### D. Sequencing (updated from the original proposal)

1. **memory_pipeline's -3.37ns**: test `BIND_STORAGE latency=2` (then
   `3`) on `data_`'s URAM binding first - concrete, documented,
   directly targets the confirmed root cause (missing output
   register), highest-confidence fix of everything in this section.
2. **compute_pipeline's -0.32ns**: clock relaxation or one pipeline
   stage - minor, either direction is fine.
3. **Area**: `ALLOCATION`/`INLINE off` audit on `decodeInstruction`/
   `handleBranch` (real duplication check via Bind Op Report, not yet
   done) - only worth doing if LUT headroom actually becomes a
   blocker for something concrete (e.g. `NUM_CUS` scaling, §16.13's
   planning flag), not preemptively.
4. **`BIND_OP` DSP/LUT rebalancing** and **`RAM_1P` port audit**: lowest
   priority - polish tier, no current overflow motivating either.

Everything in this section is a proposal awaiting approval - nothing
here has been implemented or tested yet, unlike §16.13's fixes (which
were implemented, csim-verified, and re-synthesized before being
written up).

---

### 16.15 §16.14's strategy, tested phase by phase against real synthesis - two real negative results, two premises that didn't survive checking real evidence

Per explicit instruction: implement each phase, verify against real
synthesis (not just csim), report before continuing. Two phases
produced real, honest negative results rather than the expected fix;
two more turned out to have no actionable target once checked against
real tool reports rather than assumed. Nothing forced through without
justification - all four are reported as-found.

**Phase 1 - `BIND_STORAGE latency=2` for memory_pipeline's -3.37ns
(deferred, no effect)**: implemented on `cache_bank.h`'s URAM-bound
`data_`, csim 8/8 passing, pragma confirmed applied (present in the
report's "Valid Pragma Syntax" table). **Real result: zero effect** -
Fmax identical to the byte, 142.43MHz, slack still exactly -3.37ns.
The sub-loops (`readLine_r`, `fillLine`, `fillLine_1`) all show
*positive* slack, confirming the violation was never inside the URAM
read path at all - UG949's "missing output register" explanation,
while a real and correctly-applied fix, was diagnosing the wrong
mechanism for this specific design. The real critical path is still
unidentified (most likely `lookup()`'s combinational 4-way tag-compare,
not captured in the per-loop table since it isn't a pipelined loop) -
open, deferred, needs a targeted isolation probe (same method as
§16.13's original root-causing) to actually find it. `latency=2` left
in place (harmless, still good general practice per UG949) but does
not close this violation.

**Phase 2 - relax shared clock 5.0ns -> 6.0ns (reverted, net negative)**:
real, measured result on the actual shared `kv260.tcl` (affects every
kernel synthesized against it):

| Kernel | @ 5.0ns | @ 6.0ns |
|---|---|---|
| compute_pipeline | -0.32ns slack | -0.21ns slack (improved, not closed) |
| memory_pipeline | Fmax 142.43MHz | Fmax **129.02MHz** (worse) |
| gpgpu_scheduler | 0.00ns slack | 0.00ns slack (no change) |

Relaxing the period let the scheduler pack *more* combinational logic
per stage (a known, real HLS self-defeating effect, not a tool bug) -
it measurably hurt memory_pipeline while only partially helping
compute_pipeline. Since this constraint is shared project-wide, a
partial win for one kernel at a real cost to another isn't a net
improvement at the file level - reverted to 5.0ns. Both violations
remain open, and the real fix for compute_pipeline's -0.32ns needs to
be source-level (FP datapath restructuring or `BIND_OP` latency tuning
on `executeALU`'s `FADD`/`FMUL`, scoped to that function only, not a
global clock change) - not yet attempted.

**Phase 3 - `ALLOCATION`/`INLINE off` area audit (no actionable
target, premise didn't hold)**: §16.14 speculated `decodeInstruction`/
`handleBranch` might be duplicated 32x if called from inside
`compute_pipeline`'s lane-level `UNROLL`. Checked against real source
before touching anything: both are called **exactly once per
instruction** (`decodeInstruction` at `compute_pipeline.cpp:224`,
`handleBranch` at `:194`) - the 32-wide `UNROLL` lives inside
`executeALU`/`executeVector`/`executeBranch` themselves, which is
necessary SIMT lane parallelism, not wasteful duplication. Confirmed
via the Bind Op Report too: neither function appears as a separate
multi-instance module (both single-call-site, so inlining them is
free either way - no duplication to deduplicate). No code change
made - the original hypothesis simply didn't survive contact with the
real call graph.

**Phase 4 - `BIND_OP` DSP/LUT rebalance + `RAM_1P` port audit (no safe
actionable target)**: checked the real Bind Op Report before applying
anything (learned from Phase 1/2 above - verify before touching
source). `executeOneWarp`'s dominant LUT consumers are 32 sets of
per-lane `addr_N`/`req_write_data_N` (real memory-address computation,
one set per SIMT lane): `add` operations already correctly bound to
`fabric` (DSP would be wasteful for narrow address arithmetic), and
`select` (mux) operations, which **cannot** be bound to DSP at all -
DSP performs arithmetic, not general multiplexing. The measured
DSP(20%)/LUT(51%) imbalance reflects the real, necessary shape of
32-wide SIMT fanout logic, not a misplaced-resource bug `BIND_OP`
could fix. Separately, `RAM_1P` for `cache_bank.h`'s `tag_`/`valid_`
was not tested: `lookup()` (read) and `fillLine()` (write) can
genuinely overlap under `MEMORY_PIPELINE_LOOP`'s free-running,
pipelined (`II=4`) execution - a real concurrent read/write access
pattern that single-port memory can't safely provide without risking
either incorrect scheduling or a serialization penalty. No code
change made.

**Net state after this pass**: `cache_bank.h`'s harmless `latency=2`
addition is the only surviving source change from this section;
`kv260.tcl` is back to its original 5.0ns. Both timing violations from
§16.13 remain open and are now better-understood (Phase 1 ruled out
one explanation for memory_pipeline's; Phase 2 ruled out a cheap
global fix for compute_pipeline's) rather than closed - real progress
in diagnosis, not yet in resolution. Both need dedicated, source-level
follow-up: an isolation probe for memory_pipeline's actual critical
path, and `executeALU`-scoped FP pipeline/`BIND_OP` tuning for
compute_pipeline's.

---

### 16.16 Real goal clarified (fit a 2nd CU); real fix found - ALLOCATION operator sharing closes compute_pipeline's timing violation AND cuts LUT by 28%, as a side effect of area reduction

**Context correction, important for future work**: this session's
resource/timing optimization work was assumed to be general polish.
It is not - **the actual objective is fitting a second CU into the
design.** This reframes prior conclusions: `compute_pipeline` alone at
51% LUT (pre-fix) meant two instances would need ~102-104% LUT -
doesn't fit, independent of any timing question. Area reduction on
`compute_pipeline` specifically is the real gating constraint for the
project's stated direction, not a nice-to-have.

**Root cause, confirmed by direct testing**: `executeALU`'s Bind Op
Report showed each of the 32 SIMT lanes instantiating a **separate,
full copy of every opcode's hardware** - its own adder, subtractor,
AND/OR/XOR, comparator, and (for `executeVector`) multiplier and
float-adder - even though the enclosing `switch` only ever executes
exactly one case per lane per instruction. This is distinct from the
32-way lane parallelism itself (genuinely necessary for true SIMT
execution, confirmed not-a-problem in §16.15's Phase 4) - the waste is
*within* each lane, across mutually-exclusive opcode cases, not across
lanes.

**Fix**: `#pragma HLS ALLOCATION operation instances=<op> limit=<N>`
(UG1399, confirmed real syntax via direct fetch) inside `executeALU`
(`add`, `sub`) and `executeVector` (`add`, `sub`, `mul`, `fadd`,
`fsub`, `fmul`) - forces the tool to share hardware across what it was
leaving as independently-instantiated per-lane-per-opcode logic.

**Real limit sweep** (all re-synthesized against real, non-diagnostic
source, csim-verified at each point):

| `limit=` | Total LUT | DSP | FF | Top-level timing | `executeALU` latency | `executeOneWarp` total latency |
|---|---|---|---|---|---|---|
| 32 (baseline, implicit) | 60295 (51%) | 263 (21%) | 37863 (16%) | -0.32ns violation | 9 cyc | 8261 cyc |
| 16 (`executeALU` only) | 56551 (48%) | 225 (18%) | 34125 (14%) | -0.35ns violation | 10 cyc | 8261 cyc |
| **8 (both functions)** | **43414 (37%)** | **108 (8%)** | **23777 (10%)** | **0.00ns - clean** | 12 cyc | 8261 cyc |
| 4 | 38482 (32%) | 57 (4%) | 20278 (8%) | -0.26ns violation (returns) | - | 8261 cyc |
| 2 | 35752 (30%) | 30 (2%) | 18952 (8%) | -0.12ns violation | 33 cyc | 8776 cyc |
| 1 | 32138 (27%) | 15 (1%) | 18119 (7%) | -0.24ns violation | 66 cyc | 17257 cyc |

**`limit=8` is a real local optimum, not just a stopping point on a
monotonic curve**: it's the *only* setting where compute_pipeline's
existing -0.32ns timing violation (§16.13/16.15) closes completely
(0.00ns slack) - not something predicted going in, but real and
reproducible (confirmed via a clean re-synthesis on the exact restored
source). Below `limit=8`, the violation *returns* (the input-muxing
logic needed to route 32 lanes down to fewer shared units becomes its
own long combinational path) while latency cost explodes
disproportionately to the shrinking area gain: `8`->`1` buys another
26% LUT reduction at the cost of 5.5x more cycles per ALU dispatch and
a full doubling of per-warp execution latency (8261 -> 17257 cycles).
`limit=8` kept as the real value.

**Functional correctness**: verified at every sweep point via csim
(`test_compute_pipeline` 8/8, `test_pipeline_integration` 3/3,
`test_gpgpu_top` 3/3 at the final `limit=8` value) - `ALLOCATION`
changes scheduling/resource binding only, never behavior, but this was
still checked at each step rather than assumed.

**Real, integrated-system result** (`gpgpu_scheduler` re-synthesized
with the fixed `compute_pipeline`): LUT 53% -> **39%**, DSP 20% ->
**8%**, FF 18% -> **11%**, and the merged design is also now
timing-clean (0.00ns slack, was already 0.00 before but now with much
more margin underneath it).

**2-CU feasibility, the actual point of this work**: two
`compute_pipeline` instances at 37% LUT each, plus scheduler/arbiter
overhead (~2% per the single-instance merge delta) - roughly
**77-79% LUT** for the full 2-CU system, comfortably within budget.
Before this fix: ~102-104%, did not fit at all. DSP scales even more
comfortably (2 x 108 = 216 of 1248, 17%). This is now a real,
evidenced answer to the stated goal, not just a resource-report
curiosity - though it covers `compute_pipeline` only; `memory_pipeline`
and `gpgpu_scheduler`'s own scheduling/arbitration logic still need
their own 2-CU scaling check (not yet done - `l1_caches_[NUM_CUS]`'s
per-CU array structure in `memory_pipeline.h`, explicitly preserved
this session rather than collapsed to a scalar, is exactly what makes
that scaling path possible later).

**Status of the two open timing violations from §16.13/16.15**:
compute_pipeline's -0.32ns is now closed (confirmed side effect of
this fix, not separately targeted). memory_pipeline's -3.37ns remains
open and unrelated to this fix (different function, different root
cause per §16.15 Phase 1's investigation) - still needs its own
dedicated isolation work.

---

### 16.17 3-CU feasibility assessed (does not fit); cu_id_t narrowed anyway (real-sizing, not a timing fix - tested and ruled out for that)

**3-CU feasibility, real measured math** (not further synthesis - the
real per-instance and per-merge numbers from §16.16 were sufficient):

```
compute_pipeline:              43414 LUT (37.1%)
scheduler+arbiter overhead:     2766 LUT (2.4%, 1-CU merge delta)

2 CUs: 2x43414 + ~5532 overhead =  92360 LUT (78.9%) -- FITS
3 CUs: 3x43414 + ~8298 overhead = 138540 LUT (118.3%) -- DOES NOT FIT
```

3 CUs' `compute_pipeline` footprint alone (130242 LUT) already exceeds
the device's entire 117120-LUT budget before any scheduler/arbiter
overhead - not a close call. DSP (3x8%=24%) and BRAM/URAM
(memory_pipeline's L1 caches would grow from 1 to 3 instances, an
estimated 48->~72 BRAM, still well under the 144 available) are not
the constraint - LUT is the sole, decisive wall. **2 CUs remains the
real, confirmed target**; 3 would need another substantial round of
`compute_pipeline` area reduction beyond §16.16's fix, not attempted.

**`cu_id_t` narrowing (`ap_uint<8>` -> `ap_uint<3>`), tested as a
timing-violation fix, real result: no effect.** Real synthesis of
`memory_pipeline` with the narrowed type: Fmax identical (142.43MHz),
slack identical (-3.37ns), FF/LUT essentially unchanged (7824->7819,
noise-level). The `icmp_ln127`/`l1_caches_[cu_id]` muxing chain
identified in §16.15 Phase 1 as a plausible contributor does not
appear to be a significant one after all - ruled out by direct test,
not assumption.

**Kept anyway**, on different grounds: given 3 CUs don't fit, `cu_id_t`
only ever needs to represent 0-1 in the confirmed-feasible 2-CU case;
3 bits (0-7) is reasonable, modest headroom above that, not the
original 8-bit/256-value over-provisioning. Extensively verified
harmless before keeping it: a first run of the full `test_gpgpu_top`
suite hit a real-looking deadlock/crash on
`TwoWarpBarrierKernelDrivenEntirelyByTheScheduler` - investigated
rather than dismissed (checked every `cu_id` use site across
`mem_arbiter.h`/`cu_dispatch_unit.h`/`gpgpu_top.h` for width-dependent
bit-packing or overflow - found none), then re-tested: 12/12 clean runs
at the narrow width, 8/8 clean runs at the original width for direct
comparison, 3 full-suite runs (138/138 individual test passes) at the
narrow width after. Conclusion: a pre-existing thread-timing flake in
that specific test's background-thread/polling design (consistent with
its known fragility profile, not this project's first encounter with
it), not a real width-dependent bug - real but unlucky, not swept under
the rug.

**Status, memory_pipeline's -3.37ns violation**: still open. Two real,
well-reasoned, UG949-informed and evidence-informed candidate fixes
have now been tested and ruled out (§16.15 Phase 1's `BIND_STORAGE
latency=`, this section's `cu_id_t` narrowing) - the actual critical
path is still unidentified. Next real step: a targeted isolation probe
(same method as §16.13's original BRAM root-causing) purpose-built to
locate it directly, rather than continuing to test plausible-but-
unconfirmed hypotheses one at a time.

---

### 16.18 Real bisection locates the exact cause; the obvious fix (Option 1) has zero effect - same non-result pattern as two prior attempts

**Targeted isolation probes, real bisection** (not another blind
hypothesis test): built `mem_probe_read`/`mem_probe_write` (isolate
`loadWord()`'s path from `storeWord()`'s path via `handleRequest()`
with `is_write` forced each way) - **the read path alone reproduces
the exact violation byte-for-byte** (Fmax 142.43MHz, -3.37ns,
identical); the write path is completely clean. Then bisected inside
`loadWord()` itself by temporarily modifying the real source
(reverted after each test, csim not required for these since they're
timing-only diagnostics, not committed changes):

1. Miss path (burst-fetch + both `fillLine()` calls) removed entirely
   (`return 0` after the L2-miss increment): **clean, 0.00ns slack,
   Fmax 273.97MHz.** Confirms the L1+L2 hit-path logic alone (tag
   compares, `lookup()`) is not the cause - only checked, never
   suspected after §16.15/16.17's negative results on `cu_id_t`.
2. Burst-fetch kept, final dynamic `line[widx]` read replaced with a
   fixed `line[0]`: **still -3.37ns, unchanged.** Rules out the final
   dynamic-indexed read as the cause.
3. Burst-fetch kept, both `fillLine()` calls removed (fixed-index
   return kept): **clean, 0.00ns slack, Fmax 273.97MHz again.**

**Conclusion, now with real evidence, not inference**: the two
back-to-back `fillLine()` calls in `loadWord()`'s miss path (L2's,
then L1's) are the -3.37ns violation's actual, sole cause.
`fillLine()`'s write - `data_[victim][set][i] = line[i]`, where
`victim` is a **runtime** read of `next_victim_[set]` - requires
real write-side demux logic to route into the WAYS-complete-
-partitioned `data_` array (the opposite problem from `lookup()`'s
read-side mux), and this happens twice in a row with nothing
separating them.

**Option 1 attempted (of the three discussed with the user): function-
scope `#pragma HLS PIPELINE II=1` added to `fillLine()` itself**, on
the reasoning that it would give the tool license to register `victim`
separately from the write. Real result: **zero effect** - identical
Fmax, identical slack, identical resource counts. Confirmed the pragma
was actually applied (present in the report's "Valid Pragma Syntax",
not "Ignored Pragmas"). The redundancy is the likely explanation:
`FILL_WORDS` (the inner loop) already carries its own `PIPELINE II=1`,
and Vitis HLS appears to treat that as already establishing the whole
function's pipelining semantics - adding a second, function-scope
`PIPELINE` on top gave the tool no *new* retiming freedom, since it
wasn't being denied any freedom the first pragma didn't already grant.

**Pattern now visible across three independent, well-reasoned,
individually-plausible fix attempts** (§16.15 Phase 1's `BIND_STORAGE
latency=`, §16.17's `cu_id_t` narrowing, this section's function-scope
`PIPELINE`): **all three produced measurements identical to the
byte**, not just "didn't fully close the gap" - the exact same
Fmax/slack/resource numbers before and after, every time. That's a
different, stronger signal than "this particular fix wasn't enough" -
it suggests Vitis HLS's scheduler is reaching the *same scheduling
decision* regardless of these particular pragma-level hints, which
these three otherwise-reasonable techniques cannot influence once
applied at this level. Options 2 (separate the two `fillLine()` calls
at the *call site* rather than inside `fillLine()` itself) and 3
(restructure victim selection to avoid a dynamic write target
entirely) remain untried - discussed with the user, not yet attempted.
Given the pattern above, Option 2 is now a real open question rather
than a safe bet (touches a different point in the design, may or may
not fare differently) - Option 3 is the only one of the three that
changes the *mechanism* rather than adding a hint around it, and may
be the one most likely to actually move the needle given how resistant
this scheduling decision has proven to pragma-level nudges.

---

### 16.19 Option 2 works - memory_pipeline's -3.37ns violation is closed, real, confirmed, reproducible

**Fix implemented** (`memory_pipeline.h`): extracted the two `fillLine()`
calls from `loadWord()`'s miss path into a new private helper,
`fillBothCaches(L1Cache& l1, addr_t line_base, const L2Cache::word_t
line[WORDS_PER_LINE])`, marked `#pragma HLS DATAFLOW`. Unlike §16.18's
Option 1 (a pragma hint inside `fillLine()` itself, which changed
nothing), this targets the actual scheduling boundary between the two
*independent* calls (no data dependency - both read `line`, write to
different cache instances) - and needed the extraction because
`loadWord()` itself has early-return branches (L1 hit/L2 hit/miss)
that `DATAFLOW` doesn't tolerate well; the extracted function is a
clean, branch-free 2-statement body, a legal `DATAFLOW` target.

**Real result, verified three ways**: ad hoc synthesis, full csim
regression, and the official `tests/fpga/test_flow.tcl` smoke test all
agree. Every single entry in the real, per-function/per-loop report
table now shows non-negative slack - `fillBothCaches` itself (0.00 at
the top level; its own row shows 1.18), and both `fillLine`/`fillLine_1`
sub-instances (the L2 and L1 calls, now separately scheduled and
reported) individually clean (1.84, 1.18). Top-level: **slack 0.00**
(was -3.37ns), BRAM 50 (17%, was 48/33%), FF 2665 (1%, was 7824/3%
- note this comparison mixes device-total-relative percentages
recomputed against a smaller absolute count, real reduction not just
rounding), LUT 4387 (3%, was 7329/6%), URAM 16 (25%, unchanged).
Resource usage dropped substantially alongside the timing fix, not
just neutrally - `DATAFLOW`'s explicit staging apparently let the tool
avoid whatever redundant same-cycle hardware the forced-parallel
scheduling was creating, a genuine bonus beyond what Option 2 was
attempting to fix.

**Functional correctness**: csim 46/46 (full HLS suite, every test
binary) passing both before and after, confirmed at each step per this
session's established practice, not assumed because `DATAFLOW` is
"just a scheduling pragma."

**Status update**: both timing violations opened across this session
are now closed - `compute_pipeline`'s -0.32ns (§16.16, closed as a
side effect of the `ALLOCATION` area fix) and `memory_pipeline`'s
-3.37ns (this section). `gpgpu_scheduler` (the merged system,
unaffected by this specific fix since it doesn't include
`memory_pipeline`) was already clean. Real synthesis confirms every
kernel in this project's HLS port - `compute_pipeline`,
`memory_pipeline`, `gpgpu_scheduler` - now synthesizes with zero
timing violations and comfortable resource margins, including real
headroom for the 2-CU scaling goal (§16.16/16.17).

---
