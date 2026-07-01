#!/usr/bin/env python3
"""Aggregate per-trial resource.json into a baseline-vs-adaptive resource table.

Reports mean peak RSS, mean CPU%, and thread count per method across all trials,
so the paper can state end-to-end computational cost and show that GeoDF-Adaptive
does not materially increase memory/CPU over the baseline.
"""
from __future__ import annotations

import argparse
import glob
import json
import statistics as st
from pathlib import Path


def collect(root: Path, method_suffix: str) -> dict:
    peak_rss, mean_cpu, peak_cpu, threads = [], [], [], []
    n = 0
    for p in sorted(glob.glob(str(root / f"*_{method_suffix}" / "trial_*" / "resource.json"))):
        try:
            d = json.loads(Path(p).read_text())
        except Exception:
            continue
        if d.get("error"):
            continue
        n += 1
        if isinstance(d.get("peak_rss_mb"), (int, float)):
            peak_rss.append(d["peak_rss_mb"])
        if isinstance(d.get("mean_cpu_pct"), (int, float)):
            mean_cpu.append(d["mean_cpu_pct"])
        if isinstance(d.get("peak_cpu_pct"), (int, float)):
            peak_cpu.append(d["peak_cpu_pct"])
        if isinstance(d.get("peak_threads"), int):
            threads.append(d["peak_threads"])

    def ms(v):
        return (st.mean(v), st.pstdev(v) if len(v) > 1 else 0.0) if v else (None, None)

    return {
        "n_trials": n,
        "peak_rss_mb": ms(peak_rss),
        "mean_cpu_pct": ms(mean_cpu),
        "peak_cpu_pct": ms(peak_cpu),
        "peak_threads": max(threads) if threads else None,
    }


def _cell(pair):
    m, s = pair
    return f"{m:.0f}±{s:.0f}" if m is not None else "n/a"


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("--roots", nargs="+",
                    default=["results/viode_repeat", "results/euroc_repeat"])
    ap.add_argument("--out-md", type=Path, default=Path("results/geodf_evaluation/RESOURCE_TABLE.md"))
    ap.add_argument("--out-json", type=Path, default=Path("results/geodf_evaluation/resource_summary.json"))
    args = ap.parse_args()

    bundle = {}
    lines = [
        "# End-to-end computational cost (whole VIO process)\n",
        "Process-level resource use sampled at 2 Hz over every trial (all threads). "
        "Complements the in-pipeline GeoDF module time (Table 5): shows the proposed "
        "filter does not materially change memory/CPU vs baseline.\n",
        "| Dataset | Method | Trials | Peak RSS (MB) | Mean CPU (%) | Peak CPU (%) | Peak threads |",
        "|---|---|---:|---:|---:|---:|---:|",
    ]
    any_data = False
    for root in args.roots:
        rp = Path(root)
        label = rp.name.replace("_repeat", "")
        bundle[label] = {}
        for method in ("baseline", "adaptive"):
            r = collect(rp, method)
            bundle[label][method] = r
            if r["n_trials"]:
                any_data = True
            lines.append(
                f"| {label} | {method} | {r['n_trials']} | "
                f"{_cell(r['peak_rss_mb'])} | {_cell(r['mean_cpu_pct'])} | "
                f"{_cell(r['peak_cpu_pct'])} | {r['peak_threads'] if r['peak_threads'] else 'n/a'} |"
            )

    args.out_md.parent.mkdir(parents=True, exist_ok=True)
    args.out_md.write_text("\n".join(lines) + "\n")
    args.out_json.write_text(json.dumps(bundle, indent=2) + "\n")
    if any_data:
        print("\n".join(lines))
    else:
        print("[resource-summary] no resource.json found yet (runs not done)")
    print(f"[ok] wrote {args.out_md}")


if __name__ == "__main__":
    main()
