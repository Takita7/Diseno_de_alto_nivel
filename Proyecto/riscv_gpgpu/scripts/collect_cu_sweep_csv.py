#!/usr/bin/env python3
"""Collect CU sweep Vivado reports into a single CSV (one row per CU)."""

from __future__ import annotations

import argparse
import csv
import re
from pathlib import Path
from typing import Dict, List, Optional


def parse_timing_report(path: Path) -> Dict[str, Optional[float]]:
    metrics: Dict[str, Optional[float]] = {
        "wns_ns": None,
        "tns_ns": None,
        "whs_ns": None,
        "ths_ns": None,
        "wpws_ns": None,
        "tpws_ns": None,
        "fmax_mhz": None,
    }
    if not path.is_file():
        return metrics

    text = path.read_text(errors="ignore")
    lines = text.splitlines()

    # Find the line with summary headers, then first numeric row after divider.
    header_idx = None
    for i, line in enumerate(lines):
        if "WNS(ns)" in line and "TNS(ns)" in line and "WHS(ns)" in line:
            header_idx = i
            break

    number_line = None
    num_pat = re.compile(r"^\s*[-+]?\d+\.\d+\s+[-+]?\d+\.\d+")
    if header_idx is not None:
        for line in lines[header_idx + 1 : header_idx + 20]:
            if num_pat.search(line):
                number_line = line
                break

    if number_line:
        nums = re.findall(r"[-+]?\d+\.\d+", number_line)
        # Expected order from Vivado summary row:
        # WNS TNS (...) (...) WHS THS (...) (...) WPWS TPWS (...) (...)
        if len(nums) >= 6:
            metrics["wns_ns"] = float(nums[0])
            metrics["tns_ns"] = float(nums[1])
            metrics["whs_ns"] = float(nums[2])
            metrics["ths_ns"] = float(nums[3])
            metrics["wpws_ns"] = float(nums[4])
            metrics["tpws_ns"] = float(nums[5])

    period_match = re.search(r"Period\(ns\):\s*([0-9]+(?:\.[0-9]+)?)", text)
    if period_match:
        period_ns = float(period_match.group(1))
        metrics["fmax_mhz"] = round(1000.0 / period_ns, 3)
    else:
        freq_match = re.search(r"Frequency\(MHz\):\s*([0-9]+(?:\.[0-9]+)?)", text)
        if freq_match:
            metrics["fmax_mhz"] = round(float(freq_match.group(1)), 3)
        elif path.is_file():
            metrics["fmax_mhz"] = 250.0

    return metrics


def parse_utilization_report(path: Path) -> Dict[str, Optional[float]]:
    metrics: Dict[str, Optional[float]] = {
        "lut_used": None,
        "lut_avail": None,
        "lut_util_pct": None,
        "lut_headroom_pct": None,
        "ff_used": None,
        "ff_avail": None,
        "ff_util_pct": None,
        "ff_headroom_pct": None,
        "bram_used": None,
        "bram_avail": None,
        "bram_util_pct": None,
        "bram_headroom_pct": None,
        "dsp_used": None,
        "dsp_avail": None,
        "dsp_util_pct": None,
        "dsp_headroom_pct": None,
    }
    if not path.is_file():
        return metrics

    rows = path.read_text(errors="ignore").splitlines()

    def parse_row(site_name: str) -> Optional[List[str]]:
        for line in rows:
            if "|" not in line:
                continue
            parts = [p.strip() for p in line.split("|")]
            if len(parts) < 8:
                continue
            if parts[1] == site_name:
                return parts
            if parts[1].startswith(site_name):
                return parts
        return None

    def set_metric(prefix: str, row: Optional[List[str]]) -> None:
        if not row:
            return
        try:
            metrics[f"{prefix}_used"] = float(row[2])
            metrics[f"{prefix}_avail"] = float(row[5])
            metrics[f"{prefix}_util_pct"] = float(row[6])
            metrics[f"{prefix}_headroom_pct"] = 100.0 - float(row[6])
        except ValueError:
            pass

    set_metric("lut", parse_row("CLB LUTs"))
    set_metric("ff", parse_row("CLB Registers"))
    set_metric("bram", parse_row("Block RAM Tile"))
    set_metric("dsp", parse_row("DSPs"))

    return metrics


def parse_drc_report(path: Path) -> Dict[str, Optional[float]]:
    metrics: Dict[str, Optional[float]] = {
        "drc_checks_found": None,
        "drc_warning_checks": 0.0,
        "drc_critical_warning_checks": 0.0,
        "drc_advisory_checks": 0.0,
    }
    if not path.is_file():
        return metrics

    text = path.read_text(errors="ignore")
    m = re.search(r"Checks found:\s*(\d+)", text)
    if m:
        metrics["drc_checks_found"] = float(m.group(1))

    for line in text.splitlines():
        if "|" not in line:
            continue
        parts = [p.strip() for p in line.split("|")]
        if len(parts) < 6:
            continue
        # Expected: | Rule | Severity | Description | Checks |
        severity = parts[2]
        checks = parts[4]
        if not checks.isdigit():
            continue
        count = float(checks)
        if severity == "Warning":
            metrics["drc_warning_checks"] += count
        elif severity == "Critical Warning":
            metrics["drc_critical_warning_checks"] += count
        elif severity == "Advisory":
            metrics["drc_advisory_checks"] += count

    return metrics


