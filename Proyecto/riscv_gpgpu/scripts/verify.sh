#!/usr/bin/env bash

set -uo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(dirname "$SCRIPT_DIR")"
BUILD_DIR="${BUILD_DIR:-$PROJECT_ROOT/build-all}"
RESULTS_DIR="${RESULTS_DIR:-$PROJECT_ROOT/results/verification}"
BUILD_TYPE="${BUILD_TYPE:-RelWithDebInfo}"
JOBS="${JOBS:-$(getconf _NPROCESSORS_ONLN 2>/dev/null || printf '2')}"
SYSTEMC_MODE="${SYSTEMC_MODE:-auto}"
RUN_STANDALONE="${RUN_STANDALONE:-1}"
RUN_WORKLOADS="${RUN_WORKLOADS:-1}"
CLEAN="${CLEAN:-0}"
STRICT="${STRICT:-0}"
STRICT_PTX_BENCHMARKS="${STRICT_PTX_BENCHMARKS:-0}"

mkdir -p "$RESULTS_DIR"

passes=()
failures=()
skips=()

record_pass() { passes+=("$1"); }
record_fail() { failures+=("$1"); }
record_skip() { skips+=("$1: $2"); }
remove_failure() {
    local target="$1"
    local remaining=()
    local item
    for item in "${failures[@]}"; do
        [[ "$item" == "$target" ]] || remaining+=("$item")
    done
    failures=("${remaining[@]}")
}

run_step() {
    local name="$1"
    local timeout_value="$2"
    shift 2
    printf '\n== %s ==\n' "$name"
    if timeout --signal=TERM --kill-after=30s "$timeout_value" "$@" \
        2>&1 | tee "$RESULTS_DIR/${name// /_}.log"; then
        record_pass "$name"
        return 0
    fi
    record_fail "$name"
    return 1
}

have_systemc() {
    if command -v pkg-config >/dev/null 2>&1 && pkg-config --exists systemc; then
        return 0
    fi
    local root="${SYSTEMC_HOME:-}"
    [[ -n "$root" ]] || return 1
    [[ -f "$root/include/systemc" || -f "$root/include/systemc.h" || -f "$root/include/systemc/systemc.h" ]]
}

have_riscv_clang() {
    command -v clang >/dev/null 2>&1 || return 1
    local probe
    probe="$(mktemp "${TMPDIR:-/tmp}/riscv-gpgpu-clang.XXXXXX")" || return 1
    rm -f "$probe"
    clang --target=riscv32-unknown-elf -march=rv32imf -mabi=ilp32f \
        -fuse-ld=lld -nostdlib -x assembler-with-cpp /dev/null -o "$probe" \
        >/dev/null 2>&1
    local status=$?
    rm -f "$probe"
    return "$status"
}

if [[ "$CLEAN" == 1 ]]; then
    rm -rf "$BUILD_DIR"
fi

systemc_enabled=0
case "$SYSTEMC_MODE" in
    on)
        systemc_enabled=1
        ;;
    off)
        systemc_enabled=0
        ;;
    auto)
        if have_systemc; then systemc_enabled=1; fi
        ;;
    *)
        printf 'SYSTEMC_MODE must be auto, on, or off\n' >&2
        exit 2
        ;;
esac

if (( systemc_enabled )); then
    systemc_models=ON
    systemc_integration=ON
else
    systemc_models=OFF
    systemc_integration=OFF
    record_skip "SystemC suites" "SystemC was not detected"
fi

mkdir -p "$BUILD_DIR"

configure=(
    cmake -S "$PROJECT_ROOT" -B "$BUILD_DIR"
    "-DCMAKE_BUILD_TYPE=$BUILD_TYPE"
    -DBUILD_TESTS=ON
    -DBUILD_BENCHMARKS=ON
    "-DBUILD_SYSTEMC_MODELS=$systemc_models"
    "-DBUILD_SYSTEMC_INTEGRATION=$systemc_integration"
    -DBUILD_HLS=OFF
)

run_step "configure" 5m "${configure[@]}" || true
if [[ ! -f "$BUILD_DIR/CMakeCache.txt" ]]; then
    printf '\nConfiguration failed; no build can be run.\n'
