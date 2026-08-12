# Feature Specification: Bluetooth BR/EDR & LE Link Simulation with WLAN Interference

**Feature Branch**: `001-bluetooth-link-simulation`

**Created**: 2026-08-07

**Status**: Draft

**Input**: User description: "We're working on the bluetooth section of the pdf document inside 'Proyecto_bluetooth'."

**Source document**: `2026_2C_IC_AE.pdf` (MP-6159, Interfaces de Comunicaciones — Maestría en Electrónica, TEC), §5.5 "Equipo 5 — Bluetooth"

## User Scenarios & Testing *(mandatory)*

<!--
  This feature is the technical deliverable for Team 5 (Bluetooth) of a graduate
  communications-interfaces course project: a simulated Tx–Channel–Rx Bluetooth link,
  validated against Bluetooth SIG conformance metrics, culminating in a 15-20 minute
  explanatory video (the only graded deliverable per the assignment).
-->

### User Story 1 - Generate a standard-conformant Bluetooth waveform (Priority: P1)

As the team's transmitter owner, I need a Bluetooth BR/EDR and LE waveform that is
demonstrably conformant to the Bluetooth SIG specification, so that the channel and
receiver work downstream is validating a realistic signal rather than an ad-hoc one.

**Why this priority**: Every other piece of the project (channel modeling, receiver
validation, and the video's simulation-evidence segment) depends on having a
conformant transmit signal first. Nothing else can start without it.

**Independent Test**: Can be fully tested by generating a BR/EDR waveform and a
separate LE waveform and confirming each matches the modulation/coding/frame
structure defined by the Bluetooth SIG specification for that mode, independent of
any channel or receiver work.

**Acceptance Scenarios**:

1. **Given** an official reference example for Bluetooth waveform generation, **When**
   the team parameterizes it for BR/EDR (modulation and coding scheme), **Then** the
   resulting waveform's frame structure and modulation match the Bluetooth SIG
   specification for BR/EDR.
2. **Given** the same starting point, **When** the team parameterizes it for LE,
   **Then** the resulting waveform's structure matches the Bluetooth SIG specification
   for LE.

---

### User Story 2 - Model a representative, justified 2.4 GHz channel with WLAN interference (Priority: P2)

As the team's channel owner, I need a single representative channel scenario that
includes WLAN interference in the 2.4 GHz band, backed by a citable source, so that
the receiver's error-rate results reflect a realistic and defensible coexistence
condition rather than an arbitrary one.

**Why this priority**: The receiver/validation work (User Story 3) cannot produce
meaningful results without a channel to run the signal through, and the assignment's
grading rubric weights channel justification at 15% independently of the other work.

**Independent Test**: Can be tested independently by inspecting the channel model's
parameters and confirming each is traceable to a paper, the Bluetooth SIG
specification, an IEEE/3GPP reference, or documented typical values — without needing
the transmitter or receiver components to be finished.

**Acceptance Scenarios**:

1. **Given** a chosen 2.4 GHz coexistence scenario, **When** the team documents its
   parameters, **Then** every parameter traces to a cited source rather than being
   picked arbitrarily.
2. **Given** the modeled channel, **When** a Bluetooth waveform and a WLAN interferer
   are both present, **Then** the model reflects their simultaneous presence in the
   band (not just the Bluetooth signal alone).

---

### User Story 3 - Measure error rate and interference mitigation impact (Priority: P3)

As the team's receiver/validation owner, I need to measure the Bluetooth link's error
rate under the modeled interference scenario, and compare that error rate with and
without frequency-hopping (AFH) mitigation enabled, so the team can quantify how much
AFH actually helps under WLAN coexistence.

**Why this priority**: This produces the core evidence (BER, mitigation impact) that
both the technical rubric (20% weight for "uso de métricas") and the video's
simulation-evidence segment depend on. It requires User Stories 1 and 2 to be
available first.

**Independent Test**: Can be tested independently once a waveform and channel
scenario exist, by running the receiver against the interference scenario twice (AFH
disabled, then enabled) and confirming a measurable BER difference is produced and
recorded.

**Acceptance Scenarios**:

1. **Given** the Bluetooth waveform passed through the interference channel with AFH
   disabled, **When** the receiver measures performance, **Then** a bit error rate
   (BER) value is produced for that condition.
2. **Given** the same scenario with AFH enabled, **When** the receiver measures
   performance again, **Then** a second BER value is produced and the difference
   between the two conditions is explicitly reported.

---

### User Story 4 - Assemble the explanatory video deliverable (Priority: P4)

As the team, we need to assemble a single 15-20 minute video that combines the
standard's conceptual background (history, architecture, frame format, applications,
trends) with the simulation evidence from User Stories 1-3, so the professor and
peer-reviewing teams can evaluate both technical and conceptual understanding from
one artifact, since no written report is submitted.

**Why this priority**: This is the only graded deliverable (the video, not the
underlying models) and carries the largest single rubric weight (35%), but it is
last because it packages the outputs of the first three stories rather than producing
new technical evidence on its own.

**Independent Test**: Can be tested independently by checking the recorded video
against the required section list and time budget, once simulation evidence from User
Stories 1-3 is available to include.

**Acceptance Scenarios**:

1. **Given** the completed simulation evidence, **When** the team records the video,
   **Then** it runs between 15 and 20 minutes and includes all six required
   sections: history/standardization, architecture/layer stack, frame format,
   applications, trends, and simulation evidence.
