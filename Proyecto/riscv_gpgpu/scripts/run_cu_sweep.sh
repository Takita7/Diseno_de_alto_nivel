#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"

FROM_CU="${FROM_CU:-2}"
TO_CU="${TO_CU:-16}"
STEP_CU="${STEP_CU:-2}"
JOBS="${JOBS:-$(getconf _NPROCESSORS_ONLN 2>/dev/null || printf '8')}"

usage() {
    cat <<'EOF'
Usage: scripts/run_cu_sweep.sh [--from N] [--to N] [--step N] [--jobs N]

Examples:
  scripts/run_cu_sweep.sh --from 2 --to 8 --step 2
  FROM_CU=2 TO_CU=16 STEP_CU=2 JOBS=32 scripts/run_cu_sweep.sh
EOF
}

while [[ $# -gt 0 ]]; do
    case "$1" in
        --from)
            FROM_CU="$2"
            shift 2
            ;;
        --to)
            TO_CU="$2"
            shift 2
            ;;
        --step)
            STEP_CU="$2"
            shift 2
            ;;
        --jobs)
            JOBS="$2"
            shift 2
            ;;
        -h|--help)
            usage
            exit 0
            ;;
        *)
            echo "Unknown argument: $1" >&2
            usage >&2
            exit 2
            ;;
    esac
done

if ! [[ "$FROM_CU" =~ ^[0-9]+$ ]] || ! [[ "$TO_CU" =~ ^[0-9]+$ ]] || ! [[ "$STEP_CU" =~ ^[0-9]+$ ]]; then
    echo "CU values must be integers" >&2
    exit 2
fi

if (( FROM_CU < 1 || TO_CU < 1 || STEP_CU < 1 )); then
    echo "CU values must be >= 1" >&2
    exit 2
fi

if (( FROM_CU > TO_CU )); then
    echo "--from must be <= --to" >&2
    exit 2
fi

SWEEP_ROOT="$PROJECT_ROOT/results/cu_sweep"
mkdir -p "$SWEEP_ROOT"

is_fresh_file() {
    local file="$1"
    local start_ts="$2"
    [[ -f "$file" ]] || return 1
    local mtime
    mtime="$(stat -c %Y "$file" 2>/dev/null || printf '0')"
    (( mtime >= start_ts ))
}

archive_run_artifacts() {
    local run_dir="$1"
    local cu="$2"
    local archive_root="$run_dir/complete_run"
    local util_rpt="$3"
    local timing_rpt="$4"
    local drc_rpt="$5"
    local bit_file="$6"

    rm -rf "$archive_root"
    mkdir -p "$archive_root/reports" "$archive_root/build"

    {
        echo "cu=$cu"
        echo "timestamp=$(date -u +%Y-%m-%dT%H:%M:%SZ)"
        echo "project_root=$PROJECT_ROOT"
    } > "$archive_root/metadata.txt"

    cp "$util_rpt" "$archive_root/reports/implementation_utilization.rpt" 2>/dev/null || true
    cp "$timing_rpt" "$archive_root/reports/implementation_timing.rpt" 2>/dev/null || true
    cp "$drc_rpt" "$archive_root/reports/implementation_drc.rpt" 2>/dev/null || true
    cp "$bit_file" "$archive_root/reports/${REPORT_PREFIX}_gpgpu_system_wrapper.bit" 2>/dev/null || true
    cp "$run_dir/hls.log" "$archive_root/hls.log" 2>/dev/null || true
    cp "$run_dir/vivado.log" "$archive_root/vivado.log" 2>/dev/null || true
    cp "$run_dir/hls.failed" "$archive_root/hls.failed" 2>/dev/null || true
    cp "$run_dir/vivado.failed" "$archive_root/vivado.failed" 2>/dev/null || true
    cp "$PROJECT_ROOT/hls/src/common/hls_config.h" "$archive_root/hls_config.h" 2>/dev/null || true

    if [ -d "$PROJECT_ROOT/build/vivado_kv260" ]; then
        cp -a "$PROJECT_ROOT/build/vivado_kv260" "$archive_root/build/"
    fi
    if [ -d "$PROJECT_ROOT/build/ip_export" ]; then
        cp -a "$PROJECT_ROOT/build/ip_export" "$archive_root/build/"
    fi
    if [ -d "$PROJECT_ROOT/build/vivado_kv260_runs" ]; then
        cp -a "$PROJECT_ROOT/build/vivado_kv260_runs" "$archive_root/build/"
    fi
}

for cu in $(seq "$FROM_CU" "$STEP_CU" "$TO_CU"); do
    echo "============================================================"
    echo "Running CU sweep for NUM_CUS=$cu"
    echo "============================================================"

    RUN_DIR="$SWEEP_ROOT/num_cus_${cu}"
    REPORT_PREFIX="num_cus_${cu}"
    mkdir -p "$RUN_DIR"
    rm -f "$RUN_DIR/hls.failed" "$RUN_DIR/vivado.failed"

    python3 - "$PROJECT_ROOT/hls/src/common/hls_config.h" "$cu" <<'PY'
import sys, re
from pathlib import Path
path = Path(sys.argv[1])
cu = sys.argv[2]
text = path.read_text()
# Update the RISCV_GPGPU_NUM_CUS macro (used by the hierarchical DATAFLOW guard)
if '#define RISCV_GPGPU_NUM_CUS' in text:
    text = re.sub(r'#define RISCV_GPGPU_NUM_CUS\s+\d+', f'#define RISCV_GPGPU_NUM_CUS {cu}', text)
