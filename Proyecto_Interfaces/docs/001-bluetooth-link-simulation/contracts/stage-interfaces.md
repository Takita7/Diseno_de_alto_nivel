# Stage Interface Contracts: Tx → Channel → Rx

This project has no network/web API — its "interfaces" are the internal data
handoffs between the three technical roles from the source assignment (Role A/Tx,
Role B/Channel, Role C/Rx). Each role can be implemented independently against the
contract below, then integrated (this is what makes User Stories 1-3 in spec.md
independently testable).

## Contract 1: Tx → link-level Rx (BR/EDR and LE conformance, User Story 1)

**Producer**: `src/tx/generateBREDRWaveform.m`, `src/tx/generateLEWaveform.m`
**Consumer**: `src/rx/measureLinkBER.m`

**Input to consumer** — one `BluetoothWaveform` struct (see `data-model.md`) with,
at minimum:

```text
struct(
  mode          = "BREDR" | "LE",
  phyVariant    = <string>,     % e.g. "DH1" or "LE1M"
  samples       = <Nx1 complex double/single>,
  sampleRate    = <double, Hz>,
  payloadBits   = <int8 vector>,   % ground truth for BER comparison
  sourceReference = <string>
)
```

**Consumer contract**: `measureLinkBER.m` MUST accept exactly this struct shape,
run the mode-appropriate ideal receiver (`bluetoothIdealReceiver` for `"BREDR"`,
`bleIdealReceiver` for `"LE"`), compare decoded bits against `payloadBits`, and
return a `ber` scalar in `[0, 1]` plus a `pktValidStatus` boolean. It MUST NOT
assume any field beyond the ones listed above, so either Tx function can be swapped
independently.

**Failure mode**: If `mode` is neither `"BREDR"` nor `"LE"`, or `samples` is empty,
`measureLinkBER.m` MUST raise an error rather than silently returning a zero BER —
a silent zero would be indistinguishable from a genuinely perfect link.

## Contract 2: Channel (coexistence run) → network-level Rx metrics (User Stories 2-3)

**Producer**: `src/channel/runCoexistenceScenario.m`
**Consumer**: `src/rx/summarizeCoexistenceMetrics.m`

**Input to consumer** — the `wirelessNetworkSimulator` statistics/log produced by
running the mandatory coexistence example (`research.md` Decision 1), wrapped as:

```text
struct(
  scenario       = <ChannelScenario struct, see data-model.md>,
  afhEnabled     = <bool>,          % which of the two runs this is
  perPacketLog   = <table/struct array>,  % PacketReceptionEnded records
  simDurationSec = <double>
)
```

**Consumer contract**: `summarizeCoexistenceMetrics.m` MUST accept two such structs
(one with `afhEnabled = false`, one with `afhEnabled = true`, same `scenario`) and
return a populated `PerformanceMetricsReport` (see `data-model.md`) with `per.afhOff`,
`per.afhOn`, and `afhImprovementDelta` all set. It MUST reject mismatched
`scenario` values between the two inputs (comparing PER across two different
scenarios is not a valid AFH-mitigation comparison, per FR-007's "under the same
interference scenario" requirement).

**Failure mode**: If `perPacketLog` is empty for either run, the consumer MUST
raise an error rather than reporting `per = 0`, for the same reason as Contract 1.

## Contract 3: PerformanceMetricsReport → VideoDeliverable (User Story 4)

**Producer**: `src/rx/measureLinkBER.m` (fills `.ber`),
`src/rx/summarizeCoexistenceMetrics.m` (fills everything else)
**Consumer**: the video-assembly checklist (manual/human step — not code; see
`quickstart.md`)

**Input to consumer**: one or more completed `PerformanceMetricsReport` structs
(all four normalized metrics populated, per `data-model.md` validation rules) plus
the figure files under `results/figures/`.

**Consumer contract**: Before recording, every `PerformanceMetricsReport` referenced
by the video MUST pass the "ready to feed the video" validation rule in
`data-model.md` (all four normalized metrics present). This is a manual gate, not
code, but `tests/integration` MUST include a check that the reports produced by a
full pipeline run satisfy it, so the gap is caught before recording rather than
during it.
