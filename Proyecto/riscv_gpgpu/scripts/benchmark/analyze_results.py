#!/usr/bin/env python3
import argparse
import json
from pathlib import Path


def main():
    parser = argparse.ArgumentParser(description="Analyze benchmark config and produce a reproducible summary.")
    parser.add_argument("--config", required=True, help="Path to benchmark configuration YAML file")
    parser.add_argument("--output", required=True, help="Output JSON summary file")
    args = parser.parse_args()

    config_path = Path(args.config)
    output_path = Path(args.output)

    if not config_path.exists():
        raise SystemExit(f"Config file not found: {config_path}")

    try:
        import yaml
    except ImportError:
        raise SystemExit("PyYAML is required to run this analysis script. Install it in your Python environment.")

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


if __name__ == "__main__":
    main()
