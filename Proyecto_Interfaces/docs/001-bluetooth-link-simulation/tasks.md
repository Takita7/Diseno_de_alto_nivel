---

description: "Task list for Bluetooth BR/EDR & LE Link Simulation with WLAN Interference"

---

# Tasks: Bluetooth BR/EDR & LE Link Simulation with WLAN Interference

**Input**: Design documents from `/specs/001-bluetooth-link-simulation/`

**Prerequisites**: plan.md, spec.md, research.md, data-model.md, contracts/stage-interfaces.md, quickstart.md (all present)

**Tests**: Included. The project constitution's Test Coverage principle makes automated tests non-negotiable ("New functionality MUST ship with automated tests covering its primary behavior and known edge cases"), and `plan.md`'s Constitution Check already commits to `matlab.unittest`-based tests in `tests/unit`, `tests/contract`, `tests/integration`.

**Organization**: Tasks are grouped by user story (from `spec.md`) to enable independent implementation and testing of each story.

## Format: `[ID] [P?] [Story] Description`

- **[P]**: Can run in parallel (different files, no dependencies on incomplete tasks)
- **[Story]**: Which user story this task belongs to (US1, US2, US3, US4)
- Every task includes an exact file path

## Path Conventions

Single MATLAB project (per `plan.md` Structure Decision): `src/`, `tests/`, `results/` at repository root.

---

## Phase 1: Setup (Shared Infrastructure)

**Purpose**: Project initialization and basic structure

- [ ] T001 Create the directory tree from `plan.md`: `src/tx/`, `src/channel/`, `src/rx/`, `src/common/`, `tests/contract/`, `tests/integration/`, `tests/unit/`, `results/figures/`, `results/metrics/`
- [ ] T002 [P] Verify MATLAB R2026a toolbox licenses are active (Bluetooth Toolbox, Communications Toolbox, WLAN Toolbox) using the `license('test', ...)` calls in `quickstart.md` Prerequisites; record the result in `specs/001-bluetooth-link-simulation/quickstart.md` (append a "Verified on" note)
- [ ] T003 [P] Create a repository-root `startup.m` that runs `addpath(genpath('src'))` so all `src/` functions are on the MATLAB path automatically

**Checkpoint**: Directory structure exists, toolboxes confirmed licensed, path configured.

---

## Phase 2: Foundational (Blocking Prerequisites)

**Purpose**: Shared data shapes and configuration that every user story depends on

**⚠️ CRITICAL**: No user story work can begin until this phase is complete

- [ ] T004 [P] Create `src/common/scenarioConfig.m` returning the `ChannelScenario` struct (per `data-model.md`) with the mandatory coexistence example's cited parameters: interference source (WLAN HE-SU interferer, 2.442-2.447 GHz, 20 MHz, 2 ms periodicity), `classificationIntervalMs=250`, `perThreshold=0.40`, and a `citationSource` string for each value (FR-005, `research.md` Decision 1)
- [ ] T005 [P] Create `src/common/plotHelpers.m` with shared figure-styling functions (consistent titles, axis labels, units, color coding) for use by Tx/Channel/Rx stages (`plan.md` UX Consistency row)
- [ ] T006 Create `src/common/newBluetoothWaveform.m` and `src/common/newPerformanceMetricsReport.m` struct constructors that enforce the field shapes and validation rules from `data-model.md` (e.g. `mode` must be `"BREDR"`/`"LE"`, `samples` non-empty)

**Checkpoint**: Foundation ready — user story implementation can now begin.

---

## Phase 3: User Story 1 - Generate a standard-conformant Bluetooth waveform (Priority: P1) 🎯 MVP

**Goal**: Produce conformant BR/EDR and LE waveforms and a link-level BER check proving the Tx→Rx contract holds (spec.md User Story 1).

**Independent Test**: Generate a BR/EDR waveform and a separate LE waveform and confirm each matches the Bluetooth SIG structure for that mode, independent of any channel or receiver-under-interference work.

### Tests for User Story 1

> Write these tests FIRST, ensure they FAIL before implementation exists.

