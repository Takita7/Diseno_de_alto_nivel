#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
REP_DIR="$ROOT_DIR/build/vivado_kv260"
UTIL_RPT="$REP_DIR/implementation_utilization.rpt"
TIMING_RPT="$REP_DIR/implementation_timing.rpt"
DRC_RPT="$REP_DIR/implementation_drc.rpt"

if [[ ! -f "$UTIL_RPT" || ! -f "$TIMING_RPT" ]]; then
  echo "[report_vivado_impl_summary] Missing implementation reports."
  echo "Expected:"
  echo "  $UTIL_RPT"
  echo "  $TIMING_RPT"
  exit 1
fi

extract_logic_line() {
  local key="$1"
  awk -F'|' -v k="$key" '
    $0 ~ "\| "k" " {
      gsub(/ /, "", $3); gsub(/ /, "", $6); gsub(/ /, "", $7);
      print $3"\\t"$6"\\t"$7;
      exit
    }
  ' "$UTIL_RPT"
}

extract_dsp_line() {
  awk -F'|' '
    $0 ~ /\| DSPs[[:space:]]*\|/ {
      gsub(/ /, "", $3); gsub(/ /, "", $6); gsub(/ /, "", $7);
      print $3"\\t"$6"\\t"$7;
      exit
    }
  ' "$UTIL_RPT"
}

extract_wns() {
  awk '
    /All user specified timing constraints are met\./ {met="yes"}
    /^[[:space:]]*[-+]?[0-9]+\.[0-9]+[[:space:]]+[-+]?[0-9]+\.[0-9]+[[:space:]]+[0-9]+[[:space:]]+[0-9]+/ {
      if (line == "") line=$0
    }
    END {
      if (line != "") {
        gsub(/^ +| +$/, "", line);
        print line;
      }
      if (met == "yes") {
        print "TIMING_MET=yes";
      } else {
        print "TIMING_MET=no_or_unknown";
      }
    }
  ' "$TIMING_RPT"
}

read -r lut_used lut_avail lut_util < <(extract_logic_line "CLB LUTs")
read -r ff_used ff_avail ff_util < <(extract_logic_line "CLB Registers")
read -r bram_used bram_avail bram_util < <(extract_logic_line "Block RAM Tile")
read -r dsp_used dsp_avail dsp_util < <(extract_dsp_line)

echo "metric\tused\tavailable\tutil_percent"
echo "CLB_LUT\t$lut_used\t$lut_avail\t$lut_util"
echo "CLB_FF\t$ff_used\t$ff_avail\t$ff_util"
echo "BRAM_TILE\t$bram_used\t$bram_avail\t$bram_util"
echo "DSP\t$dsp_used\t$dsp_avail\t$dsp_util"

echo
echo "timing_summary_wns_line"
extract_wns

echo
echo "report_paths"
echo "$UTIL_RPT"
echo "$TIMING_RPT"
if [[ -f "$DRC_RPT" ]]; then
  echo "$DRC_RPT"
fi