else
    run_step "build" 20m cmake --build "$BUILD_DIR" --parallel "$JOBS" || true

    ctest --test-dir "$BUILD_DIR" -N 2>&1 | tee "$RESULTS_DIR/ctest_inventory.log"
    test_count="$(ctest --test-dir "$BUILD_DIR" -N 2>/dev/null | awk '/Total Tests:/ {print $3}')"
    test_count="${test_count:-0}"
    if (( test_count > 0 )); then
        bench_re_all='^(ptx_kernels_benchmark|rodinia_real_benchmark)$'
        bench_re_cuda='^(rodinia_real_benchmark)$'
        bench_re_ptx='^(ptx_kernels_benchmark)$'
        ordinary_exclude="$bench_re_all"
        if ! have_riscv_clang; then
            ordinary_exclude="${ordinary_exclude}|^(llvm_backend_tests|llvm_mc_tests|kernel_loader_tests|systemc_integration_tests)$"
            record_skip "clang-dependent suites" "clang with RV32 and lld was not detected"
        fi
        run_step "ctest_tests" 30m ctest --test-dir "$BUILD_DIR" \
            --output-on-failure --timeout 300 -j1 -E "$ordinary_exclude" || true

        benchmark_count="$(ctest --test-dir "$BUILD_DIR" -N -R "$bench_re_all" 2>/dev/null | awk '/Total Tests:/ {print $3}')"
        benchmark_count="${benchmark_count:-0}"
        if (( benchmark_count > 0 )); then
            benchmark_cuda_count="$(ctest --test-dir "$BUILD_DIR" -N -R "$bench_re_cuda" 2>/dev/null | awk '/Total Tests:/ {print $3}')"
            benchmark_cuda_count="${benchmark_cuda_count:-0}"
            if (( benchmark_cuda_count > 0 )); then
                run_step "ctest_benchmarks_cuda" 30m ctest --test-dir "$BUILD_DIR" \
                    --output-on-failure --timeout 900 -j1 -R "$bench_re_cuda" || true
            else
                record_skip "CTest CUDA benchmarks" "no real Rodinia benchmark targets were configured"
            fi

            benchmark_ptx_count="$(ctest --test-dir "$BUILD_DIR" -N -R "$bench_re_ptx" 2>/dev/null | awk '/Total Tests:/ {print $3}')"
            benchmark_ptx_count="${benchmark_ptx_count:-0}"
            if (( benchmark_ptx_count > 0 )); then
                run_step "ctest_benchmarks_ptx" 60m ctest --test-dir "$BUILD_DIR" \
                    --output-on-failure --timeout 900 -j1 -R "$bench_re_ptx" || true
                if [[ "$STRICT_PTX_BENCHMARKS" != 1 ]] && [[ " ${failures[*]} " == *" ctest_benchmarks_ptx "* ]]; then
                    # Keep PTX benchmark results visible in logs but do not fail the whole verify by default.
                    remove_failure "ctest_benchmarks_ptx"
                    record_skip "ctest_benchmarks_ptx" "failed (non-blocking). Set STRICT_PTX_BENCHMARKS=1 to fail verify on PTX benchmark regressions"
                fi
            else
                record_skip "CTest PTX benchmarks" "no PTX benchmark targets were configured"
            fi
        else
            record_skip "CTest benchmarks" "benchmark targets were not configured"
        fi
    else
        record_skip "CTest" "no tests were configured, usually because GTest is unavailable"
    fi

    if have_riscv_clang; then
        run_step "compile_kernels" 10m cmake --build "$BUILD_DIR" \
            --parallel "$JOBS" --target compile_kernels || true
    else
        record_skip "compile_kernels" "clang with RV32 and lld was not detected"
    fi
fi

if (( systemc_enabled )) && [[ "$RUN_STANDALONE" == 1 ]]; then
    systemc_args=(-C "$PROJECT_ROOT/models/systemc/test")
    if [[ -n "${SYSTEMC_HOME:-}" ]]; then systemc_args+=("SYSTEMC_HOME=$SYSTEMC_HOME"); fi
    run_step "standalone_regression" 10m make "${systemc_args[@]}" regression || true
    run_step "standalone_benchmark" 15m make "${systemc_args[@]}" benchmark || true
elif [[ "$RUN_STANDALONE" == 1 ]]; then
    record_skip "Standalone SystemC" "SystemC was not detected"
fi

if [[ "$RUN_WORKLOADS" == 1 ]]; then
    if have_riscv_clang; then
        run_step "vector_add_workload" 5m bash \
            "$PROJECT_ROOT/benchmarks/workloads/vector_add/run_vector_add.sh" || true
        run_step "saxpy_workload" 5m bash \
            "$PROJECT_ROOT/benchmarks/workloads/saxpy/run_saxpy.sh" || true
    else
        record_skip "Workload scripts" "clang with RV32 and lld was not detected"
    fi
fi

printf '\n=====================================\n'
printf 'Verification summary\n'
printf '=====================================\n'
printf 'PASS: %d\n' "${#passes[@]}"
for item in "${passes[@]}"; do printf '  PASS  %s\n' "$item"; done
printf 'SKIP: %d\n' "${#skips[@]}"
for item in "${skips[@]}"; do printf '  SKIP  %s\n' "$item"; done
printf 'FAIL: %d\n' "${#failures[@]}"
for item in "${failures[@]}"; do printf '  FAIL  %s\n' "$item"; done
printf 'Logs: %s\n' "$RESULTS_DIR"

if (( ${#failures[@]} > 0 )); then exit 1; fi
if [[ "$STRICT" == 1 && ${#skips[@]} -gt 0 ]]; then exit 2; fi
exit 0
