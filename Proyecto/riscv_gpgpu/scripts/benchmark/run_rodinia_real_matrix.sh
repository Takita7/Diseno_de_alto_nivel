#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(dirname "$(dirname "$SCRIPT_DIR")")"
BUILD_DIR="${BUILD_DIR:-$PROJECT_ROOT/build-all}"
BIN_PATH="${BIN_PATH:-$BUILD_DIR/bin/rodinia_real_benchmark}"
RESULTS_DIR="${RESULTS_DIR:-$PROJECT_ROOT/results/benchmarks/rodinia_real_matrix}"
SUMMARY_TSV="$RESULTS_DIR/summary.tsv"
SUMMARY_JSON="$RESULTS_DIR/summary.json"
JOBS="${JOBS:-1}"
MAX_CYCLES="${RODINIA_REAL_STRESS_MAX_CYCLES:-200000}"
NODES="${RODINIA_REAL_STRESS_NODES:-128}"
FANOUT="${RODINIA_REAL_STRESS_FANOUT:-}"
TIMEOUT_VALUE="${TIMEOUT_VALUE:-30m}"
FILTER="${FILTER:-RodiniaRealBenchmark.BfsFanoutStress}"
DEFAULT_CUS="${RODINIA_REAL_STRESS_CU_VALUES:-2 3 5 10}"
DEFAULT_THREADS="${RODINIA_REAL_STRESS_THREAD_VALUES:-128}"

mkdir -p "$RESULTS_DIR"
cat > "$SUMMARY_TSV" <<'EOF'
case	cus	threads	status	elapsed_ms	log
EOF

if [[ ! -x "$BIN_PATH" ]]; then
    echo "Benchmark binary not found or not executable: $BIN_PATH" >&2
    exit 1
fi

if [[ $# -eq 0 ]]; then
    combos=()
    read -r -a cu_values <<<"$DEFAULT_CUS"
    read -r -a thread_values <<<"$DEFAULT_THREADS"
    for cus in "${cu_values[@]}"; do
        for threads in "${thread_values[@]}"; do
            combos+=("$cus $threads")
        done
    done
else
    combos=()
    if (( $# % 2 != 0 )); then
        echo "Expected an even number of arguments: CU THREAD pairs" >&2
        exit 2
    fi
    while (( $# > 0 )); do
        combos+=("$1 $2")
        shift 2
    done
fi

echo "Running Rodinia real benchmark matrix"
echo "Binary: $BIN_PATH"
echo "Results: $RESULTS_DIR"
echo "Filter: $FILTER"
echo "Max cycles: $MAX_CYCLES"
echo "Nodes: $NODES"
echo "Fanout: ${FANOUT:-auto}"
echo "CU values: $DEFAULT_CUS"
echo "Thread values: $DEFAULT_THREADS"
echo

passes=0
failures=0

for combo in "${combos[@]}"; do
    read -r cus threads <<<"$combo"
    if [[ -z "${cus:-}" || -z "${threads:-}" ]]; then
        echo "Skipping malformed combo: '$combo'" >&2
        continue
    fi

    name="cus${cus}_threads${threads}"
    log_file="$RESULTS_DIR/${name}.log"

    echo "== $name =="
    start_ns="$(date +%s%N)"
    if env \
        RODINIA_REAL_STRESS=1 \
        RODINIA_REAL_STRESS_CUS="$cus" \
        RODINIA_REAL_STRESS_THREADS="$threads" \
        RODINIA_REAL_STRESS_MAX_CYCLES="$MAX_CYCLES" \
        RODINIA_REAL_STRESS_NODES="$NODES" \
        RODINIA_REAL_STRESS_FANOUT="$FANOUT" \
        "$BIN_PATH" --gtest_filter="$FILTER" \
        2>&1 | tee "$log_file"; then
        end_ns="$(date +%s%N)"
        elapsed_ms=$(( (end_ns - start_ns) / 1000000 ))
        printf 'PASS %s elapsed=%dms\n' "$name" "$elapsed_ms"
        printf '%s\t%s\t%s\t%s\t%d\t%s\n' "$name" "$cus" "$threads" "PASS" "$elapsed_ms" "$log_file" >> "$SUMMARY_TSV"
        passes=$((passes + 1))
    else
        end_ns="$(date +%s%N)"
        elapsed_ms=$(( (end_ns - start_ns) / 1000000 ))
        printf 'FAIL %s elapsed=%dms\n' "$name" "$elapsed_ms"
        printf '%s\t%s\t%s\t%s\t%d\t%s\n' "$name" "$cus" "$threads" "FAIL" "$elapsed_ms" "$log_file" >> "$SUMMARY_TSV"
        failures=$((failures + 1))
    fi
    echo
done

if command -v python3 >/dev/null 2>&1; then
    python3 "$PROJECT_ROOT/scripts/benchmark/analyze_results.py" \
        --matrix-dir "$RESULTS_DIR" \
        --output "$SUMMARY_JSON"
fi

echo "Summary: PASS=$passes FAIL=$failures"

if (( failures > 0 )); then
    exit 1
fi