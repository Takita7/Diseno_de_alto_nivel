# =============================================================================
# Makefile – RISC-V GPGPU SystemC Simulation
# Run from: riscv_gpgpu/models/systemc/test/
#
# Targets:
#   make all        Build both regression and benchmark binaries (default)
#   make regression Build and run full regression suite (Phases 0-9)
#   make benchmark  Build and run benchmark suite (paper benchmarks)
#   make rebuild    Clean and build both
#   make clean      Remove all build artifacts
#   make info       Show build configuration
# =============================================================================

# ── Toolchain ─────────────────────────────────────────────────────────────────
CXX      := g++
CXXFLAGS := -std=c++17 -Wall -Wextra -g -DSC_ALLOW_DEPRECATED_IEEE_API
DEPFLAGS := -MMD -MP

REGRESSION_TARGET := riscv_gpgpu_regression
BENCHMARK_TARGET  := riscv_gpgpu_benchmark

# ── Paths ─────────────────────────────────────────────────────────────────────
SRC_DIR   := ../src
BUILD_DIR := build

SYSTEMC_HOME ?= /usr/local
INCLUDES     := -I$(SYSTEMC_HOME)/include -I$(SRC_DIR)
LIBDIR       := $(SYSTEMC_HOME)/lib-linux64
LIBS         := -L$(LIBDIR) -lsystemc -lpthread

# ── Shared module sources (compiled once, linked into both binaries) ───────────
MODULE_SRCS := \
	$(SRC_DIR)/top/top.cpp                          \
	$(SRC_DIR)/scheduler/warp_scheduler.cpp         \
	$(SRC_DIR)/simt_controller/simt_controller.cpp  \
	$(SRC_DIR)/memory/memory_hierarchy.cpp          \
	$(SRC_DIR)/compute_unit/compute_unit.cpp        \
	$(SRC_DIR)/system_top/system_top.cpp

MODULE_OBJS := $(addprefix $(BUILD_DIR)/,$(notdir $(MODULE_SRCS:.cpp=.o)))
MODULE_DEPS := $(MODULE_OBJS:.o=.d)

# ── Test-specific sources ─────────────────────────────────────────────────────
REGRESSION_OBJ := $(BUILD_DIR)/regression_test.o
BENCHMARK_OBJ  := $(BUILD_DIR)/benchmark_test.o

ALL_DEPS := $(MODULE_DEPS) \
            $(REGRESSION_OBJ:.o=.d) \
            $(BENCHMARK_OBJ:.o=.d)

# ── VPATH: where make searches for source files by basename ───────────────────
VPATH := \
	$(SRC_DIR)/top              \
	$(SRC_DIR)/scheduler        \
	$(SRC_DIR)/simt_controller  \
	$(SRC_DIR)/memory           \
	$(SRC_DIR)/compute_unit     \
	$(SRC_DIR)/system_top       \
	.

# ── Targets ───────────────────────────────────────────────────────────────────
.PHONY: all dirs regression benchmark rebuild clean info help

all: dirs $(REGRESSION_TARGET) $(BENCHMARK_TARGET)
	@echo "\n✓ Build complete: ./$(REGRESSION_TARGET)  ./$(BENCHMARK_TARGET)"

dirs:
	@mkdir -p $(BUILD_DIR)

# Regression binary: shared modules + regression_test.o
$(REGRESSION_TARGET): $(MODULE_OBJS) $(REGRESSION_OBJ)
	@echo "\n[Link] $@"
	$(CXX) $(CXXFLAGS) -o $@ $^ $(LIBS)
	@echo "[OK] $(REGRESSION_TARGET) ready"

# Benchmark binary: shared modules + benchmark_test.o
$(BENCHMARK_TARGET): $(MODULE_OBJS) $(BENCHMARK_OBJ)
	@echo "\n[Link] $@"
	$(CXX) $(CXXFLAGS) -o $@ $^ $(LIBS)
	@echo "[OK] $(BENCHMARK_TARGET) ready"

# Pattern rule for all .cpp files (modules + test files)
$(BUILD_DIR)/%.o: %.cpp | dirs
	@echo "[CC]  $<"
	$(CXX) $(CXXFLAGS) $(DEPFLAGS) $(INCLUDES) -c -o $@ $<

-include $(ALL_DEPS)

regression: dirs $(REGRESSION_TARGET)
	@echo "[Run] Regression suite..."
	./$(REGRESSION_TARGET)

benchmark: dirs $(BENCHMARK_TARGET)
	@echo "[Run] Benchmark suite..."
	./$(BENCHMARK_TARGET)

rebuild: clean all

clean:
	@rm -rf $(BUILD_DIR) $(REGRESSION_TARGET) $(BENCHMARK_TARGET)
	@echo "✓ Clean complete"

info:
	@echo "╔═══════════════════════════════════════════════════════╗"
	@echo "║  RISC-V GPGPU – Build Info                            ║"
	@echo "╠═══════════════════════════════════════════════════════╣"
	@echo "║  SYSTEMC_HOME    = $(SYSTEMC_HOME)"
	@echo "║  CXX             = $(CXX)"
	@echo "║  Module sources  = $(words $(MODULE_SRCS)) files"
	@echo "║  Regression      = $(REGRESSION_TARGET)"
	@echo "║  Benchmark       = $(BENCHMARK_TARGET)"
	@echo "╠═══════════════════════════════════════════════════════╣"
	@ls -lh $(LIBDIR)/libsystemc* 2>/dev/null || echo "  [!] libsystemc not found in $(LIBDIR)"
	@echo "╚═══════════════════════════════════════════════════════╝"

help:
	@echo "╔═══════════════════════════════════════════════════════╗"
	@echo "║  Available targets                                    ║"
	@echo "╠═══════════════════════════════════════════════════════╣"
	@echo "║  make all        Build both binaries (default)        ║"
	@echo "║  make regression Build + run full regression suite    ║"
	@echo "║  make benchmark  Build + run benchmark suite          ║"
	@echo "║  make rebuild    Clean and build both                 ║"
	@echo "║  make clean      Remove build artifacts               ║"
	@echo "║  make info       Show build configuration             ║"
	@echo "║  make help       This message                         ║"
	@echo "╚═══════════════════════════════════════════════════════╝"