elif 'constexpr int NUM_CUS' in text:
    text = re.sub(r'constexpr\s+int\s+NUM_CUS\s*=\s*\d+;', f'constexpr int NUM_CUS                    = {cu};', text)
else:
    raise SystemExit('NUM_CUS definition not found')
path.write_text(text)
PY

    echo "Updated HLS config to NUM_CUS=$cu"

    if ! command -v vitis-run >/dev/null 2>&1; then
        echo "vitis-run not found; source your Vitis environment first" >&2
        exit 1
    fi
    if ! command -v vivado >/dev/null 2>&1; then
        echo "vivado not found; source your Vivado environment first" >&2
        exit 1
    fi

    if ! vitis-run --mode hls --tcl "$PROJECT_ROOT/tests/fpga/export_gpgpu_ip.tcl" > "$RUN_DIR/hls.log" 2>&1; then
        echo "HLS failed for NUM_CUS=$cu" | tee "$RUN_DIR/hls.failed"
        continue
    fi

    run_start_ts="$(date +%s)"
    if ! vivado -mode batch -source "$PROJECT_ROOT/fpga/scripts/build_all.tcl" -tclargs "$JOBS" > "$RUN_DIR/vivado.log" 2>&1; then
        echo "Vivado build failed for NUM_CUS=$cu" | tee "$RUN_DIR/vivado.failed"
        continue
    fi

    UTIL_RPT=""
    TIMING_RPT=""
    DRC_RPT=""
    BIT_FILE=""

    for candidate in \
        "$PROJECT_ROOT/build/vivado_kv260/implementation_utilization.rpt" \
        "$PROJECT_ROOT/build/vivado_kv260_runs/post_run_num_cus_${cu}_"*/implementation_utilization.rpt \
        "$PROJECT_ROOT/build/vivado_kv260_runs/pre_run_num_cus_${cu}_"*/implementation_utilization.rpt; do
        if [ -f "$candidate" ]; then
            UTIL_RPT="$candidate"
            break
        fi
    done
    for candidate in \
        "$PROJECT_ROOT/build/vivado_kv260/implementation_timing.rpt" \
        "$PROJECT_ROOT/build/vivado_kv260_runs/post_run_num_cus_${cu}_"*/implementation_timing.rpt \
        "$PROJECT_ROOT/build/vivado_kv260_runs/pre_run_num_cus_${cu}_"*/implementation_timing.rpt; do
        if [ -f "$candidate" ]; then
            TIMING_RPT="$candidate"
            break
        fi
    done
    for candidate in \
        "$PROJECT_ROOT/build/vivado_kv260/implementation_drc.rpt" \
        "$PROJECT_ROOT/build/vivado_kv260_runs/post_run_num_cus_${cu}_"*/implementation_drc.rpt \
        "$PROJECT_ROOT/build/vivado_kv260_runs/pre_run_num_cus_${cu}_"*/implementation_drc.rpt; do
        if [ -f "$candidate" ]; then
            DRC_RPT="$candidate"
            break
        fi
    done
    for candidate in \
        "$PROJECT_ROOT/build/vivado_kv260/riscv_gpgpu_kv260.runs/impl_1/gpgpu_system_wrapper.bit" \
        "$PROJECT_ROOT/build/vivado_kv260_runs/post_run_num_cus_${cu}_"*/gpgpu_system_wrapper.bit \
        "$PROJECT_ROOT/build/vivado_kv260_runs/pre_run_num_cus_${cu}_"*/gpgpu_system_wrapper.bit; do
        if [ -f "$candidate" ]; then
            BIT_FILE="$candidate"
            break
        fi
    done

    if [ -z "$UTIL_RPT" ] || [ -z "$TIMING_RPT" ] || [ -z "$DRC_RPT" ] || [ -z "$BIT_FILE" ]; then
        echo "Vivado build failed for NUM_CUS=$cu (missing impl outputs)" | tee "$RUN_DIR/vivado.failed"
        continue
    fi

    if ! is_fresh_file "$UTIL_RPT" "$run_start_ts" || \
       ! is_fresh_file "$TIMING_RPT" "$run_start_ts" || \
       ! is_fresh_file "$DRC_RPT" "$run_start_ts" || \
       ! is_fresh_file "$BIT_FILE" "$run_start_ts"; then
        echo "Vivado build failed for NUM_CUS=$cu (missing fresh impl outputs)" | tee "$RUN_DIR/vivado.failed"
        continue
    fi

    cp "$UTIL_RPT" "$RUN_DIR/${REPORT_PREFIX}_implementation_utilization.rpt"
    cp "$TIMING_RPT" "$RUN_DIR/${REPORT_PREFIX}_implementation_timing.rpt"
    cp "$DRC_RPT" "$RUN_DIR/${REPORT_PREFIX}_implementation_drc.rpt"
    cp "$BIT_FILE" "$RUN_DIR/${REPORT_PREFIX}_gpgpu_system_wrapper.bit"
    archive_run_artifacts "$RUN_DIR" "$cu" "$UTIL_RPT" "$TIMING_RPT" "$DRC_RPT" "$BIT_FILE"

    echo "Saved complete run artifacts to $RUN_DIR/complete_run"
done

echo "Sweep complete. Results under $SWEEP_ROOT"
