# Makefile - Kria cross-compilation entry point (T055)
#
# Convenience wrapper around the CMake cross build used by
# scripts/deploy_kria.sh. Requires the gcc-aarch64-linux-gnu toolchain.
#
# Targets:
#   make -C fpga cross     # cross-compile the ARM software stack (FPGA_TARGET=ON)
#   make -C fpga clean     # remove the cross build directory

REPO_ROOT := $(abspath $(dir $(lastword $(MAKEFILE_LIST)))/..)
BUILD_DIR := $(REPO_ROOT)/build-kria-aarch64

.PHONY: cross clean

cross:
	cmake -S $(REPO_ROOT) -B $(BUILD_DIR) \
	    -DCMAKE_TOOLCHAIN_FILE=$(REPO_ROOT)/fpga/toolchain-aarch64.cmake \
	    -DFPGA_TARGET=ON \
	    -DBUILD_SYSTEMC_MODELS=OFF \
	    -DBUILD_SYSTEMC_INTEGRATION=OFF \
	    -DBUILD_BENCHMARKS=OFF \
	    -DBUILD_TESTS=OFF
	cmake --build $(BUILD_DIR) -j$(shell nproc)

clean:
	rm -rf $(BUILD_DIR)