- [ ] T007 [P] [US1] Contract test: `generateBREDRWaveform.m` output matches the `BluetoothWaveform` shape (Contract 1) in `tests/contract/test_tx_bredr_contract.m`
- [ ] T008 [P] [US1] Contract test: `generateLEWaveform.m` output matches the `BluetoothWaveform` shape (Contract 1) in `tests/contract/test_tx_le_contract.m`
- [ ] T009 [P] [US1] Unit test: `measureLinkBER.m` raises an error on an invalid `mode` or empty `samples`, per Contract 1's Failure mode, in `tests/unit/test_measureLinkBER_errors.m`
- [ ] T010 [P] [US1] Integration test: full BR/EDR Tx→Rx round trip yields a near-zero BER (spec.md Acceptance Scenario 1) in `tests/integration/test_bredr_conformance.m`
- [ ] T011 [P] [US1] Integration test: full LE Tx→Rx round trip yields a near-zero BER (spec.md Acceptance Scenario 2) in `tests/integration/test_le_conformance.m`

### Implementation for User Story 1

- [ ] T012 [P] [US1] Implement `src/tx/generateBREDRWaveform.m` wrapping `bluetoothWaveformGenerator`/`bluetoothWaveformConfig`, returning a `BluetoothWaveform` struct via `newBluetoothWaveform.m` (FR-001, FR-003, FR-010)
- [ ] T013 [P] [US1] Implement `src/tx/generateLEWaveform.m` wrapping `bleWaveformGenerator` (default `Mode="LE1M"`), returning a `BluetoothWaveform` struct via `newBluetoothWaveform.m` (FR-002, `research.md` Decision 3)
- [ ] T014 [US1] Implement `src/rx/measureLinkBER.m`: dispatch to `bluetoothIdealReceiver` or `bleIdealReceiver` by `mode`, bit-for-bit compare against `payloadBits`, return `ber` + `pktValidStatus`, with the error handling from Contract 1 (depends on T006, T012, T013)
- [ ] T015 [US1] Run `checkcode` on `src/tx/generateBREDRWaveform.m`, `src/tx/generateLEWaveform.m`, and `src/rx/measureLinkBER.m` and resolve all warnings (constitution Code Quality)

**Checkpoint**: User Story 1 is fully functional and independently testable — BR/EDR and LE waveforms generate and link-level BER measurement works.

---

## Phase 4: User Story 2 - Model a representative, justified channel with WLAN interference (Priority: P2)

**Goal**: A single 2.4 GHz coexistence scenario, sourced from the mandatory official example, with every parameter traceable to a citable source (spec.md User Story 2).

**Independent Test**: Inspect the channel model's parameters and confirm each is traceable to a cited source, without needing the transmitter or receiver components finished.

### Tests for User Story 2

- [ ] T016 [P] [US2] Contract test: `runCoexistenceScenario.m` output matches the Contract 2 input struct shape (`scenario`, `afhEnabled`, `perPacketLog`, `simDurationSec`) in `tests/contract/test_channel_contract.m`
- [ ] T017 [P] [US2] Unit test: every field returned by `scenarioConfig.m` has a non-empty `citationSource` (FR-005) in `tests/unit/test_scenarioConfig_citations.m`
- [ ] T018 [P] [US2] Integration test: running the scenario (AFH disabled) produces a non-empty `perPacketLog` (quickstart.md Step 4) in `tests/integration/test_coexistence_baseline.m`

### Implementation for User Story 2

- [ ] T019 [US2] Implement `src/channel/runCoexistenceScenario.m` wrapping `bluetoothNode`, `wirelessNetworkSimulator`, `bluetoothConnectionConfig`, `helperInterferingWLANNode`, and `helperBluetoothChannelClassification`, with an `AFHEnabled` name-value toggle (FR-004, FR-007, `research.md` Decision 1) (depends on T004)
- [ ] T020 [US2] Ensure `runCoexistenceScenario.m` sources every parameter exclusively from `src/common/scenarioConfig.m` — no arbitrary inline values (FR-005) (depends on T019)
- [ ] T021 [US2] Run `checkcode` on `src/channel/runCoexistenceScenario.m` and resolve all warnings (constitution Code Quality)

**Checkpoint**: User Stories 1 AND 2 both work independently.

---

## Phase 5: User Story 3 - Measure error rate and interference mitigation impact (Priority: P3)

**Goal**: Quantify PER with AFH disabled vs. enabled under the same scenario, plus the derived normalized metrics (spec.md User Story 3).

**Independent Test**: Once a waveform and channel scenario exist, run the receiver against the interference scenario twice (AFH disabled, then enabled) and confirm a measurable PER difference is produced and recorded.

### Tests for User Story 3

