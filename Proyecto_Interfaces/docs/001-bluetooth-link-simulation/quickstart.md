# Quickstart: Validating the Bluetooth Link Simulation

This is a runnable validation guide, not an implementation walkthrough. It proves
each user story from `spec.md` end-to-end once the corresponding `src/` files exist.
See `data-model.md` for data shapes and `contracts/stage-interfaces.md` for the
handoffs between stages.

## Prerequisites

- MATLAB R2026a (or later) with **Bluetooth Toolbox**, **Communications Toolbox**,
  and **WLAN Toolbox** licensed and on the path. Confirm with:
  ```matlab
  license('test', 'Bluetooth_Toolbox')
  license('test', 'Communication_Toolbox')
  license('test', 'WLAN_System_Toolbox')
  ```
  Each MUST return `1`. (Confirmed locally: toolbox files present under
  `/usr/local/MATLAB/R2026a/toolbox/{bluetooth,comm,wlan}`; license status should
  still be checked, since installed files don't guarantee an active license seat.)
- Repository root on the MATLAB path (`addpath(genpath('src'))`).

## Step 1 — Validate BR/EDR waveform generation (User Story 1, Acceptance Scenario 1)

```matlab
wfm = generateBREDRWaveform();   % src/tx/generateBREDRWaveform.m
assert(strcmp(wfm.mode, "BREDR"));
assert(~isempty(wfm.samples));
```

**Expected outcome**: A `BluetoothWaveform` struct (per `data-model.md`) with a
non-empty complex sample vector and `sourceReference` pointing at
`bluetoothWaveformGenerator`.

## Step 2 — Validate LE waveform generation (User Story 1, Acceptance Scenario 2)

```matlab
wfm = generateLEWaveform();      % src/tx/generateLEWaveform.m
assert(strcmp(wfm.mode, "LE"));
assert(ismember(wfm.phyVariant, ["LE1M","LE2M","LE500K","LE125K"]));
```

**Expected outcome**: Same struct shape as Step 1, `mode = "LE"`.

## Step 3 — Validate link-level BER conformance check (User Story 1 completion)

```matlab
berResult = measureLinkBER(wfm);   % src/rx/measureLinkBER.m, Contract 1
assert(berResult.ber >= 0 && berResult.ber <= 1);
```

**Expected outcome**: A BER value close to 0 for a noiseless/ideal round-trip,
proving the Tx→Rx contract holds before any channel impairment is introduced.

## Step 4 — Run the mandatory channel scenario, AFH disabled (User Story 2)

```matlab
runOff = runCoexistenceScenario('AFHEnabled', false);  % src/channel/runCoexistenceScenario.m
assert(~isempty(runOff.perPacketLog));
```

**Expected outcome**: A populated `perPacketLog`, confirming the mandatory official
coexistence example (Decision 1 in `research.md`) runs and produces packet-level
statistics under WLAN interference.

## Step 5 — Run the same scenario, AFH enabled, and compare (User Story 3)

```matlab
runOn = runCoexistenceScenario('AFHEnabled', true);
report = summarizeCoexistenceMetrics(runOff, runOn);   % Contract 2
assert(report.afhImprovementDelta >= 0, ...
  'AFH is expected to reduce or maintain PER, not worsen it');
```

**Expected outcome**: A `PerformanceMetricsReport` with `per.afhOff`, `per.afhOn`,
and `afhImprovementDelta` populated — the core evidence for FR-007 and
SC-003.

## Step 6 — Complete the normalized metrics (User Story 3 → 4 handoff)

```matlab
report.ber = berResult.ber;
report.spectralEfficiencyBitsPerHz = computeSpectralEfficiency(runOn);  % Decision 4
report.performanceMargin = computePerformanceMargin(runOn);
report.degradationSensitivity = "Qualitative statement, see spec.md Edge Cases";
```

**Expected outcome**: All four normalized metrics from FR-008/SC-004 populated on
`report`, satisfying the `data-model.md` "ready to feed the video" validation rule.

## Step 7 — Confirm video readiness (User Story 4)

Before recording, verify:

- [ ] `report` (Step 6) passes the four-metric validation rule
- [ ] Figures exist under `results/figures/` for: eye diagram / constellation,
      PER-vs-condition bar or table, hopping-sequence visualization
      (`helperVisualizeCoexistence` output)
- [ ] A run-through script/outline covers all six required sections (FR-009)
      and fits the 15-20 minute window (SC-005) — timed with a dry run before the
      final recording

This step is manual (recording/editing/submission logistics are out of scope per
`spec.md` Assumptions), but `tests/integration` should assert that a full pipeline
run (Steps 1-6) produces a `report` and figure set that pass this checklist
programmatically wherever possible (e.g., the four-metric completeness check),
catching gaps before the team sits down to record.
