#!/usr/bin/env python3
import argparse
import csv
import json
import re
from pathlib import Path
from statistics import mean, median


CASE_RE = re.compile(r"^cus(?P<cus>\d+)_threads(?P<threads>\d+)\.log$")
ELAPSED_RE = re.compile(r"elapsed=(?P<elapsed_ms>\d+)ms")
EXEC_RE = re.compile(
    r"Execution complete:\s+(?P<cycles>\d+)\s+cycles,\s+(?P<instructions>\d+)\s+instructions"
)
METRICS_RE = re.compile(
    r"Workload metrics:\s+scheduler=(?P<scheduler>\w+)\s+workers=(?P<workers>\d+)\s+"
    r"active_units=(?P<active_units>\d+)\s+worker_cycles_total=(?P<worker_cycles_total>\d+)\s+"
    r"worker_cycles_mean=(?P<worker_cycles_mean>[0-9.]+)\s+worker_cycles_max=(?P<worker_cycles_max>\d+)\s+"
    r"effective_cycles=(?P<effective_cycles>\d+)\s+parallelism_factor=(?P<parallelism_factor>[0-9.]+)"
)
PASS_RE = re.compile(r"\[       OK \] RodiniaRealBenchmark\.BfsFanoutStress")
FAIL_RE = re.compile(r"\[  FAILED  \] RodiniaRealBenchmark\.BfsFanoutStress")


def load_config_summary(config_path: Path, output_path: Path) -> None:
    if not config_path.exists():
        raise SystemExit(f"Config file not found: {config_path}")

    try:
        import yaml
    except ImportError as exc:
        raise SystemExit(
            "PyYAML is required to run this analysis mode. Install it in your Python environment."
        ) from exc

    config = yaml.safe_load(config_path.read_text())
    summary = {
        "benchmark_config": str(config_path),
        "benchmark_suite": config.get("benchmark_suite", {}),
        "measurements": config.get("measurements", {}),
        "workloads": config.get("workloads", {}),
        "architecture": config.get("architecture", {}),
        "result_format": config.get("results", {}).get("format", "json"),
        "reproducible": True,
    }

    output_path.parent.mkdir(parents=True, exist_ok=True)
    output_path.write_text(json.dumps(summary, indent=2))
    print(f"Wrote reproducible benchmark summary to {output_path}")


def parse_summary_tsv(summary_path: Path) -> list[dict[str, str]]:
    records: list[dict[str, str]] = []
    if not summary_path.exists():
        return records

    with summary_path.open(newline="") as handle:
        reader = csv.DictReader(handle, delimiter="\t")
        for row in reader:
            if not row:
                continue
            records.append(row)
    return records