2. **Given** the four normalized comparison metrics (performance margin, error rate,
   qualitative channel-degradation sensitivity, spectral efficiency), **When** the
   video is recorded, **Then** all four are explicitly stated so viewers can compare
   this video against the other five teams' videos.

### Edge Cases

- What happens when the official reference example does not natively support one of
  the required waveform types (BR/EDR or LE)? The team must document that gap and
  build the missing piece manually, per the assignment's stated exception.
- How does the team report results if BER remains unacceptably high even with AFH
  mitigation enabled under the chosen interference scenario? This qualitative outcome
  must still be reported, not hidden — it is valid evidence for the "sensitivity to
  channel degradation" metric.
- What happens if the citable source for the channel scenario only documents some of
  the needed parameters? Remaining parameters must be filled from a secondary cited
  source or clearly flagged as an assumption, not invented silently.
- What happens if the video runs over 20 minutes or under 15 minutes? It falls
  outside the assignment's required range and must be re-edited before submission.

## Requirements *(mandatory)*

### Functional Requirements

- **FR-001**: The team MUST generate a Bluetooth BR/EDR waveform that conforms to the
  Bluetooth SIG specification's modulation and frame structure requirements.
- **FR-002**: The team MUST generate a Bluetooth LE waveform that conforms to the
  Bluetooth SIG specification's modulation and frame structure requirements.
- **FR-003**: Waveform generation MUST start from the designated official reference
  example before any custom parameterization is applied.
- **FR-004**: The team MUST model exactly one representative 2.4 GHz channel scenario
  that includes WLAN signal interference; a sensitivity sweep across multiple
  scenarios is explicitly out of scope.
- **FR-005**: Every parameter of the channel scenario MUST be traceable to a cited
  source (published literature, the Bluetooth SIG specification, an IEEE/3GPP
  reference, or documented typical values), not chosen arbitrarily.
- **FR-006**: The team MUST measure the bit error rate (BER) of the Bluetooth link
  under the modeled interference scenario.
- **FR-007**: The team MUST measure and report the BER difference produced by
  enabling frequency-hopping (AFH) interference mitigation versus leaving it
  disabled, under the same interference scenario.
- **FR-008**: The team MUST report four normalized comparison metrics: performance
  margin (SNR required to reach a target BER), error rate under the simulated
  scenario, a qualitative statement of expected sensitivity to channel degradation,
  and spectral efficiency (bits/s/Hz).
- **FR-009**: The team MUST produce one video, 15-20 minutes long, covering: standard
  history/standardization body, protocol architecture/layer stack, frame/packet
  structure, real-world applications, future trends, and the team's own simulation
  evidence.
- **FR-010**: All waveforms MUST be produced through the designated toolbox's own
  functions/apps rather than hand-built from scratch, except for elements the toolbox
  does not natively support (see Edge Cases).

### Key Entities

- **Bluetooth Waveform**: A generated BR/EDR or LE signal; key attributes are mode
  (BR/EDR vs. LE), modulation, coding scheme, and frame/packet structure.
- **Channel Scenario**: The single representative 2.4 GHz propagation and
  interference model; key attributes are the WLAN interference characterization,
  propagation/fading parameters, and its citable justification source.
- **Performance Metrics Report**: The set of measured/derived results per scenario —
  BER (with and without AFH), performance margin, spectral efficiency, and the
  qualitative degradation-sensitivity statement.
- **Video Deliverable**: The single submitted artifact bundling the six required
  content sections and the Performance Metrics Report into one 15-20 minute
  recording.

## Success Criteria *(mandatory)*

### Measurable Outcomes

- **SC-001**: The generated BR/EDR and LE waveforms match the Bluetooth SIG
  specification's structure with zero unexplained deviations from the standard.
- **SC-002**: 100% of the channel scenario's parameters are traceable to a cited
  source when reviewed.
- **SC-003**: A quantified BER difference between AFH-disabled and AFH-enabled
  conditions is available under the same interference scenario.
- **SC-004**: All four normalized comparison metrics (performance margin, error rate,
  degradation sensitivity, spectral efficiency) are present and explicitly stated in
  the final video.
- **SC-005**: The final video runs between 15 and 20 minutes and contains all six
  required content sections.
- **SC-006**: A viewer unfamiliar with the project can, from the video alone, state
  what the measured BER was and whether AFH mitigation helped, without needing
  supplementary material.

## Assumptions

- This spec covers the full Team 5 technical scope described in the source
  document — waveform generation, channel modeling, and receiver validation — plus
  the resulting video content requirements, rather than a single team member's role
  in isolation. If this project is being driven by one team member responsible for
  only part of that scope, the User Stories above still apply individually since each
  is independently testable.
- The channel scenario will be based on the official reference example already
  identified in the source document (Bluetooth BR/EDR coexistence with WLAN
  interference and adaptive frequency hopping), since it is both the mandatory
  starting point and an already-cited, literature-backed scenario.
- Video recording, editing, and submission logistics (screen-capture narration,
  delivery to the professor's shared folder, peer-coevaluation viewing) are treated
  as out of scope for this technical specification; only the video's *required
  content* (FR-009, SC-005) is in scope here.
- Specific LE PHY variant (1M/2M/Coded) is left to the planning phase to decide,
  since the source document does not mandate one and any conformant choice satisfies
  FR-002.
- The Bluetooth Toolbox (or equivalent MATLAB/Simulink toolbox named in the source
  document) is a fixed constraint from the assignment, not a discretionary
  implementation choice, so it is treated as a project constraint rather than a
  scope ambiguity.
