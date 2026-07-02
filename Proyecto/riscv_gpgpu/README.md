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
- Google Test (`libgtest-dev`, `libgmock-dev`) for C++ unit tests
- LLVM/Clang tools (`clang`, `lld`, `llvm-dev`, `llvm-tools`) for compiler/runtime development
- OpenCL ICD headers (`ocl-icd-opencl-dev`, `opencl-c-headers`, `opencl-clhpp-headers`) for runtime integration
- Host dev packages: `libhwloc-dev`, `libnuma-dev`, `libelf-dev`, `libfdt-dev`, `libxml2-dev`, `libssl-dev`

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

Benchmark results are written to `results/benchmarks/summary.json` by default.

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

Contributing — Software development setup

 - **Create a local Python virtualenv:** preferred to avoid system package conflicts (PEP 668).

	 ```bash
	 # From project root (recommended for local contribs)
	 python3 -m venv .venv
	 source .venv/bin/activate
	 python -m pip install --upgrade pip
	 python -m pip install pyyaml jinja2 lit numpy
	 ```

 - **Use the project helper script to create a system-wide venv and install host packages** (requires sudo):

	 ```bash
	 # Run as root to install system packages and create a shared venv under /opt
	 sudo ./scripts/setup-software-dev.sh
	 # Activate the shared venv
	 source /opt/riscv-gpgpu-venv/bin/activate
	 ```

 - **Cloning recommended source repos (manual):**

	 ```bash
	 mkdir -p /opt/riscv-src && cd /opt/riscv-src
	 git clone https://github.com/llvm/llvm-project.git
	 git clone https://github.com/riscv/riscv-gnu-toolchain.git
	 git clone https://github.com/pocl/pocl.git
	 ```

 - **Build notes:** prefer `ninja` and an out-of-tree build for large projects (LLVM). Example:

	 ```bash
	 mkdir -p /opt/riscv-src/llvm-project/build && cd /opt/riscv-src/llvm-project/build
	 cmake -G Ninja ../llvm -DLLVM_ENABLE_PROJECTS="clang;lld;compiler-rt" -DCMAKE_BUILD_TYPE=Release -DLLVM_TARGETS_TO_BUILD=RISCV
	 ninja -j$(nproc)
	 ```

 - **If pip fails with "externally-managed-environment":** use a virtualenv as shown above, or install OS-packaged python libs via apt (not recommended for development).