def analyze_matrix(matrix_dir: Path, output_path: Path) -> None:
    if not matrix_dir.exists():
        raise SystemExit(f"Matrix directory not found: {matrix_dir}")

    summary_tsv = matrix_dir / "summary.tsv"
    run_records = parse_summary_tsv(summary_tsv)

    cases = []
    if run_records:
        source_records = run_records
    else:
        source_records = []
        for log_path in sorted(matrix_dir.glob("cus*_threads*.log")):
            match = CASE_RE.match(log_path.name)
            if not match:
                continue
            source_records.append({
                "case": log_path.stem,
                "cus": match.group("cus"),
                "threads": match.group("threads"),
                "log": str(log_path),
            })

    for record in source_records:
        try:
            cus = int(record.get("cus", ""))
            threads = int(record.get("threads", ""))
        except ValueError:
            continue

        log_path = Path(record.get("log") or record.get("log_path") or matrix_dir / f"cus{cus}_threads{threads}.log")
        if not log_path.exists():
            continue
        text = log_path.read_text(errors="replace")

        status = record.get("status")
        elapsed_ms = record.get("elapsed_ms")

        if elapsed_ms is not None and elapsed_ms != "":
            try:
                elapsed_ms = int(elapsed_ms)
            except ValueError:
                elapsed_ms = None
        else:
            elapsed_match = ELAPSED_RE.search(text)
            elapsed_ms = int(elapsed_match.group("elapsed_ms")) if elapsed_match else None

        exec_match = EXEC_RE.search(text)
        cycles = int(exec_match.group("cycles")) if exec_match else None
        instructions = int(exec_match.group("instructions")) if exec_match else None

        metrics_match = METRICS_RE.search(text)
        workload_metrics = None
        if metrics_match:
            workload_metrics = {
                "scheduler": metrics_match.group("scheduler"),
                "workers": int(metrics_match.group("workers")),
                "active_units": int(metrics_match.group("active_units")),
                "worker_cycles_total": int(metrics_match.group("worker_cycles_total")),
                "worker_cycles_mean": float(metrics_match.group("worker_cycles_mean")),
                "worker_cycles_max": int(metrics_match.group("worker_cycles_max")),
                "effective_cycles": int(metrics_match.group("effective_cycles")),
                "parallelism_factor": float(metrics_match.group("parallelism_factor")),
            }

        if status is None:
            if PASS_RE.search(text) and not FAIL_RE.search(text):
                status = "PASS"
            elif FAIL_RE.search(text):
                status = "FAIL"
            else:
                status = "UNKNOWN"

        ipc = None
        if cycles and cycles > 0 and instructions is not None:
            ipc = instructions / cycles

        cases.append({
            "case": f"cus{cus}_threads{threads}",
            "cus": cus,
            "threads": threads,
            "status": status,
            "elapsed_ms": elapsed_ms,
            "cycles": cycles,
            "instructions": instructions,
            "ipc": ipc,
            "workload_metrics": workload_metrics,
            "log_path": str(log_path),
        })

    elapsed_values = [case["elapsed_ms"] for case in cases if case["elapsed_ms"] is not None]
    cycle_values = [case["cycles"] for case in cases if case["cycles"] is not None]
    worker_mean_values = [case["workload_metrics"]["worker_cycles_mean"] for case in cases if case["workload_metrics"]]
    effective_cycle_values = [case["workload_metrics"]["effective_cycles"] for case in cases if case["workload_metrics"]]
    parallelism_values = [case["workload_metrics"]["parallelism_factor"] for case in cases if case["workload_metrics"]]

    summary = {
        "matrix_dir": str(matrix_dir),
        "summary_tsv": str(summary_tsv) if summary_tsv.exists() else None,
        "case_count": len(cases),
        "pass_count": sum(1 for case in cases if case["status"] == "PASS"),
        "fail_count": sum(1 for case in cases if case["status"] == "FAIL"),
        "elapsed_ms": {
            "min": min(elapsed_values) if elapsed_values else None,
            "max": max(elapsed_values) if elapsed_values else None,
            "mean": mean(elapsed_values) if elapsed_values else None,
            "median": median(elapsed_values) if elapsed_values else None,
        },
        "cycles": {
            "min": min(cycle_values) if cycle_values else None,
            "max": max(cycle_values) if cycle_values else None,
            "mean": mean(cycle_values) if cycle_values else None,
            "median": median(cycle_values) if cycle_values else None,
        },
        "worker_cycles_mean": {
            "min": min(worker_mean_values) if worker_mean_values else None,
            "max": max(worker_mean_values) if worker_mean_values else None,
            "mean": mean(worker_mean_values) if worker_mean_values else None,
            "median": median(worker_mean_values) if worker_mean_values else None,
        },
        "effective_cycles": {
            "min": min(effective_cycle_values) if effective_cycle_values else None,
            "max": max(effective_cycle_values) if effective_cycle_values else None,
            "mean": mean(effective_cycle_values) if effective_cycle_values else None,
            "median": median(effective_cycle_values) if effective_cycle_values else None,
        },
        "parallelism_factor": {
            "min": min(parallelism_values) if parallelism_values else None,
            "max": max(parallelism_values) if parallelism_values else None,
            "mean": mean(parallelism_values) if parallelism_values else None,
            "median": median(parallelism_values) if parallelism_values else None,
        },
        "cases": cases,
    }

    output_path.parent.mkdir(parents=True, exist_ok=True)
    output_path.write_text(json.dumps(summary, indent=2))
    print(f"Wrote matrix benchmark summary to {output_path}")


def main():
    parser = argparse.ArgumentParser(description="Analyze benchmark config or matrix results.")
    mode = parser.add_mutually_exclusive_group(required=True)
    mode.add_argument("--config", help="Path to benchmark configuration YAML file")
    mode.add_argument("--matrix-dir", help="Path to Rodinia real matrix results directory")
    parser.add_argument("--output", required=True, help="Output JSON summary file")
    args = parser.parse_args()

    output_path = Path(args.output)
    if args.config:
        load_config_summary(Path(args.config), output_path)
    else:
        analyze_matrix(Path(args.matrix_dir), output_path)


if __name__ == "__main__":
    main()
