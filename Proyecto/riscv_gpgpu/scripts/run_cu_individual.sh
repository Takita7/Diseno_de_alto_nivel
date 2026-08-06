#!/usr/bin/env bash
set -euo pipefail
# Ignorar SIGINT/SIGTERM en el proceso padre para que Ctrl+C no mate el script.
# Los subprocesos (vitis-run, vivado) corren con setsid en su propia sesión.
trap '' INT TERM

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
SWEEP_ROOT="${SWEEP_ROOT:-$PROJECT_ROOT/results/cu_sweep}"
JOBS="${JOBS:-$(getconf _NPROCESSORS_ONLN 2>/dev/null || printf '8')}"

usage() {
    cat <<'EOF'
Usage: scripts/run_cu_individual.sh [CU ...]

Examples:
  scripts/run_cu_individual.sh 2 4 6 8
  CU_VALUES="2 4 6 8" scripts/run_cu_individual.sh
EOF
}

if [[ $# -gt 0 ]]; then
    CU_VALUES=("$@")
else
    if [[ -n "${CU_VALUES:-}" ]]; then
        read -r -a CU_VALUES <<<"$CU_VALUES"
    else
        CU_VALUES=(2 4 6 8)
    fi
fi

if [[ "${1:-}" == "-h" || "${1:-}" == "--help" ]]; then
    usage
    exit 0
fi

mkdir -p "$SWEEP_ROOT"

is_fresh_file() {
    local file="$1"
    local start_ts="$2"
    [[ -f "$file" ]] || return 1
    local mtime
    mtime="$(stat -c %Y "$file" 2>/dev/null || printf '0')"
    (( mtime >= start_ts ))
}

find_report() {
    local pattern="$1"
    local candidate
    for candidate in \
        "$PROJECT_ROOT/build/vivado_kv260/$pattern" \
        "$PROJECT_ROOT/build/vivado_kv260_runs/post_run_num_cus_${cu}_"*/$pattern \
        "$PROJECT_ROOT/build/vivado_kv260_runs/pre_run_num_cus_${cu}_"*/$pattern; do
        if [[ -f "$candidate" ]]; then
            printf '%s\n' "$candidate"
            return 0
        fi
    done
    return 1
}

find_bitstream() {
    local candidate
    for candidate in \
        "$PROJECT_ROOT/build/vivado_kv260/riscv_gpgpu_kv260.runs/impl_1/gpgpu_system_wrapper.bit" \
        "$PROJECT_ROOT/build/vivado_kv260_runs/post_run_num_cus_${cu}_"*/gpgpu_system_wrapper.bit \
        "$PROJECT_ROOT/build/vivado_kv260_runs/pre_run_num_cus_${cu}_"*/gpgpu_system_wrapper.bit; do
        if [[ -f "$candidate" ]]; then
            printf '%s\n' "$candidate"
            return 0
        fi
    done
    return 1
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
    cp "$bit_file" "$archive_root/reports/num_cus_${cu}_gpgpu_system_wrapper.bit" 2>/dev/null || true
    cp "$run_dir/hls.log" "$archive_root/hls.log" 2>/dev/null || true
    cp "$run_dir/vivado.log" "$archive_root/vivado.log" 2>/dev/null || true
    cp "$run_dir/hls.failed" "$archive_root/hls.failed" 2>/dev/null || true
    cp "$run_dir/vivado.failed" "$archive_root/vivado.failed" 2>/dev/null || true
    cp "$PROJECT_ROOT/hls/src/common/hls_config.h" "$archive_root/hls_config.h" 2>/dev/null || true

    if [[ -d "$PROJECT_ROOT/build/vivado_kv260" ]]; then
        cp -a "$PROJECT_ROOT/build/vivado_kv260" "$archive_root/build/"
    fi
    if [[ -d "$PROJECT_ROOT/build/ip_export" ]]; then
        cp -a "$PROJECT_ROOT/build/ip_export" "$archive_root/build/"
    fi
    if [[ -d "$PROJECT_ROOT/build/vivado_kv260_runs" ]]; then
        cp -a "$PROJECT_ROOT/build/vivado_kv260_runs" "$archive_root/build/"
    fi
}

run_single_cu() {
    local cu="$1"
    local run_dir="$SWEEP_ROOT/num_cus_${cu}"
    local report_prefix="num_cus_${cu}"
    mkdir -p "$run_dir"
    rm -f "$run_dir/hls.failed" "$run_dir/vivado.failed"
    rm -f "$run_dir/${report_prefix}_implementation_utilization.rpt"
    rm -f "$run_dir/${report_prefix}_implementation_timing.rpt"
    rm -f "$run_dir/${report_prefix}_implementation_drc.rpt"
    rm -f "$run_dir/${report_prefix}_gpgpu_system_wrapper.bit"
    rm -rf "$run_dir/complete_run"

    echo "============================================================"
    echo "Running NUM_CUS=$cu"
    echo "============================================================"

    python3 - "$PROJECT_ROOT/hls/src/common/hls_config.h" "$cu" <<'PY'
import re
import sys
from pathlib import Path
path = Path(sys.argv[1])
cu = sys.argv[2]
text = path.read_text()
if '#define RISCV_GPGPU_NUM_CUS' in text:
    text = re.sub(r'#define RISCV_GPGPU_NUM_CUS\s+\d+', f'#define RISCV_GPGPU_NUM_CUS {cu}', text)
elif 'constexpr int NUM_CUS' in text:
    text = re.sub(r'constexpr\s+int\s+NUM_CUS\s*=\s*\d+;', f'constexpr int NUM_CUS = {cu};', text)
else:
    raise SystemExit('NUM_CUS definition not found')
path.write_text(text)
PY

    echo "Updated HLS config to NUM_CUS=$cu"

    if ! command -v vitis-run >/dev/null 2>&1; then
        echo "vitis-run not found; source your Vitis environment first" >&2
        return 1
    fi
    if ! command -v vivado >/dev/null 2>&1; then
        echo "vivado not found; source your Vivado environment first" >&2
        return 1
    fi

    # setsid aisla el proceso de la sesión del terminal — evita que Ctrl+C en el
    # shell padre envíe SIGINT a Vivado/vitis-run mientras corren en background.
    if ! setsid vitis-run --mode hls --tcl "$PROJECT_ROOT/tests/fpga/export_gpgpu_ip.tcl" > "$run_dir/hls.log" 2>&1; then
        echo "HLS failed for NUM_CUS=$cu (gpgpu_scheduler)" | tee "$run_dir/hls.failed"
        return 1
    fi

    if ! setsid vitis-run --mode hls --tcl "$PROJECT_ROOT/tests/fpga/export_memory_ip.tcl" >> "$run_dir/hls.log" 2>&1; then
        echo "HLS failed for NUM_CUS=$cu (memory_pipeline)" | tee "$run_dir/hls.failed"
        return 1
    fi

    local run_start_ts="$(date +%s)"
    if ! setsid vivado -mode batch -source "$PROJECT_ROOT/fpga/scripts/build_all.tcl" -tclargs "$JOBS" > "$run_dir/vivado.log" 2>&1; then
        echo "Vivado build failed for NUM_CUS=$cu" | tee "$run_dir/vivado.failed"
        return 1
    fi

    local util_rpt=""
    local timing_rpt=""
    local drc_rpt=""
    local bit_file=""

    if util_rpt="$(find_report 'implementation_utilization.rpt' 2>/dev/null)"; then :; fi
    if timing_rpt="$(find_report 'implementation_timing.rpt' 2>/dev/null)"; then :; fi
    if drc_rpt="$(find_report 'implementation_drc.rpt' 2>/dev/null)"; then :; fi
    if bit_file="$(find_bitstream 2>/dev/null)"; then :; fi

    if [[ -z "$util_rpt" || -z "$timing_rpt" || -z "$drc_rpt" || -z "$bit_file" ]]; then
        echo "Vivado build failed for NUM_CUS=$cu (missing impl outputs)" | tee "$run_dir/vivado.failed"
        return 1
    fi

    if ! is_fresh_file "$util_rpt" "$run_start_ts" || \
       ! is_fresh_file "$timing_rpt" "$run_start_ts" || \
       ! is_fresh_file "$drc_rpt" "$run_start_ts" || \
       ! is_fresh_file "$bit_file" "$run_start_ts"; then
        echo "Vivado build failed for NUM_CUS=$cu (missing fresh impl outputs)" | tee "$run_dir/vivado.failed"
        return 1
    fi

    cp "$util_rpt" "$run_dir/${report_prefix}_implementation_utilization.rpt"
    cp "$timing_rpt" "$run_dir/${report_prefix}_implementation_timing.rpt"
    cp "$drc_rpt" "$run_dir/${report_prefix}_implementation_drc.rpt"
    cp "$bit_file" "$run_dir/${report_prefix}_gpgpu_system_wrapper.bit"
    archive_run_artifacts "$run_dir" "$cu" "$util_rpt" "$timing_rpt" "$drc_rpt" "$bit_file"

    echo "Saved complete run artifacts to $run_dir/complete_run"
}

for cu in "${CU_VALUES[@]}"; do
    if [[ ! "$cu" =~ ^[0-9]+$ ]]; then
        echo "Skipping invalid CU value: $cu" >&2
        continue
    fi
    if ! run_single_cu "$cu"; then
        echo "Continuing with the next CU value after NUM_CUS=$cu" >&2
    fi
    echo
done

echo "Finished individual CU runs. Results under $SWEEP_ROOT"
