# Phase 0 Research: Bluetooth BR/EDR & LE Link Simulation with WLAN Interference

All Technical Context unknowns from `plan.md` are resolved below — no
`NEEDS CLARIFICATION` markers remain.

## Decision 1: Base the channel/AFH work on the mandatory official coexistence example

**Decision**: Use `bluetoothNode` + `wirelessNetworkSimulator` +
`bluetoothConnectionConfig` + `helperInterferingWLANNode` +
`helperBluetoothChannelClassification` (+ `helperVisualizeCoexistence` for figures)
as the base implementation for the Channel scenario (User Story 2) and the
AFH-mitigation comparison (User Story 3).

**Rationale**: This is exactly the object/function set used by MathWorks' "Bluetooth
BR Data and Voice Communication with WLAN Signal Interference" example — the
official reference the source assignment (§11, item 4) names as the *mandatory*
starting point for Team 5. It already implements a literature-grounded 2.4 GHz
WLAN-coexistence scenario with built-in adaptive frequency hopping (channel
classification into "good"/"bad" channels, periodic re-evaluation every 250 ms by
default), so FR-004, FR-005, and FR-007 are satisfied by the starting point itself,
before any customization.

**Alternatives considered**: Hand-building a link-level channel with
`comm.RayleighChannel` plus a manually-scripted interferer — rejected because it
would not trace to the mandatory official example (violating FR-003 and the
constitution's Simplicity/YAGNI principle, which requires justifying any deviation
from the simplest, spec-mandated starting point) and would duplicate AFH logic the
toolbox already provides correctly.

## Decision 2: Report PER as the primary error-rate metric for the interference scenario; BER for link-level conformance

**Decision**: For the channel/AFH comparison (User Stories 2-3), report **Packet
Error Rate (PER)** — computed from `PacketReceptionEnded` statistics inside the
coexistence simulation — as the primary error-rate metric. Separately, for the
waveform-conformance check (User Story 1), report **Bit Error Rate (BER)** from a
direct bit-for-bit comparison between transmitted and `bluetoothIdealReceiver` /
`bleIdealReceiver`-decoded payloads.

**Rationale**: The mandatory coexistence example is a discrete-event, packet-level
network simulation — it does not expose raw per-bit visibility, so it cannot produce
BER directly; it computes PER natively. The source PDF's own metric definition
(§8, item 2) explicitly allows "BER, PER, o BLER según corresponda a la tecnología,"
so using the toolbox-native PER for the network-level scenario is a conforming
choice, not a deviation. BER remains meaningful and available at the link level
(User Story 1's Tx/Rx conformance check), which is a simpler, isolated measurement
using the standalone ideal-receiver functions.

**Alternatives considered**: Reconstructing bit-level visibility inside the
network-level coexistence simulation to force a BER number — rejected as
unnecessary complexity for this feature's bounded scope (FR-004 rules out anything
beyond one scenario) and explicitly permitted against by the assignment's own
flexible metric wording.

## Decision 3: Add a supplemental LE waveform/receiver path (not covered by the mandatory example)

**Decision**: Generate the required LE waveform (FR-002) with `bleWaveformGenerator`
(default mode `LE1M`) and validate it with `bleIdealReceiver`, as a path separate
from — but structured consistently with — the mandatory BR/EDR coexistence example.

**Rationale**: The mandatory official example (source PDF §11.4) is titled "Bluetooth
**BR** Data and Voice Communication with WLAN Signal Interference" and, per its own
documentation, only models BR/EDR traffic; it does not generate or receive LE
waveforms. The source PDF (§5.5, Role A) requires *both* BR/EDR and LE waveforms.
This is precisely the situation spec.md's Edge Cases section already anticipates:
"What happens when the official reference example does not natively support one of
the required waveform types? ... the team must document that gap and build the
missing piece manually, per the assignment's stated exception" (source PDF §10: "no
reconstruida manualmente desde cero, salvo en los casos donde el toolbox no ofrezca
soporte nativo" — the toolbox *does* offer native LE support via
`bleWaveformGenerator`/`bleIdealReceiver`, so "manually" here means "via the
toolbox's own LE-specific functions," not reconstructing modulation math by hand).

**Alternatives considered**: Skipping LE and only delivering BR/EDR to stay
literally inside the one official example — rejected because FR-002 makes LE a hard
requirement sourced directly from the assignment, not an optional extension.

## Decision 4: Compute spectral efficiency as a derived metric

**Decision**: Compute spectral efficiency (bits/s/Hz) per scenario as
`achieved throughput ÷ occupied channel bandwidth`, using throughput already
produced by the coexistence simulation's statistics and the standard Bluetooth
channel spacing (1 MHz for BR/EDR and LE1M/LE2M-class channels), rather than any
dedicated toolbox "spectral efficiency" function (none exists).

**Rationale**: FR-008/SC-004 require spectral efficiency as one of the four
normalized comparison metrics mandated by the source PDF (§8, item 4). No Bluetooth
Toolbox function computes it directly, but it is a straightforward derived quantity
from data the simulation already produces, keeping the implementation simple (no new
dependency).

**Alternatives considered**: Omitting spectral efficiency — rejected; it is an
explicit, non-optional requirement in both spec.md and the source PDF.

## Decision 5: Testing and static-analysis tooling

**Decision**: Use `matlab.unittest.TestCase`-based automated tests for all
functional test tiers (`tests/unit`, `tests/contract`, `tests/integration`), and
`checkcode` (MATLAB's built-in static analyzer) as the lint/code-quality gate.

**Rationale**: Both are native to the MATLAB environment the assignment already
requires (source PDF §2: full MATLAB license with the listed toolboxes) — no
additional tooling or licensing needed. This directly satisfies the constitution's
Code Quality and Test Coverage principles.

**Alternatives considered**: Ad hoc `assert()` scripts without a test framework —
rejected as harder to discover and run consistently than a standard framework, and
the constitution requires "automated tests," not just assertions.

## Decision 6: Target environment

**Decision**: Target MATLAB R2026a (Update 2, 26.1.0.3251617), confirmed installed
locally with the Bluetooth, Communications, and WLAN toolboxes present.

**Rationale**: Verified directly against the local MATLAB installation
(`/usr/local/MATLAB/R2026a/toolbox/{bluetooth,comm,wlan}`) and its `VersionInfo.xml`.
Matches the source PDF's "licencia completa de MATLAB" requirement (§2). No version
ambiguity remains.

**Alternatives considered**: N/A — dictated by the already-installed environment.