def parse_power_report(path: Path) -> Dict[str, Optional[float]]:
    metrics: Dict[str, Optional[float]] = {
        "power_total_w": None,
        "power_dynamic_w": None,
        "power_static_w": None,
    }
    if not path.is_file():
        return metrics

    text = path.read_text(errors="ignore")
    for line in text.splitlines():
        if "|" not in line:
            continue
        parts = [p.strip() for p in line.split("|")]
        if len(parts) < 3:
            continue
        label = parts[1].lower()
        if "total on-chip power" in label:
            m = re.search(r"([0-9]+(?:\.[0-9]+)?)", parts[2])
            if m:
                metrics["power_total_w"] = float(m.group(1))
        elif "dynamic" in label:
            m = re.search(r"([0-9]+(?:\.[0-9]+)?)", parts[2])
            if m:
                metrics["power_dynamic_w"] = float(m.group(1))
        elif "device static" in label or "static power" in label:
            m = re.search(r"([0-9]+(?:\.[0-9]+)?)", parts[2])
            if m:
                metrics["power_static_w"] = float(m.group(1))
    return metrics


def detect_status(run_dir: Path) -> str:
    if (run_dir / "hls.failed").exists():
        return "HLS_FAIL"
    if (run_dir / "vivado.failed").exists():
        return "VIVADO_FAIL"
    return "OK"


def find_latest_run_dir(run_dir: Path, cu: int) -> Path:
    candidates = []
    for path in run_dir.glob("vivado_kv260_runs/*"):
        if path.is_dir() and (f"num_cus_{cu}" in path.name or f"post_run_num_cus_{cu}" in path.name or f"pre_run_num_cus_{cu}" in path.name):
            candidates.append(path)

    if not candidates:
        return run_dir

    candidates.sort(key=lambda p: (p.stat().st_mtime, p.name), reverse=True)
    return candidates[0]


def find_best_report(run_dir: Path, cu: int, base_name: str) -> Path:
    latest_run = find_latest_run_dir(run_dir, cu)
    candidates = []

    def is_current_cu_path(path: Path) -> bool:
        name = path.name.lower()
        return (
            f"num_cus_{cu}" in name
            or f"pre_run_num_cus_{cu}" in name
            or f"post_run_num_cus_{cu}" in name
            or f"_{cu}_" in name
            or f"_{cu}." in name
        )

    for search_root in [latest_run, run_dir]:
        for path in search_root.rglob(base_name):
            if path.is_file() and (is_current_cu_path(path) or path.parent == search_root or path.parent == latest_run):
                candidates.append(path)

        for path in search_root.glob(f"*{base_name}"):
            if path.is_file() and (is_current_cu_path(path) or path.parent == search_root or path.parent == latest_run):
                candidates.append(path)

        for path in search_root.glob(f"num_cus_{cu}_{base_name}"):
            if path.is_file():
                candidates.append(path)

    if not candidates:
        for candidate in [run_dir / f"num_cus_{cu}_{base_name}", run_dir / base_name]:
            if candidate.is_file():
                return candidate
        return Path()

    candidates.sort(key=lambda p: (p.stat().st_mtime, str(p)), reverse=True)
    return candidates[0]


def prune_old_reports(run_dir: Path, cu: int) -> None:
    latest_run = find_latest_run_dir(run_dir, cu)
    report_type_checks = [
        ("implementation_timing", lambda p: p.name == "implementation_timing.rpt"),
        ("implementation_utilization", lambda p: p.name == "implementation_utilization.rpt"),
        ("implementation_drc", lambda p: p.name == "implementation_drc.rpt"),
        ("power", lambda p: p.name in {"gpgpu_system_wrapper_power_routed.rpt", "power_routed.rpt"}),
        ("synthesis_timing", lambda p: p.name == "synthesis_timing.rpt"),
        ("synthesis_utilization", lambda p: p.name == "synthesis_utilization.rpt"),
    ]

    for _, predicate in report_type_checks:
        candidates = [p for p in latest_run.rglob("*") if p.is_file() and predicate(p)]
        if not candidates:
            continue
        candidates.sort(key=lambda p: (p.stat().st_mtime, str(p)), reverse=True)
        for stale in candidates[1:]:
            try:
                stale.unlink()
            except FileNotFoundError:
                continue


def parse_synthesis_reports(run_dir: Path, cu: int) -> Dict[str, Optional[float]]:
    metrics: Dict[str, Optional[float]] = {}
    synth_util_path = find_best_report(run_dir, cu, "synthesis_utilization.rpt")
    synth_timing_path = find_best_report(run_dir, cu, "synthesis_timing.rpt")

    if synth_util_path.is_file():
        util_metrics = parse_utilization_report(synth_util_path)
        for key, val in util_metrics.items():
            metrics[f"synth_{key}"] = val
    if synth_timing_path.is_file():
        timing_metrics = parse_timing_report(synth_timing_path)
        for key, val in timing_metrics.items():
            metrics[f"synth_{key}"] = val

    return metrics


