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
4. **`m_axi` binding specifics: KV260 decided — HP** (reversed from an
   earlier HPC lean; §10.11 has the reasoning). U55C's pseudo-channel
   count/interleaving is still open, tracked for T025/T026.
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

**U55C: still genuinely open**, not a number dressed up as one. Device
support isn't installed in this environment (§10 intro), so there's no
measured baseline to project from the way KV260's is above. Its fabric is
dramatically larger (§5: ~14× KV260's BRAM alone), so more CUs are
plausible there, but stating a figure now would be a guess. Left open
until real U55C synthesis exists.

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
