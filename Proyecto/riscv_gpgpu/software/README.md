# Software

This directory contains host-side software components that are owned by the
`software/` subtree.

Scope of this README:
- software modules in this directory
- build/test entrypoints for those modules

Out of scope:
- runtime internals in `runtime/`
- userspace driver internals in `driver/`
- SystemC model internals in `models/systemc/`

## Directory Scope

| Path | Scope |
|---|---|
| `software/common/` | Shared software configuration/types |
| `software/host_api/` | Public host API (`gpgpuMalloc`, `gpgpuMemcpy`, `gpgpuLaunchKernel`, `gpgpuSynchronize`) |
| `software/kernel_loader/` | Kernel bundle and symbol resolution utilities |
| `software/llvm/` | Compiler/backend scaffolding for PTX/RISC-V flow |

## Integration Boundaries

- `software/host_api` calls into runtime/driver APIs.
- Under `FPGA_TARGET`, launch/sync paths use the FPGA register/ELF flow.
- Without `FPGA_TARGET`, simulation path remains available.

For implementation details of runtime and driver layers, see repository root
structure and those source directories.

## Build and Test

```bash
cmake -S . -B build-all -DBUILD_TESTS=ON
cmake --build build-all --target host_api_lib test_host_api -j$(nproc)
ctest --test-dir build-all -R "host_api|compiler"
```

## Notes For Contributors

- Keep API semantics stable in `software/host_api/host_api.h`.
- Keep hardware-specific behavior behind compile-time guards.
- Record cross-cutting behavior changes in `docs/software/interfaces.md`.