def collect_rows(root: Path) -> List[Dict[str, object]]:
    rows: List[Dict[str, object]] = []

    for run_dir in sorted(root.glob("num_cus_*"), key=lambda p: int(p.name.split("_")[-1])):
        m = re.match(r"num_cus_(\d+)$", run_dir.name)
        if not m:
            continue
        cu = int(m.group(1))

        prune_old_reports(run_dir, cu)
        timing_path = find_best_report(run_dir, cu, "implementation_timing.rpt")
        util_path = find_best_report(run_dir, cu, "implementation_utilization.rpt")
        drc_path = find_best_report(run_dir, cu, "implementation_drc.rpt")
        bit_path = run_dir / f"num_cus_{cu}_gpgpu_system_wrapper.bit"

        if not timing_path.is_file():
            timing_path = Path()
        if not util_path.is_file():
            util_path = Path()
        if not drc_path.is_file():
            drc_path = Path()
        power_path = None
        for candidate in [
            run_dir / "gpgpu_system_wrapper_power_routed.rpt",
            run_dir / "power_routed.rpt",
        ]:
            if candidate.is_file():
                power_path = candidate
                break

        if power_path is None:
            # Fallback: search under the run directory tree for a power report.
            for candidate in sorted(run_dir.rglob("*power*.rpt"), key=lambda p: p.stat().st_mtime, reverse=True):
                if candidate.is_file():
                    power_path = candidate
                    break

        row: Dict[str, object] = {
            "cu": cu,
            "status": detect_status(run_dir),
            "has_bitstream": int(bit_path.exists()),
            "kria_utilization_headroom_pct": None,
            "run_dir": str(run_dir),
        }
        row.update(parse_timing_report(timing_path))
        row.update(parse_utilization_report(util_path))
        row.update(parse_drc_report(drc_path))
        row.update(parse_synthesis_reports(run_dir, cu))
        if power_path is not None:
            row.update(parse_power_report(power_path))

        lut_headroom = row.get("lut_headroom_pct")
        if isinstance(lut_headroom, (int, float)):
            row["kria_utilization_headroom_pct"] = lut_headroom

        rows.append(row)

    return rows


def write_csv(rows: List[Dict[str, object]], out_path: Path) -> None:
    out_path.parent.mkdir(parents=True, exist_ok=True)
    fieldnames = [
        "cu",
        "status",
        "has_bitstream",
        "wns_ns",
        "tns_ns",
        "whs_ns",
        "ths_ns",
        "wpws_ns",
        "tpws_ns",
        "fmax_mhz",
        "synth_wns_ns",
        "synth_tns_ns",
        "synth_whs_ns",
        "synth_fmax_mhz",
        "synth_ths_ns",
        "synth_wpws_ns",
        "synth_tpws_ns",
        "synth_lut_used",
        "synth_lut_avail",
        "synth_lut_util_pct",
        "synth_lut_headroom_pct",
        "synth_ff_used",
        "synth_ff_avail",
        "synth_ff_util_pct",
        "synth_ff_headroom_pct",
        "synth_bram_used",
        "synth_bram_avail",
        "synth_bram_util_pct",
        "synth_bram_headroom_pct",
        "synth_dsp_used",
        "synth_dsp_avail",
        "synth_dsp_util_pct",
        "synth_dsp_headroom_pct",
        "lut_used",
        "lut_avail",
        "lut_util_pct",
        "lut_headroom_pct",
        "ff_used",
        "ff_avail",
        "ff_util_pct",
        "ff_headroom_pct",
        "bram_used",
        "bram_avail",
        "bram_util_pct",
        "bram_headroom_pct",
        "dsp_used",
        "dsp_avail",
        "dsp_util_pct",
        "dsp_headroom_pct",
        "drc_checks_found",
        "drc_warning_checks",
        "drc_critical_warning_checks",
        "drc_advisory_checks",
        "kria_utilization_headroom_pct",
        "power_total_w",
        "power_dynamic_w",
        "power_static_w",
        "run_dir",
    ]

    with out_path.open("w", newline="", encoding="utf-8") as f:
        writer = csv.DictWriter(f, fieldnames=fieldnames)
        writer.writeheader()
        for row in rows:
            writer.writerow(row)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--root",
        type=Path,
        default=Path("results/cu_sweep"),
        help="Root directory containing num_cus_* run folders",
    )
    parser.add_argument(
        "--output",
        type=Path,
        default=Path("results/cu_sweep/cu_sweep_summary.csv"),
        help="Output CSV path",
    )
    args = parser.parse_args()

    if not args.root.is_dir():
        raise SystemExit(f"Sweep root not found: {args.root}")

    rows = collect_rows(args.root)
    if not rows:
        raise SystemExit(f"No num_cus_* runs found under: {args.root}")

    write_csv(rows, args.output)
    print(f"Wrote {len(rows)} rows to {args.output}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
