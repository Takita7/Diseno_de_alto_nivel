RISCV GPGPU — Build & Run
==========================

Overview

This repository contains an FPGA-oriented, SystemC-based baseline of an open RISC-V GPGPU research platform.
The following sections show minimal commands to set up the environment, build the SystemC model, run simulations,
execute tests, and run the verification/benchmark harnesses.

Prerequisites

- Linux (Ubuntu 22.04+ recommended)
- CMake 3.24+
- A C++17-capable compiler (GCC/Clang)
- SystemC development headers and libraries (SystemC 2.3.x)
- Python 3.10+ (for scripts)
- Optional: Google Test for C++ unit tests

Quick setup

Load the project environment (optional helper):

```bash
# From project root
source scripts/setup-env.sh
```

Build (SystemC models + tests)

```bash
mkdir -p build
cd build
cmake .. -DBUILD_SYSTEMC_MODELS=ON -DBUILD_TESTS=ON
make -j$(nproc)
```

Run the SystemC simulation

Option A — via provided runner (recommended):

```bash
# from project root
./scripts/run_systemc_sim.sh --scenario baseline
```

Option B — run the built executable directly:

```bash
# from project root
./build/bin/systemc_simulation
```

Run tests

If `GTest` was found during configuration, run CTest from the build directory:

```bash
cd build
ctest --output-on-failure
```

Run verification harness

```bash
# from project root
./scripts/verify.sh
```

Run benchmarks (placeholder harness)

```bash
# from project root
./scripts/benchmark.sh
```

Simulation scenarios (environment variables)

The `scripts/scenarios/` directory contains example scenario scripts:

- `baseline` — default baseline configuration
- `high_throughput` — larger CU/warp counts
- `power_efficient` — reduced resources and frequency

Use the `--scenario` option with `run_systemc_sim.sh` to pick one, e.g.:

```bash
./scripts/run_systemc_sim.sh --scenario high_throughput
```

Notes & troubleshooting

- If `cmake` fails to find SystemC, set `SYSTEMC_HOME` to the SystemC install root or install SystemC development packages.
- If `GTest` is not available, tests will be skipped but the SystemC simulation will still build and run.
- The `scripts/setup-env.sh` attempts to detect and set `SYSTEMC_HOME` and `LLVM_HOME` where possible.

Contributing

Please follow the project's contribution guidelines and keep changes traceable to `docs/traceability/`.

