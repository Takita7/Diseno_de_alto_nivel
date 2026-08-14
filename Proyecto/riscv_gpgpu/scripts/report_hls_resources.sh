#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

extract_totals() {
  local rpt="$1"
  awk -F'|' '
    /^\|Total[[:space:]]+/ {
      gsub(/ /, "", $3); gsub(/ /, "", $4); gsub(/ /, "", $5); gsub(/ /, "", $6); gsub(/ /, "", $7);
      print $3"\t"$4"\t"$5"\t"$6"\t"$7;
      exit
    }
  ' "$rpt"
}

extract_clk_est_ns() {
  local rpt="$1"
  awk -F'|' '
    /\|[[:space:]]*clk[[:space:]]*\|/ {
      gsub(/ /, "", $4);
      print $4;
      exit
    }
  ' "$rpt"
}

emit_profile() {
  local name="$1"
  local sched_rpt="$2"
  local mem_rpt="$3"

  if [[ ! -f "$sched_rpt" || ! -f "$mem_rpt" ]]; then
    echo "[report_hls_resources] Missing reports for profile '$name'" >&2
    return 1
  fi

  read -r s_bram s_dsp s_ff s_lut s_uram < <(extract_totals "$sched_rpt")
  read -r m_bram m_dsp m_ff m_lut m_uram < <(extract_totals "$mem_rpt")
  s_clk="$(extract_clk_est_ns "$sched_rpt")"
  m_clk="$(extract_clk_est_ns "$mem_rpt")"

  t_bram=$((s_bram + m_bram))
  t_dsp=$((s_dsp + m_dsp))
  t_ff=$((s_ff + m_ff))
  t_lut=$((s_lut + m_lut))
  t_uram=$((s_uram + m_uram))

  printf "%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\n" \
    "$name" "$s_clk" "$m_clk" "$t_bram" "$t_dsp" "$t_ff" "$t_lut" "$t_uram" \
    "$s_bram+$m_bram" "$s_lut+$m_lut" "$s_ff+$m_ff"
}

BASE_SCHED="$ROOT_DIR/build/ip_export/gpgpu_scheduler/gpgpu_scheduler/solution1/syn/report/riscv_gpgpu_hls_gpgpu_scheduler_csynth.rpt"
BASE_MEM="$ROOT_DIR/build/ip_export/memory_pipeline/memory_pipeline/solution1/syn/report/riscv_gpgpu_hls_memory_pipeline_csynth.rpt"
DEMO_SCHED="$ROOT_DIR/build/ip_export_demo/gpgpu_scheduler/gpgpu_scheduler_demo/solution1/syn/report/riscv_gpgpu_hls_gpgpu_scheduler_csynth.rpt"
DEMO_MEM="$ROOT_DIR/build/ip_export_demo/memory_pipeline/memory_pipeline_demo/solution1/syn/report/riscv_gpgpu_hls_memory_pipeline_csynth.rpt"

printf "profile\tsched_clk_ns\tmem_clk_ns\tbram18k_total\tdsp_total\tff_total\tlut_total\turam_total\tbram_expr\tlut_expr\tff_expr\n"
emit_profile baseline "$BASE_SCHED" "$BASE_MEM"
emit_profile demo_small_shared "$DEMO_SCHED" "$DEMO_MEM"
