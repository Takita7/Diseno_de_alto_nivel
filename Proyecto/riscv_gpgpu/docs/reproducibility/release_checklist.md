# Release Checklist

Phase 6 reproducibility and release checklist.

## Build and Test

- [ ] Configure project from clean workspace
- [ ] Build succeeds for default simulation profile
- [ ] CTest passes on the release build directory

## Benchmarks

- [ ] Rodinia matrix benchmark executed
- [ ] summary.tsv and summary.json generated
- [ ] docs/verification/benchmark_results.md updated

## FPGA/Kria Path

- [ ] FPGA target build configured with FPGA_TARGET=ON
- [ ] deploy_kria.sh arguments validated
- [ ] docs/verification/kria_results.md updated from hardware run

## Documentation

- [ ] Root and component READMEs aligned with directory scope
- [ ] docs/architecture and docs/software contracts updated if interfaces changed
- [ ] docs/traceability/traceability_matrix.md links requirements to evidence
- [ ] REPRODUCIBILITY.md updated for this release

## Artifact Snapshot

- [ ] Commit hash recorded
- [ ] Build/test commands recorded
- [ ] Benchmark and verification outputs archived
