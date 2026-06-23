#!/usr/bin/env python3
"""Summarize GeoDF-VINS-Hard benchmark results into markdown tables.

Reads run folders under --root with eval/metrics.json and optional geo_df_stats.csv.
"""
from __future__ import annotations

import argparse
import csv
import json
import re
import statistics
from pathlib import Path


def _ate_rmse(run_dir: Path) -> float | None:
    metrics = run_dir / "eval" / "metrics.json"
    if not metrics.is_file():
        return None
    data = json.loads(metrics.read_text())
    if "ate_rmse_m" in data:
        return data["ate_rmse_m"]
    return data.get("ate", {}).get("rmse")


def _geo_stats(run_dir: Path) -> dict[str, float] | None:
    """Aggregate per-frame GeoDF stats (mean reject ratio, guard %, sampson)."""
    stats = run_dir / "geo_df_stats.csv"
    if not stats.is_file():
        return None
    ratios: list[float] = []
    guard: list[int] = []
    rejected: list[int] = []
    mean_sampson: list[float] = []
    max_sampson: list[float] = []
    with stats.open() as f:
        reader = csv.DictReader(f)
        for row in reader:
            try:
                ratios.append(float(row.get("reject_ratio", row.get("rejection_ratio", 0.0))))
            except (TypeError, ValueError):
                continue
            try:
                guard.append(int(row.get("guard_triggered", 0)))
            except (TypeError, ValueError):
                guard.append(0)
            try:
                rejected.append(int(row.get("rejected", 0)))
            except (TypeError, ValueError):
                rejected.append(0)
            if row.get("mean_sampson") not in (None, ""):
                try:
                    mean_sampson.append(float(row["mean_sampson"]))
                except ValueError:
                    pass
            if row.get("max_sampson") not in (None, ""):
                try:
                    max_sampson.append(float(row["max_sampson"]))
                except ValueError:
                    pass
    if not ratios:
        return None
    n = len(ratios)
    return {
        "frames": n,
        "mean_reject_ratio": statistics.mean(ratios),
        "max_reject_ratio": max(ratios),
        "reject_frames_pct": 100.0 * sum(1 for r in rejected if r > 0) / n,
        "guard_pct": 100.0 * sum(guard) / n,
        "mean_sampson": statistics.mean(mean_sampson) if mean_sampson else float("nan"),
        "max_sampson": max(max_sampson) if max_sampson else float("nan"),
    }


def _fps_from_log(run_dir: Path) -> float | None:
    for name in ("pht_vio_node.log", "vins_node.log"):
        log = run_dir / name
        if not log.is_file():
            continue
        text = log.read_text(errors="replace")
        m = re.search(r"average process time\s+([0-9.]+)\s*ms", text, re.I)
        if m:
            ms = float(m.group(1))
            return 1000.0 / ms if ms > 0 else None
    return None


def _parse_run_name(name: str) -> dict[str, str]:
    out = {"sequence": "", "method": ""}
    m = re.match(
        r"^(MH_\d+_\w+?)_(baseline|geodf_hard|geodf_noguard|adaptive|alwayson|geodf_dump)(?:_s[\dp]+|_run\d+)?$",
        name,
    )
    if m:
        out["sequence"] = m.group(1)
        out["method"] = m.group(2)
    return out



def _find_run(runs: dict[str, dict], seq: str, method: str) -> dict | None:
    prefix = f"{seq}_{method}"
    for name, data in runs.items():
        if name == prefix or name.startswith(prefix + "_s"):
            return data
    return None

def _md_table(headers: list[str], rows: list[list[str]]) -> str:
    lines = [
        "| " + " | ".join(headers) + " |",
        "| " + " | ".join("---" for _ in headers) + " |",
    ]
    for row in rows:
        lines.append("| " + " | ".join(row) + " |")
    return "\n".join(lines)


def _fmt(v: float | None, prec: int = 3, suffix: str = "") -> str:
    if v is None:
        return "—"
    try:
        if v != v:  # NaN
            return "—"
    except TypeError:
        return "—"
    return f"{v:.{prec}f}{suffix}"


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("--root", type=Path, required=True)
    ap.add_argument("--out", type=Path, default=None)
    args = ap.parse_args()

    root: Path = args.root
    out_path = args.out or (root / "geodf_summary.md")

    runs: dict[str, dict] = {}
    for run_dir in sorted(root.iterdir()):
        if not run_dir.is_dir():
            continue
        ate = _ate_rmse(run_dir)
        if ate is None:
            continue
        runs[run_dir.name] = {
            "ate": ate,
            "geo": _geo_stats(run_dir),
            "fps": _fps_from_log(run_dir),
            **_parse_run_name(run_dir.name),
        }

    lines: list[str] = ["# GeoDF-VINS-Hard Study Summary", "", f"Root: `{root}`", ""]

    # --- Static sanity ---
    static_seqs = ["MH_01_easy", "MH_02_easy", "MH_03_medium", "MH_04_difficult", "MH_05_difficult"]
    static_rows: list[list[str]] = []
    for seq in static_seqs:
        base = _find_run(runs, seq, "baseline")
        geo = _find_run(runs, seq, "geodf_hard")
        if not base and not geo:
            continue
        base_ate = base["ate"] if base else None
        geo_ate = geo["ate"] if geo else None
        change = "—"
        pass_flag = "—"
        if base_ate and geo_ate:
            pct = (geo_ate - base_ate) / base_ate * 100.0
            change = f"{pct:+.1f}%"
            pass_flag = "PASS" if pct <= 20.0 else "FAIL"
        mrr = "—"
        if geo and geo["geo"]:
            mrr = _fmt(geo["geo"]["mean_reject_ratio"] * 100.0, 2, "%")
        static_rows.append([
            seq.replace("_", "\\_"),
            _fmt(base_ate),
            _fmt(geo_ate),
            change,
            mrr,
            pass_flag,
        ])
    if static_rows:
        lines += ["## 1. Static sanity check (ATE RMSE, m)", ""]
        lines.append(_md_table(
            ["Sequence", "Baseline ATE", "GeoDF-Hard ATE", "Change", "Mean reject ratio", "Pass (≤20%)"],
            static_rows,
        ))
        lines.append("")

    # --- Ablation ---
    ablation_rows: list[list[str]] = []
    for seq in ("MH_01_easy", "MH_03_medium"):
        for method, label in (
            ("baseline", "VINS-Fusion baseline"),
            ("geodf_noguard", "GeoDF-Hard no guard"),
            ("geodf_hard", "GeoDF-Hard with guard"),
        ):
            r = _find_run(runs, seq, method)
            if not r:
                continue
            geo = r["geo"]
            ablation_rows.append([
                seq.replace("_", "\\_"),
                label,
                _fmt(r["ate"]),
                _fmt(geo["mean_reject_ratio"] * 100.0, 2, "%") if geo else "—",
                _fmt(geo["guard_pct"], 1, "%") if geo else "—",
            ])
    if ablation_rows:
        lines += ["## 2. Ablation (Geo filter / hard reject / ratio guard)", ""]
        lines.append(_md_table(
            ["Sequence", "Method", "ATE RMSE", "Mean reject ratio", "Guard-triggered frames"],
            ablation_rows,
        ))
        lines.append("")

    out_path.write_text("\n".join(lines) + "\n")
    print(f"[ok] wrote {out_path}")
    # Echo static table to stdout for quick inspection.
    if static_rows:
        print("\n".join(lines[lines.index("## 1. Static sanity check (ATE RMSE, m)"):]))


if __name__ == "__main__":
    main()
