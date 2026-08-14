# Implementation Plan: Bluetooth BR/EDR & LE Link Simulation with WLAN Interference

**Branch**: `001-bluetooth-link-simulation` | **Date**: 2026-08-07 | **Spec**: [spec.md](./spec.md)

**Input**: Feature specification from `/specs/001-bluetooth-link-simulation/spec.md`

## Summary

Build a MATLAB Bluetooth Toolbox simulation covering Team 5's full scope from the
source assignment (§5.5): generate standard-conformant BR/EDR and LE waveforms
(User Story 1), model the mandatory official 2.4 GHz WLAN-coexistence channel
scenario (User Story 2), and measure error-rate performance with and without
adaptive frequency hopping (AFH) mitigation (User Story 3) — producing the figures
and metrics needed for the required 15-20 minute video (User Story 4). The technical
approach starts from MathWorks' official `bluetoothNode`/`wirelessNetworkSimulator`
coexistence example (the assignment's mandatory reference) for the channel/AFH work,
and supplements it with the toolbox's standalone waveform/receiver functions for
BR/EDR and LE conformance checks, since the mandatory example only covers BR/EDR at
the network level.

## Technical Context

**Language/Version**: MATLAB R2026a (confirmed installed locally, Update 2 /
26.1.0.3251617)

**Primary Dependencies**: Bluetooth Toolbox (`bluetoothWaveformGenerator`,
`bluetoothIdealReceiver`, `bleWaveformGenerator`, `bleIdealReceiver`, `bluetoothNode`,
`bluetoothConnectionConfig`, `wirelessNetworkSimulator`, `helperInterferingWLANNode`,
`helperBluetoothChannelClassification`, `helperVisualizeCoexistence`); WLAN Toolbox
(interferer waveform generation, used internally by `helperInterferingWLANNode`);
Communications Toolbox (shared dependency of both)

**Storage**: N/A — no database; artifacts are in-memory MATLAB variables persisted to
`.mat`/`.csv` metric logs and `.png`/`.fig` figures for the video's evidence segment

**Testing**: `matlab.unittest.TestCase`-based automated tests; `checkcode` (MATLAB's
static analyzer) as the linting gate

**Target Platform**: Desktop MATLAB/Simulink (developed on Linux; toolboxes are
pure MATLAB/M-code, no OS-specific dependency)

**Project Type**: Single project — a small MATLAB function library plus driver
scripts and tests (adapted from the template's "Option 1" layout)

**Performance Goals**: Not throughput-oriented. Each scenario run must complete in
interactive time on a laptop (the mandatory official example defaults to 1.5 s of
simulated time with 250 ms classification intervals); customized runs should stay in
the same order of magnitude so the team can iterate without long waits.

**Constraints**:
- FR-003/FR-010: waveform generation must start from and use toolbox
  functions/objects, not hand-rolled modulation — confirmed feasible via
  `bluetoothWaveformGenerator` (BR/EDR) and `bleWaveformGenerator` (LE).
- FR-004: exactly one channel/interference scenario — the mandatory official
  coexistence example — customized, not swept.
- FR-005: every channel parameter must trace to the official example's own
  documented values or a secondary cited source.
- The mandatory official example (§11.4 of the source PDF) is BR/EDR- and
  network-level-only; it does not cover LE and does not expose raw per-bit BER —
  see `research.md` Decisions 2 and 3 for how FR-002/FR-006/FR-007 are still met.

**Scale/Scope**: One BR/EDR scenario, one LE scenario, one channel/interference
scenario run twice (AFH disabled vs. enabled) — five bounded simulation runs total,
no parameter sweep.

## Constitution Check

*GATE: Must pass before Phase 0 research. Re-check after Phase 1 design.*

| Principle | Status | How this plan satisfies it |
|---|---|---|
| I. Code Quality | PASS | All `.m` files run through `checkcode` before being considered done; consistent MATLAB naming (camelCase functions, one responsibility per file in `src/tx`, `src/channel`, `src/rx`, `src/common`); no scratch/exploration scripts left in the final tree. |
| II. Test Coverage | PASS | `matlab.unittest` tests cover: waveform-structure conformance (User Story 1), the Tx→Channel→Rx contract (`tests/contract`), and metric-computation correctness — PER/BER/spectral-efficiency math (`tests/unit`). No hardware-dependent exception needed: this feature is a pure simulation, nothing runs against physical Bluetooth hardware. |
| III. UX Consistency | PASS (adapted) | This feature has no interactive UI; "UX" is reinterpreted as consistent console/log output and consistent figure styling (titles, axis labels, units, color coding) across the Tx/Channel/Rx stages in `src/common/plotHelpers.m`, so the video's simulation-evidence segment reads as one coherent artifact rather than three mismatched outputs. |
| IV. Simplicity & YAGNI | PASS | Single channel scenario (no sweep, per FR-004); the mandatory official example is used unmodified as the starting point before any customization (FR-003); the only addition beyond the mandatory example (an LE waveform/receiver path) is justified in writing in `research.md` Decision 3 because it is a hard spec requirement (FR-002) the example doesn't cover — not speculative scope. |
| Additional Constraints (dependencies) | PASS | Bluetooth/WLAN/Communications Toolbox choices are dictated by the source assignment (§2) and documented with rationale in `research.md`; no hardware-specific SDK is involved (pure simulation), so the minimum-supported-version record is: MATLAB R2026a + the Bluetooth SIG PHY versions implemented by `bluetoothWaveformGenerator`/`bleWaveformGenerator` in that toolbox release. |
| Development Workflow | PASS | Tests (`matlab.unittest`) and lint (`checkcode`) must pass before any stage is marked done; self-review checklist applies if this remains single-contributor work, team review applies if teammates join. |

No violations — Complexity Tracking table is omitted.

**Post-Design re-check** (after Phase 1 `data-model.md`/`contracts/`/`quickstart.md`):
still PASS on all rows. The Phase 1 artifacts added no new dependency, no new
scenario, and no speculative abstraction beyond what Decisions 1-6 in `research.md`
already justified — the per-stage contract split in `contracts/stage-interfaces.md`
exists to make the three role-owners' work independently testable (a spec
requirement), not as premature architecture.

## Project Structure

### Documentation (this feature)

```text
specs/001-bluetooth-link-simulation/
├── plan.md              # This file (/speckit-plan command output)
├── research.md          # Phase 0 output (/speckit-plan command)
├── data-model.md        # Phase 1 output (/speckit-plan command)
├── quickstart.md        # Phase 1 output (/speckit-plan command)
├── contracts/           # Phase 1 output (/speckit-plan command)
│   └── stage-interfaces.md
└── tasks.md             # Phase 2 output (/speckit-tasks command - NOT created by /speckit-plan)
```

### Source Code (repository root)

```text
src/
├── tx/
│   ├── generateBREDRWaveform.m     # Role A (BR/EDR): wraps bluetoothWaveformGenerator
│   └── generateLEWaveform.m        # Role A (LE): wraps bleWaveformGenerator
├── channel/
│   └── runCoexistenceScenario.m    # Role B: wraps bluetoothNode/wirelessNetworkSimulator
│                                    # + helperInterferingWLANNode, parameterized per
│                                    # research.md Decision 1, AFH on/off toggle
├── rx/
│   ├── measureLinkBER.m            # Role C (link-level): bluetoothIdealReceiver /
│   │                                # bleIdealReceiver bit-for-bit comparison
│   └── summarizeCoexistenceMetrics.m  # Role C (network-level): PER, AFH-on-vs-off
│                                       # delta, spectral efficiency, SNR margin
└── common/
    ├── scenarioConfig.m            # Shared, cited channel/interference parameters
    └── plotHelpers.m               # Shared figure styling for video evidence

tests/
├── contract/    # Verifies the Tx/Channel/Rx data contracts in contracts/stage-interfaces.md
├── integration/ # End-to-end Tx→Channel→Rx runs: BR/EDR path, LE path, AFH on vs. off
└── unit/        # PER/BER/spectral-efficiency computation correctness, isolated from toolbox runs

results/
├── figures/     # Eye diagrams, PER-vs-time, hopping-sequence visualizations for the video
└── metrics/     # Logged .mat/.csv metrics (BER, PER, SNR margin, spectral efficiency)
```

**Structure Decision**: Single MATLAB project (template's Option 1, adapted to
MATLAB conventions — no `web`/`mobile` split applies). Each of the three technical
user stories owns its own top-level `src/` subfolder (`tx`, `channel`, `rx`) so the
three role-owners named in the source assignment can work independently against the
shared contract in `contracts/stage-interfaces.md`, matching the "independently
testable" requirement in spec.md.
