#!/usr/bin/env python3
"""Summarize EuRoC static ablation: baseline vs always-on vs adaptive.

Reads results/geodf/<seq>_<method>_s<start>/eval/metrics.json and the per-run
geo_df_stats.csv (for mean reject ratio + adaptive armed%).
"""
from __future__ import annotations

import argparse
import csv
import glob
import json
from pathlib import Path

START = {"MH_01_easy": "40", "MH_02_easy": "35", "MH_03_medium": "17p5",
         "MH_04_difficult": "15", "MH_05_difficult": "15"}


def find_run(root: Path, seq: str, method: str) -> Path | None:
    # method dir token: baseline | alwayson | adaptive
    pat = str(root / f"{seq}_{method}_s*")
    hits = sorted(glob.glob(pat))
    return Path(hits[0]) if hits else None


def metrics(d: Path | None):
    if not d:
        return None
    f = d / "eval" / "metrics.json"
    if f.exists():
        try:
            return json.loads(f.read_text())
        except json.JSONDecodeError:
            return None
    return None


def reject_armed(d: Path | None):
    if not d:
        return None, None
    f = d / "geo_df_stats.csv"
    if not f.exists():
        return None, None
    rr, act, n, has_active = [], 0, 0, False
    with f.open() as fh:
        for row in csv.DictReader(fh):
            n += 1
            try:
                rr.append(float(row["reject_ratio"]))
            except (KeyError, ValueError):
                pass
            if row.get("frame_active") not in (None, ""):
                has_active = True
                act += int(row["frame_active"])
    mr = sum(rr) / len(rr) if rr else None
    af = act / n if (n and has_active) else None
    return mr, af


def fmt(v, p=3):
    return f"{v:.{p}f}" if isinstance(v, (int, float)) else "—"


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("--root", type=Path, default=Path("results/geodf"))
    ap.add_argument("--seqs", default="MH_01_easy MH_02_easy MH_03_medium MH_04_difficult MH_05_difficult")
    args = ap.parse_args()

    lines = ["# EuRoC static ablation: baseline vs always-on vs adaptive GeoDF", "",
             "Original EuRoC machine-hall sequences (no dynamic objects). ATE-RMSE [m] "
             "vs `state_groundtruth_estimate0` (evo SE(3)). Tests that the filter "
             "does not degrade static accuracy; adaptive should match baseline by "
             "passing through (gate stays disarmed).", "",
             "| Seq | baseline | always-on | adaptive | always Δ | adapt Δ | armed% |",
             "|---|---:|---:|---:|---:|---:|---:|"]
    bundle = {}
    n_adapt_ok = 0
    n_always_bad = 0
    for seq in args.seqs.split():
        b = metrics(find_run(args.root, seq, "baseline"))
        a = metrics(find_run(args.root, seq, "alwayson"))
        d = metrics(find_run(args.root, seq, "adaptive"))
        _, armed = reject_armed(find_run(args.root, seq, "adaptive"))
        bv = b.get("ate_rmse_m") if b else None
        av = a.get("ate_rmse_m") if a else None
        dv = d.get("ate_rmse_m") if d else None
        ad = (100 * (av - bv) / bv) if isinstance(bv, (int, float)) and isinstance(av, (int, float)) and bv else None
        dd = (100 * (dv - bv) / bv) if isinstance(bv, (int, float)) and isinstance(dv, (int, float)) and bv else None
        if isinstance(dd, float) and isinstance(ad, float):
            if abs(dd) <= abs(ad) + 1e-9:
                n_adapt_ok += 1
            if ad > 0.5:
                n_always_bad += 1
        bundle[seq] = {"baseline": b, "always_on": a, "adaptive": d, "armed": armed}
        lines.append(f"| {seq} | {fmt(bv)} | {fmt(av)} | {fmt(dv)} | "
                     f"{(f'{ad:+.1f}%' if isinstance(ad,float) else '—')} | "
                     f"{(f'{dd:+.1f}%' if isinstance(dd,float) else '—')} | "
                     f"{(f'{100*armed:.1f}%' if isinstance(armed,float) else '—')} |")

    lines += ["",
              f"- Adaptive ≤ always-on (closer to baseline) in **{n_adapt_ok}/{len(args.seqs.split())}** seqs.",
              f"- Always-on degraded static (>0.5%) in **{n_always_bad}** seqs (the cost adaptive removes).",
              ""]
    out = args.root / "euroc_static_ablation.md"
    out.write_text("\n".join(lines) + "\n")
    out.with_suffix(".json").write_text(json.dumps(bundle, indent=2) + "\n")
    print("\n".join(lines))
    print(f"\n[ok] -> {out}")


if __name__ == "__main__":
    main()
