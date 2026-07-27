# Compiler / Runtime Interface Contract (US3)

This document captures the minimal interface contract between the compiler-generated kernel binaries and the host runtime/driver for the `riscv-gpgpu` project.

## Purpose

- Define kernel binary layout and metadata
- Define kernel arguments and memory model mapping
- Define host-to-driver verbs (upload, launch, query, teardown)
- Define kernel bundle/package format for upload to the device

## Status

Draft — work in progress. The current implementation uses LLVM/Clang to emit a RISC-V ELF kernel binary and a JSON manifest to describe the bundle.

## Kernel Bundle Format

A kernel bundle is represented as a manifest JSON file paired with one or more binary artifacts. The manifest provides the runtime with the information needed for upload and execution.

Example manifest:

```json
{
  "kernel_name": "vector_add",
  "binary_path": "vector_add.riscv.elf",
  "binary_size": 4096,
  "entry_point": "kernel",
  "workgroup": {
    "x": 16,
    "y": 1,
    "z": 1
  },
  "metadata": {
    "shared_mem_bytes": 1024,
    "registers_per_thread": 32,
    "argument_count": 2
  }
}
```

## Kernel Binary Layout

- The compiler emits a RISC-V ELF object containing the kernel code and any static data.
- The runtime expects a standalone ELF binary or shared object with a well-known entry point.
- The kernel binary is packaged using the kernel loader, which tracks the binary file path and metadata in the bundle manifest.

## Memory and Argument Semantics

- Kernel arguments are passed through the runtime/driver interface as 64-bit values.
- The runtime maps these arguments into the device kernel argument space.
- Global and shared memory regions are described by the manifest and/or runtime configuration.

## Host-to-Driver Verbs

- `uploadKernel(bundle_manifest_path)`
  - Parses the manifest and transfers the kernel binary to the device.
- `launchKernel(kernel_name, workgroup_x, workgroup_y, workgroup_z, args)`
  - Starts kernel execution with the requested grid and workgroup dimensions.
- `queryKernelStatus(kernel_name)`
  - Returns the current kernel status (`PENDING`, `RUNNING`, `COMPLETED`, `FAILED`).
- `unloadKernel(kernel_name)`
  - Releases any device resources associated with the kernel.

## Notes

- The current implementation is a software prototype; the final hardware path may extend the manifest to include ISA-specific configuration and memory banking details.
- This document should be updated once the runtime and driver binary protocol are stabilized.

