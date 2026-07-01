#!/usr/bin/env python3
"""Aggregate geo_ms / frame_active from all adaptive repeat trials (Table 5)."""
from __future__ import annotations

import argparse
import csv
import glob
import json
import statistics as st
from pathlib import Path


def load_stats(csv_path: Path) -> list[dict]:
    rows = []
    with csv_path.open() as f:
        for row in csv.DictReader(f):
            rows.append(row)
    return rows


def aggregate(root: Path, pattern: str = "*_adaptive/trial_*/geo_df_stats.csv") -> dict:
    geo_ms: list[float] = []
    active = total = 0
    trials = 0
    for p in sorted(glob.glob(str(root / pattern))):
        rows = load_stats(Path(p))
        if not rows:
            continue
        trials += 1
        for r in rows:
            total += 1
            if r.get("frame_active") in ("1", "1.0", 1):
                active += 1
            try:
                v = float(r.get("geo_ms") or 0)
                if v > 0:
                    geo_ms.append(v)
            except (TypeError, ValueError):
                pass
    if not geo_ms:
        return {"trials": trials, "frames": total}
    geo_ms.sort()

    def pct(p: float) -> float:
        if not geo_ms:
            return 0.0
        k = (len(geo_ms) - 1) * p / 100.0
        f = int(k)
        c = min(f + 1, len(geo_ms) - 1)
        if f == c:
            return geo_ms[f]
        return geo_ms[f] + (geo_ms[c] - geo_ms[f]) * (k - f)

    return {
        "trials": trials,
        "frames": total,
        "mean_geo_ms": st.mean(geo_ms),
        "median_geo_ms": st.median(geo_ms),
        "p95_geo_ms": pct(95),
        "p99_geo_ms": pct(99),
        "pct_of_50ms_budget": st.mean(geo_ms) / 50.0 * 100.0,
        "frames_rejection_armed_pct": 100.0 * active / total if total else 0.0,
    }


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("--root", type=Path, default=Path("results/viode_repeat"))
    ap.add_argument("--out-json", type=Path, default=Path("results/geodf_evaluation/runtime_summary.json"))
    ap.add_argument("--out-md", type=Path, default=Path("results/geodf_evaluation/RUNTIME_TABLE.md"))
    args = ap.parse_args()

    m = aggregate(args.root)
    args.out_json.parent.mkdir(parents=True, exist_ok=True)
    args.out_json.write_text(json.dumps(m, indent=2) + "\n")

    if "mean_geo_ms" not in m:
        print(f"[runtime] no geo_df_stats in {args.root}")
        return

    lines = [
        "# GeoDF-Adaptive runtime (aggregated from repeat trials)\n",
        "| Metric | Value |",
        "|---|---:|",
        f"| Mean per-frame cost | {m['mean_geo_ms']:.2f} ms |",
        f"| Median per-frame cost | {m['median_geo_ms']:.2f} ms |",
        f"| 95th percentile | {m['p95_geo_ms']:.2f} ms |",
        f"| 99th percentile | {m['p99_geo_ms']:.2f} ms |",
        f"| Fraction of 50 ms (20 Hz) budget | {m['pct_of_50ms_budget']:.2f}% |",
        f"| Frames with rejection armed | {m['frames_rejection_armed_pct']:.1f}% |",
        f"| Logged frames | {m['frames']:,} ({m['trials']} adaptive trials) |",
        "",
    ]
    args.out_md.write_text("\n".join(lines))
    print("\n".join(lines))
    print(f"[ok] {args.out_json}  {args.out_md}")


if __name__ == "__main__":
    main()
