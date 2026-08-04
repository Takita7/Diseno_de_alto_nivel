# Reproducibility

This directory defines how to recreate builds, tests, and benchmark evidence.

Scope:
- pinned environment assumptions
- reproducible command sequences
- artifact checklist for result replication

## What To Keep Here

- host/toolchain prerequisites (versions and assumptions)
- exact build/test command sets used for evidence generation
- benchmark replay instructions and expected output locations
- release/reproducibility checklist references

## What Not To Keep Here

- architecture descriptions (`docs/architecture/`)
- requirement mapping tables (`docs/traceability/`)
- implementation detail walkthroughs already covered in component READMEs

## Related Artifacts

- verification outputs: `docs/verification/` and `results/`
- task ownership and phase status: `specs/001-open-riscv-gpgpu/tasks.md`
