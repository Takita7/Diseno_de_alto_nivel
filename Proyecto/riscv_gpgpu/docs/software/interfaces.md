# Compiler / Runtime Interface Contract (US3)

This document captures the minimal interface contract between the compiler-generated kernel binaries and the host runtime/driver for the `riscv-gpgpu` project.

## Purpose

- Define kernel binary layout and metadata
- Define kernel arguments and memory model mapping
- Define host-to-driver verbs (upload, launch, query, teardown)

## Status

Draft — work in progress. Fill in ABI fields and example kernel manifest.

## TODO

- Add kernel binary manifest example (JSON)
- Document memory regions and addressing rules
- Specify kernel launch semantics (workgroup/grid mapping)
- Define error codes and runtime status values

