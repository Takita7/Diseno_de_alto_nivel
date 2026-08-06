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
    }
    if not path.is_file():
        return metrics

    lines = path.read_text(errors="ignore").splitlines()

    # Find the line with summary headers, then first numeric row after divider.
    header_idx = None
    for i, line in enumerate(lines):
        if "WNS(ns)" in line and "TNS(ns)" in line and "WHS(ns)" in line:
            header_idx = i
            break
    if header_idx is None:
        return metrics

    number_line = None
    num_pat = re.compile(r"^\s*[-+]?\d+\.\d+\s+[-+]?\d+\.\d+")
    for line in lines[header_idx + 1 : header_idx + 20]:
        if num_pat.search(line):
            number_line = line
            break

    if not number_line:
        return metrics

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

    return metrics


def parse_utilization_report(path: Path) -> Dict[str, Optional[float]]:
    metrics: Dict[str, Optional[float]] = {
        "lut_used": None,
        "lut_avail": None,
        "lut_util_pct": None,
        "ff_used": None,
        "ff_avail": None,
        "ff_util_pct": None,
        "bram_used": None,
        "bram_avail": None,
        "bram_util_pct": None,
        "dsp_used": None,
        "dsp_avail": None,
        "dsp_util_pct": None,
    }
    if not path.is_file():
        return metrics

    rows = path.read_text(errors="ignore").splitlines()

    def parse_row(site_name: str) -> Optional[List[str]]:
        for line in rows:
            if site_name not in line or "|" not in line:
                continue
            parts = [p.strip() for p in line.split("|")]
            # Format is: empty | Site Type | Used | Fixed | Prohibited | Available | Util% | empty
            if len(parts) < 8:
                continue
            if parts[1] != site_name:
                continue
            return parts
        return None

    def set_metric(prefix: str, row: Optional[List[str]]) -> None:
        if not row:
            return
        try:
            metrics[f"{prefix}_used"] = float(row[2])
            metrics[f"{prefix}_avail"] = float(row[5])
            metrics[f"{prefix}_util_pct"] = float(row[6])
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


def detect_status(run_dir: Path) -> str:
    if (run_dir / "hls.failed").exists():
        return "HLS_FAIL"
    if (run_dir / "vivado.failed").exists():
        return "VIVADO_FAIL"
    return "OK"


def collect_rows(root: Path) -> List[Dict[str, object]]:
    rows: List[Dict[str, object]] = []

    for run_dir in sorted(root.glob("num_cus_*"), key=lambda p: int(p.name.split("_")[-1])):
        m = re.match(r"num_cus_(\d+)$", run_dir.name)
        if not m:
            continue
        cu = int(m.group(1))

        timing_path = run_dir / f"num_cus_{cu}_implementation_timing.rpt"
        util_path = run_dir / f"num_cus_{cu}_implementation_utilization.rpt"
        drc_path = run_dir / f"num_cus_{cu}_implementation_drc.rpt"
        bit_path = run_dir / f"num_cus_{cu}_gpgpu_system_wrapper.bit"

        row: Dict[str, object] = {
            "cu": cu,
            "status": detect_status(run_dir),
            "has_bitstream": int(bit_path.exists()),
            "run_dir": str(run_dir),
        }
        row.update(parse_timing_report(timing_path))
        row.update(parse_utilization_report(util_path))
        row.update(parse_drc_report(drc_path))

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
        "lut_used",
        "lut_avail",
        "lut_util_pct",
        "ff_used",
        "ff_avail",
        "ff_util_pct",
        "bram_used",
        "bram_avail",
        "bram_util_pct",
        "dsp_used",
        "dsp_avail",
        "dsp_util_pct",
        "drc_checks_found",
        "drc_warning_checks",
        "drc_critical_warning_checks",
        "drc_advisory_checks",
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
