# RISC-V GPGPU

Open research platform for a RISC-V GPGPU flow spanning:
- SystemC functional simulation
- host software stack (launch/memory/runtime)
- HLS/RTL path for Kria FPGA deployment

This README is intentionally brief and only covers repository entry points.
Component details live in each component README.

## Scope Map

| Path | Owner scope |
|---|---|
| `models/systemc/` | Functional model and SystemC integration details |
| `software/` | Host-facing software APIs and compiler/runtime interfaces |
| `benchmarks/` | Benchmark binaries, workloads, and analysis artifacts |
| `hls/` | HLS implementation plan and constraints |
| `docs/architecture/` | Hardware/software architecture contracts |
| `docs/traceability/` | Requirement-to-evidence mapping |
| `docs/reproducibility/` | Reproducible environment and run procedures |
| `specs/001-open-riscv-gpgpu/` | Spec, plan, and task checklist (source of truth) |

## Quick Start

```bash
cmake -S . -B build-all -DBUILD_TESTS=ON -DBUILD_SYSTEMC_MODELS=ON
cmake --build build-all -j$(nproc)
ctest --test-dir build-all --output-on-failure
```

Optional integration path:

```bash
cmake -S . -B build-all -DBUILD_SYSTEMC_INTEGRATION=ON -DBUILD_TESTS=ON
cmake --build build-all -j$(nproc)
ctest --test-dir build-all --output-on-failure
```

## Common Commands

```bash
# Full verification harness
bash scripts/verify.sh

# Rodinia matrix benchmark
bash scripts/benchmark/run_rodinia_real_matrix.sh

# Kria deployment helper (Phase 7)
bash scripts/deploy_kria.sh --help
```

## Current Stability Snapshot

- Core build and test flow is green in local CI-style runs.
- Phase 7 Kria deployment path has code and tests in place.
- Hardware execution on the physical board is still a separate step.

## Where To Update Status

- Task progress: `specs/001-open-riscv-gpgpu/tasks.md`
- Verification evidence: `docs/verification/`
- Traceability matrix: `docs/traceability/traceability_matrix.md`
