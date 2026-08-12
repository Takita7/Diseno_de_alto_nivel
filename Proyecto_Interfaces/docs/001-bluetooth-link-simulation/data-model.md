# Phase 1 Data Model: Bluetooth BR/EDR & LE Link Simulation with WLAN Interference

Entities below correspond to the Key Entities in `spec.md`, refined with the concrete
fields each toolbox-backed implementation will actually carry. These are data
shapes (structs), not classes with behavior — this feature is a set of simulation
scripts, not a stateful application.

## BluetoothWaveform

Represents one generated, standard-conformant transmit signal (User Story 1).

| Field | Type | Notes |
|---|---|---|
| `mode` | string enum: `"BREDR"` \| `"LE"` | Which Bluetooth PHY family this waveform represents |
| `phyVariant` | string | For BR/EDR: packet type (e.g. `"DH1"`, `"EV3"`); for LE: PHY mode (`"LE1M"`, `"LE2M"`, `"LE500K"`, `"LE125K"`) |
| `configObject` | `bluetoothWaveformConfig` \| name-value struct | The exact toolbox configuration used to generate this waveform (kept for reproducibility and for the video's "how it was configured" narration) |
| `samples` | complex column vector | Output of `bluetoothWaveformGenerator` or `bleWaveformGenerator` |
| `sampleRate` | double (Hz) | Required by downstream channel/receiver stages |
| `payloadBits` | int8 vector | The known transmitted bits, retained so User Story 1's BER check has ground truth to compare against |
| `sourceReference` | string | Which official example/function this was generated from (traceability, satisfies FR-003) |

**Validation rules** (from FR-001/FR-002/FR-003):
- `mode`/`phyVariant` combination MUST be one the toolbox natively supports (see
  `research.md` Decision 3) or explicitly flagged as a manually-built exception.
- `samples` MUST be non-empty and produced by a toolbox generator function, never
  hand-computed modulation (FR-010).

## ChannelScenario

Represents the single representative 2.4 GHz coexistence scenario (User Story 2).

| Field | Type | Notes |
|---|---|---|
| `scenarioName` | string | Fixed to the mandatory official example's scenario for this feature (Decision 1) |
| `interferenceSource` | struct: `{type, centerFrequency, bandwidth, periodicity}` | e.g. WLAN HE-SU interferer at 2.442-2.447 GHz, 20 MHz bandwidth, 2 ms periodicity — values from the official example |
| `classificationIntervalMs` | double | AFH re-evaluation period (default 250 ms per the official example) |
| `perThreshold` | double (0-1) | PER threshold used to mark a channel "bad" (default 40%) |
| `afhEnabled` | bool | The toggle that produces the two conditions compared in User Story 3 |
| `citationSource` | string | Where each parameter above traces back to (FR-005) — the official MathWorks example page, itself citing Bluetooth SIG/IEEE coexistence norms |

**Validation rules** (from FR-004/FR-005):
- Exactly one `ChannelScenario` instance is defined per simulation run — no sweep
  across multiple scenarios.
- Every field with a numeric value MUST have a non-empty `citationSource`.

## PerformanceMetricsReport

Represents the measured/derived results the video must present (User Stories 3-4).

| Field | Type | Notes |
|---|---|---|
| `scenario` | reference to `ChannelScenario` | Which run produced this report |
| `per` | struct: `{afhOff: double, afhOn: double}` | Packet Error Rate under each AFH condition (FR-007) |
| `ber` | double or n/a | Link-level bit error rate from the User Story 1 conformance check (Decision 2); may be `n/a` for the network-level coexistence run itself |
| `performanceMargin` | double (dB) | SNR required to reach the target BER/PER (normalized metric #1, FR-008) |
| `spectralEfficiencyBitsPerHz` | double | Derived per Decision 4 (normalized metric #4, FR-008) |
| `degradationSensitivity` | string (qualitative) | Free-text statement of expected margin behavior if the channel worsened (normalized metric #3, FR-008) — no sweep is run, so this stays qualitative per FR-004 |
| `afhImprovementDelta` | double | `per.afhOff - per.afhOn`, the explicit comparison required by FR-007/Acceptance Scenario 2 of User Story 3 |

**Validation rules** (from FR-006/FR-007/FR-008, SC-003/SC-004):
- `per.afhOff` and `per.afhOn` MUST both be present before `afhImprovementDelta` is
  considered valid.
- All four normalized metrics (`performanceMargin`, `per` or `ber` as applicable,
  `degradationSensitivity`, `spectralEfficiencyBitsPerHz`) MUST be populated before
  a `PerformanceMetricsReport` is considered ready to feed the video (User Story 4).

## VideoDeliverable

Represents the single submitted artifact (User Story 4). Tracked as a checklist/
metadata record, not simulation data.

| Field | Type | Notes |
|---|---|---|
| `durationMinutes` | double | MUST be in `[15, 20]` (FR-009, SC-005) |
| `sections` | set of 6 required section names | history, architecture/stack, frame-format, applications, trends, simulation-evidence — all MUST be present (FR-009) |
| `linkedMetricsReports` | list of `PerformanceMetricsReport` | Evidence the simulation-evidence section draws from |
| `linkedFigures` | list of file paths under `results/figures/` | Eye diagrams / PER plots / hopping visualizations referenced in the video |

**Validation rules** (from FR-009, SC-005):
- `durationMinutes` outside `[15, 20]` fails SC-005 and blocks submission.
- `sections` MUST equal the required six — no substitutions, no omissions.

## Relationships

```
BluetoothWaveform (BR/EDR) ──┐
                              ├──> ChannelScenario (coexistence run) ──> PerformanceMetricsReport
BluetoothWaveform (LE)  ─────┘        (BR/EDR path only; LE is link-level only,
   │                                   per Decision 3 — no coexistence run for LE)
   └──> measureLinkBER (User Story 1) ──> PerformanceMetricsReport.ber

PerformanceMetricsReport(s) ──> VideoDeliverable.linkedMetricsReports
results/figures/* ──> VideoDeliverable.linkedFigures
```