- [ ] T022 [P] [US3] Contract test: `summarizeCoexistenceMetrics.m` rejects two runs with mismatched `scenario` values (Contract 2 Failure mode) in `tests/contract/test_metrics_mismatched_scenario.m`
- [ ] T023 [P] [US3] Contract test: `summarizeCoexistenceMetrics.m` raises an error on an empty `perPacketLog` rather than reporting `per = 0` (Contract 2 Failure mode) in `tests/contract/test_metrics_empty_log.m`
- [ ] T024 [P] [US3] Unit test: spectral-efficiency computation matches `throughput ÷ occupied bandwidth` (`research.md` Decision 4) in `tests/unit/test_spectralEfficiency.m`
- [ ] T025 [P] [US3] Unit test: performance-margin (SNR-to-target-PER/BER) computation in `tests/unit/test_performanceMargin.m`
- [ ] T026 [US3] Integration test: comparing an AFH-off run against an AFH-on run of the same scenario produces `per.afhOff`, `per.afhOn`, and a correctly-computed `afhImprovementDelta` (spec.md User Story 3 Acceptance Scenario 2, quickstart.md Step 5) in `tests/integration/test_afh_comparison.m`

### Implementation for User Story 3

- [ ] T027 [US3] Implement `src/rx/summarizeCoexistenceMetrics.m`: extract PER from `perPacketLog`, compute `afhImprovementDelta`, enforce the scenario-match and non-empty-log validations from Contract 2 (depends on T006, T019)
- [ ] T028 [P] [US3] Implement `src/common/computeSpectralEfficiency.m` per `research.md` Decision 4 (FR-008 metric #4)
- [ ] T029 [P] [US3] Implement `src/common/computePerformanceMargin.m` (FR-008 metric #1)
- [ ] T030 [US3] Wire `summarizeCoexistenceMetrics.m` to populate `PerformanceMetricsReport.ber` from `measureLinkBER` results and set a qualitative `degradationSensitivity` statement (data-model.md) (depends on T014, T027, T028, T029)
- [ ] T031 [US3] Generate `results/figures/` outputs (eye diagram/constellation, PER-vs-condition comparison, hopping-sequence visualization via `helperVisualizeCoexistence`) and `results/metrics/` logs (`.mat`/`.csv`), using `src/common/plotHelpers.m` for consistent styling (FR-008) (depends on T005, T030)
- [ ] T032 [US3] Run `checkcode` on `src/rx/summarizeCoexistenceMetrics.m`, `src/common/computeSpectralEfficiency.m`, and `src/common/computePerformanceMargin.m` and resolve all warnings

**Checkpoint**: User Stories 1-3 are all independently functional; a fully-populated `PerformanceMetricsReport` exists.

---

## Phase 6: User Story 4 - Assemble the explanatory video deliverable (Priority: P4)

**Goal**: Package the six required conceptual/technical sections plus the simulation evidence from User Stories 1-3 into one 15-20 minute video (spec.md User Story 4).

**Independent Test**: Once simulation evidence from User Stories 1-3 is available, check the recorded video against the required section list and time budget.

### Tests for User Story 4

- [ ] T033 [P] [US4] Integration test: a full pipeline run (US1-3 outputs) produces a `PerformanceMetricsReport` that passes the four-normalized-metric completeness check from Contract 3 in `tests/integration/test_video_readiness.m`

### Implementation for User Story 4

- [ ] T034 [US4] Implement `src/common/validateVideoReadiness.m` checking report completeness (all four normalized metrics populated) and presence of the required figures under `results/figures/` (Contract 3) (depends on T031)
- [ ] T035 [US4] Draft `specs/001-bluetooth-link-simulation/video-outline.md`: a script/outline covering the six required sections and their time budgets (history 2-3 min, architecture/stack 3-4 min, frame format 3-4 min, applications 2 min, trends 2 min, simulation evidence 4-5 min) referencing the figures/metrics produced in Phase 5 (FR-009)
- [ ] T036 [US4] Record the video, time it, and verify it falls within 15-20 minutes (SC-005) and covers all six sections (FR-009) — manual step, checklist in `quickstart.md` Step 7 (depends on T034, T035)

**Checkpoint**: All four user stories complete; video ready for submission per the source assignment's logistics (§7.2, out of this feature's technical scope).

---

## Phase 7: Polish & Cross-Cutting Concerns

**Purpose**: Final verification across all stories

- [ ] T037 [P] Run `checkcode` across the entire `src/` tree and confirm zero warnings remain (constitution Code Quality)
- [ ] T038 [P] Run the full `matlab.unittest` suite (`tests/unit`, `tests/contract`, `tests/integration`) and confirm 100% pass (constitution Test Coverage)
- [ ] T039 Execute `quickstart.md` Steps 1-7 end-to-end as a final validation pass
- [ ] T040 [P] Review every `src/*.m` file that produces console output or figures for consistent use of `src/common/plotHelpers.m` (constitution UX Consistency, adapted)

---

## Dependencies & Execution Order

### Phase Dependencies

- **Setup (Phase 1)**: No dependencies — start immediately
- **Foundational (Phase 2)**: Depends on Setup — BLOCKS all user stories
- **User Story 1 (Phase 3)**: Depends only on Foundational
- **User Story 2 (Phase 4)**: Depends only on Foundational (independent of US1)
- **User Story 3 (Phase 5)**: Depends on Foundational; T027/T030 also depend on US2's `runCoexistenceScenario.m` (T019) and US1's `measureLinkBER.m` (T014) — the metrics-summarization step is where the three roles' outputs actually converge, matching the source assignment's role structure
- **User Story 4 (Phase 6)**: Depends on a completed `PerformanceMetricsReport`, i.e. Phase 5
- **Polish (Phase 7)**: Depends on all four user stories being complete

### User Story Dependencies

- **US1 (P1)**: No dependency on other stories — can start right after Foundational
- **US2 (P2)**: No dependency on other stories — can start right after Foundational, in parallel with US1
- **US3 (P3)**: Integrates US1's `measureLinkBER` and US2's `runCoexistenceScenario` outputs, but its own contract tests (T022-T025) and unit tests can be written and run independently of US1/US2 being finished
- **US4 (P4)**: Packages US1-3 outputs; cannot start meaningfully until Phase 5's checkpoint

### Parallel Opportunities

- T002, T003 (Setup) in parallel
- T004, T005 (Foundational) in parallel; T006 depends on neither but is listed after for readability
- All US1 tests (T007-T011) in parallel; T012/T013 (Tx implementations) in parallel with each other
- All US2 tests (T016-T018) in parallel
- US1 and US2 implementation phases (3 and 4) can proceed in parallel by different people, matching the source assignment's Role A / Role B split
- All US3 tests (T022-T025) in parallel; T028/T029 (metric-computation helpers) in parallel with each other
- T037, T038, T040 (Polish) in parallel

---

## Parallel Example: User Story 1

```bash
# Launch all US1 tests together:
Task: "Contract test for generateBREDRWaveform.m in tests/contract/test_tx_bredr_contract.m"
Task: "Contract test for generateLEWaveform.m in tests/contract/test_tx_le_contract.m"
Task: "Unit test for measureLinkBER.m error handling in tests/unit/test_measureLinkBER_errors.m"
Task: "Integration test for BR/EDR round trip in tests/integration/test_bredr_conformance.m"
Task: "Integration test for LE round trip in tests/integration/test_le_conformance.m"

# Launch the two Tx implementations together:
Task: "Implement src/tx/generateBREDRWaveform.m"
Task: "Implement src/tx/generateLEWaveform.m"
```

---

## Implementation Strategy

### MVP First (User Story 1 Only)

1. Complete Phase 1: Setup
2. Complete Phase 2: Foundational (blocks everything else)
3. Complete Phase 3: User Story 1
4. **STOP and VALIDATE**: confirm BR/EDR and LE waveforms are conformant and link-level BER measurement works, independent of any channel/interference work
5. This alone demonstrates Role A's (Transmisor) full contribution from the source assignment

### Incremental Delivery

1. Setup + Foundational → foundation ready
2. Add US1 (Tx conformance) → validate independently → this is the MVP
3. Add US2 (Channel scenario) → validate independently, in parallel with US1 if staffed
4. Add US3 (PER/AFH metrics) → validate independently — this is where US1 and US2 outputs converge
5. Add US4 (video) → the only graded deliverable, packaging everything above

### Parallel Team Strategy (matches the source assignment's 4-person team)

1. Everyone completes Setup + Foundational together
2. Once Foundational is done:
   - Role A (Transmisor): User Story 1
   - Role B (Canal): User Story 2
   - Role C (Receptor/Validación): starts US3's tests/unit work early (T024, T025 don't depend on US1/US2), then integrates once US1 and US2 land
   - Whoever is free: starts drafting the video outline (T035) early, per the source PDF's suggested schedule ("La integración y el video ... arrancan en paralelo")
3. Stories converge in US3, then package into US4

---

## Notes

- [P] tasks = different files, no unmet dependencies
- [Story] label maps each task to its user story for traceability against spec.md
- Every implementation task names its exact file path, per plan.md's Project Structure
- Commit after each task or logical group
- Stop at any checkpoint to validate a story independently before moving on
