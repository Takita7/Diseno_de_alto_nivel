# Traceability

This directory owns requirement-to-evidence mapping.

Scope:
- mapping requirements to implementation artifacts
- mapping requirements to test/verification evidence

## Primary Artifact

| File | Purpose |
|---|---|
| `traceability_matrix.md` | Living matrix from requirement IDs to code, tests, and evidence outputs |

## Process

1. Add or update requirement IDs in the matrix.
2. Link each requirement to implementation files and tests.
3. Link each requirement to evidence outputs in `docs/verification/` or `results/`.
4. Keep status synchronized with `specs/001-open-riscv-gpgpu/tasks.md`.

## Boundaries

- Do not duplicate architecture or API descriptions here.
- Use this directory only for trace links and verification evidence references.
