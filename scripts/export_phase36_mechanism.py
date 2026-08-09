#!/usr/bin/env python3
"""Export Phase 3.6 mechanism-isolation paired ATE table from a protocol tag.

Computes M0/M1/M2/M3(/Q3) means, paired deltas vs U+W, and a rough
ΔE_qm / ΔE_arb / ΔE_interaction decomposition when all four are present.
"""
from __future__ import annotations

import argparse
import json
import statistics
from collections import defaultdict
from pathlib import Path

import sys

sys.path.insert(0, str(Path(__file__).resolve().parent / "lib"))
from sem_geodf_ablation_common import load_run_record  # noqa: E402

METHODS = (
    "union_weight",
    "qm_only",
    "arbitration_only",
    "adaptive_arbitration_v2",
    "qm_stereo",
)


def collect(root: Path):
    cells = defaultdict(list)  # (scene, method) -> [ate]
    coverage = defaultdict(list)
    for metrics in root.rglob("metrics.json"):
        run_dir = metrics.parent.parent if metrics.parent.name == "eval" else metrics.parent
        rec = load_run_record(run_dir)
        if rec is None or rec.ate_rmse_m is None:
            continue
        if rec.method not in METHODS:
            continue
        cells[(rec.scene, rec.method)].append(rec.ate_rmse_m)
        cov = rec.metrics.get("trajectory_coverage")
        if cov is not None:
            coverage[(rec.scene, rec.method)].append(float(cov))
    return cells, coverage


def mean_std(vals):
    if not vals:
        return None, None
    if len(vals) == 1:
        return vals[0], 0.0
    return statistics.mean(vals), statistics.stdev(vals)


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--root", type=Path, required=True)
    ap.add_argument("--out", type=Path, default=None)
    args = ap.parse_args()

    cells, coverage = collect(args.root)
    scenes = sorted({s for s, _ in cells})
    rows = []
    for scene in scenes:
        row = {"scene": scene}
        for method in METHODS:
            vals = cells.get((scene, method), [])
            m, s = mean_std(vals)
            row[method] = {"n": len(vals), "mean": m, "std": s,
                           "values": vals}
            cov = coverage.get((scene, method), [])
            if cov:
                row[method]["coverage_mean"] = statistics.mean(cov)
        m0 = row.get("union_weight", {}).get("mean")
        m1 = row.get("qm_only", {}).get("mean")
        m2 = row.get("arbitration_only", {}).get("mean")
        m3 = row.get("adaptive_arbitration_v2", {}).get("mean")
        if None not in (m0, m1, m2, m3):
            d_qm = m1 - m0
            d_arb = m2 - m0
            d_full = m3 - m0
            row["delta"] = {
                "dE_qm": d_qm,
                "dE_arb": d_arb,
                "dE_full": d_full,
                "dE_interaction": d_full - d_qm - d_arb,
                "pct_qm": 100.0 * d_qm / m0 if m0 else None,
                "pct_arb": 100.0 * d_arb / m0 if m0 else None,
                "pct_full": 100.0 * d_full / m0 if m0 else None,
            }
        rows.append(row)

    out = {"root": str(args.root), "rows": rows}
    text = json.dumps(out, indent=2)
    if args.out:
        args.out.parent.mkdir(parents=True, exist_ok=True)
        args.out.write_text(text + "\n")
        # Also a markdown summary.
        md = args.out.with_suffix(".md")
        lines = [
            "# Phase 3.6 mechanism isolation",
            "",
            f"Root: `{args.root}`",
            "",
            "| Scene | M0 U+W | M1 q_m | M2 arb | M3 full | Q3 qm_stereo | ΔE_qm% | ΔE_arb% | ΔE_full% |",
            "|---|---:|---:|---:|---:|---:|---:|---:|---:|",
        ]
        for row in rows:
            def fmt(method):
                cell = row.get(method) or {}
                if cell.get("mean") is None:
                    return "—"
                return f"{cell['mean']:.6f}±{(cell.get('std') or 0):.6f} (n={cell['n']})"
            d = row.get("delta") or {}
            lines.append(
                f"| {row['scene']} | {fmt('union_weight')} | {fmt('qm_only')} | "
                f"{fmt('arbitration_only')} | {fmt('adaptive_arbitration_v2')} | "
                f"{fmt('qm_stereo')} | "
                f"{d.get('pct_qm', float('nan')) if d else float('nan'):+.2f} | "
                f"{d.get('pct_arb', float('nan')) if d else float('nan'):+.2f} | "
                f"{d.get('pct_full', float('nan')) if d else float('nan'):+.2f} |"
            )
        md.write_text("\n".join(lines) + "\n")
        print(f"wrote {args.out} and {md}")
    else:
        print(text)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
