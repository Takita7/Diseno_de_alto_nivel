import importlib.util
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location(
    "collect_cu_sweep_csv",
    ROOT / "scripts" / "collect_cu_sweep_csv.py",
)
module = importlib.util.module_from_spec(SPEC)
assert SPEC.loader is not None
SPEC.loader.exec_module(module)


def test_collect_rows_adds_frequency_and_headroom_metrics(tmp_path):
    run_dir = tmp_path / "num_cus_2"
    run_dir.mkdir(parents=True)

    (run_dir / "num_cus_2_implementation_timing.rpt").write_text(
        """| WNS(ns) | TNS(ns) | WHS(ns) | THS(ns) | WPWS(ns) | TPWS(ns) |
| 1.0 | 0.0 | 0.2 | 0.0 | 0.1 | 0.0 |
""",
        encoding="utf-8",
    )
    (run_dir / "num_cus_2_implementation_utilization.rpt").write_text(
        """| Site Type | Used | Fixed | Prohibited | Available | Util% |
| CLB LUTs | 10 | 0 | 0 | 100 | 10.0 |
| CLB Registers | 5 | 0 | 0 | 100 | 5.0 |
| Block RAM Tile | 2 | 0 | 0 | 20 | 10.0 |
| DSPs | 1 | 0 | 0 | 10 | 10.0 |
""",
        encoding="utf-8",
    )
    (run_dir / "num_cus_2_implementation_drc.rpt").write_text(
        """Checks found: 2
| Rule | Severity | Description | Checks |
| foo | Warning | bar | 2 |
""",
        encoding="utf-8",
    )

    rows = module.collect_rows(tmp_path)

    assert len(rows) == 1
    row = rows[0]
    assert row["fmax_mhz"] == 250.0
    assert row["lut_util_pct"] == 10.0
    assert row["lut_headroom_pct"] == 90.0
    assert row["kria_utilization_headroom_pct"] == 90.0


def test_find_best_report_prefers_current_cu_reports_over_other_cus(tmp_path):
    root = tmp_path / "num_cus_4"
    root.mkdir(parents=True)
    other_cu_dir = root / "vivado_kv260_runs" / "pre_run_num_cus_2_20260806_100315"
    other_cu_dir.mkdir(parents=True)
    (other_cu_dir / "implementation_timing.rpt").write_text(
        "| WNS(ns) | TNS(ns) | WHS(ns) | THS(ns) | WPWS(ns) | TPWS(ns) |\n| 1.0 | 0.0 | 0.2 | 0.0 | 0.1 | 0.0 |\n",
        encoding="utf-8",
    )

    current_cu_dir = root / "vivado_kv260_runs" / "pre_run_num_cus_4_20260806_100315"
    current_cu_dir.mkdir(parents=True)
    (current_cu_dir / "implementation_timing.rpt").write_text(
        "| WNS(ns) | TNS(ns) | WHS(ns) | THS(ns) | WPWS(ns) | TPWS(ns) |\n| 2.0 | 0.0 | 0.4 | 0.0 | 0.2 | 0.0 |\n",
        encoding="utf-8",
    )

    found = module.find_best_report(root, 4, "implementation_timing.rpt")

    assert found == current_cu_dir / "implementation_timing.rpt"
