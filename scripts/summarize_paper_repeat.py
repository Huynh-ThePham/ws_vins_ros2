#!/usr/bin/env python3
"""Aggregate paper repeat trials (N runs/seq) into mean±std tables + JSON."""
from __future__ import annotations

import argparse
import csv
import glob
import json
import os
import statistics as st
from pathlib import Path

WS = Path(__file__).resolve().parents[1]

EUROC_SEQS = ["MH_01_easy", "MH_02_easy", "MH_03_medium", "MH_04_difficult", "MH_05_difficult"]
EUROC_METHODS = ["baseline", "alwayson", "adaptive", "adaptive_fixed"]
VIODE_ENVS = ["city_day", "city_night", "parking_lot"]
VIODE_LEVELS = ["0_none", "1_low", "2_mid", "3_high"]
VIODE_METHODS = ["baseline", "geodf_dump", "adaptive", "adaptive_fixed"]
METHOD_LABEL = {
    "baseline": "baseline",
    "alwayson": "always-on",
    "geodf_dump": "always-on",
    "adaptive": "**adaptive (PROPOSED)**",
    "adaptive_fixed": "adaptive (fixed ρ)",
}


def load_metric(path: Path, key: str = "ate_rmse_m"):
    try:
        return json.loads(path.read_text()).get(key)
    except Exception:
        return None


def trial_values(root: Path, pattern: str, key: str = "ate_rmse_m"):
    vals = []
    for p in sorted(glob.glob(str(root / pattern))):
        v = load_metric(Path(p), key)
        if isinstance(v, (int, float)):
            vals.append(float(v))
    return vals


def stats(vals):
    if not vals:
        return None, None, 0
    m = st.mean(vals)
    s = st.pstdev(vals) if len(vals) > 1 else 0.0
    return m, s, len(vals)


def fmt_stats(vals):
    m, s, n = stats(vals)
    if m is None:
        return "n/a", None, None, 0
    return f"{m:.3f}±{s:.3f}", m, s, n


def armed_pct(trial_dir: Path):
    f = trial_dir / "geo_df_stats.csv"
    if not f.exists():
        return None
    act, n, has = 0, 0, False
    with f.open() as fh:
        for row in csv.DictReader(fh):
            n += 1
            if row.get("frame_active") not in (None, ""):
                has = True
                act += int(row["frame_active"])
    return 100.0 * act / n if (n and has) else None


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--root", type=Path, default=WS / "results" / "paper_repeat")
    ap.add_argument("--trials", type=int, default=3)
    ap.add_argument("--out-md", type=Path, default=WS / "results" / "geodf_evaluation" / "PAPER_RESULTS.md")
    ap.add_argument("--out-json", type=Path, default=WS / "results" / "geodf_evaluation" / "paper_repeat_summary.json")
    args = ap.parse_args()
    root = args.root

    bundle = {"trials_per_seq": args.trials, "euroc": {}, "viode": {}, "detection": {}}
    lines = [
        "# Paper results — mean±std over N trials per sequence",
        "",
        f"N = **{args.trials}** trials per (sequence, method). Method **adaptive** = GeoDF-Adaptive PROPOSED (auto-ρ_on).",
        f"Source: `{root}`",
        "",
        "## 1. EuRoC static (ATE-RMSE m)",
        "",
        "| Seq | baseline | always-on | **adaptive (PROPOSED)** | adaptive (fixed ρ) |",
        "|---|---:|---:|---:|---:|",
    ]

    for seq in EUROC_SEQS:
        row = {"seq": seq, "methods": {}}
        cells = [seq]
        for m in EUROC_METHODS:
            vals = trial_values(root / "euroc", f"{seq}_{m}/trial_*/eval/metrics.json")
            txt, mean, std, n = fmt_stats(vals)
            row["methods"][m] = {"mean": mean, "std": std, "n": n, "values": vals}
            cells.append(txt if txt != "n/a" else "n/a")
        bundle["euroc"][seq] = row
        lines.append("| " + " | ".join(cells) + " |")

    lines += ["", "## 2. VIODE trajectory (ATE-RMSE m)", ""]

    for env in VIODE_ENVS:
        lines.append(f"### {env}")
        lines.append("")
        hdr = "| level | baseline | always-on | **adaptive (PROPOSED)** | adaptive (fixed ρ) | adapt Δ vs base |"
        lines.append(hdr)
        lines.append("|" + "---|" * (len(hdr.split("|")) - 2))
        bundle["viode"][env] = {}

        for lv in VIODE_LEVELS:
            row = {"level": lv, "methods": {}}
            cells = [lv]
            means = {}
            for m in VIODE_METHODS:
                vals = trial_values(root / "viode", f"{env}_{lv}_{m}/trial_*/eval/metrics.json")
                txt, mean, std, n = fmt_stats(vals)
                row["methods"][m] = {"mean": mean, "std": std, "n": n, "values": vals}
                means[m] = mean
                cells.append(txt if txt != "n/a" else "n/a")
            base, prop = means.get("baseline"), means.get("adaptive")
            if isinstance(base, float) and isinstance(prop, float) and base:
                delta = f"{(base - prop) / base * 100:+.1f}%"
            else:
                delta = "n/a"
            cells.append(delta)
            bundle["viode"][env][lv] = row
            lines.append("| " + " | ".join(cells) + " |")
        lines.append("")

    lines += ["## 3. Detection (always-on dump, trial_1)", ""]
    lines.append("| env | level | precision | recall | lift | static-FPR |")
    lines.append("|---|---|---:|---:|---:|---:|")

    for env in VIODE_ENVS:
        bundle["detection"][env] = {}
        for lv in VIODE_LEVELS:
            det = root / "viode" / f"{env}_{lv}_geodf_dump" / "trial_1" / "detection_eval.json"
            if det.exists():
                d = json.loads(det.read_text())
                bundle["detection"][env][lv] = d
                lines.append(
                    f"| {env} | {lv} | {d.get('precision', 'n/a'):.3f} | "
                    f"{d.get('recall') if d.get('recall') is not None else 'n/a'} | "
                    f"{d.get('precision_lift', 'n/a')} | {d.get('static_fpr', 'n/a'):.4f} |"
                    if isinstance(d.get("precision"), (int, float))
                    else f"| {env} | {lv} | n/a | n/a | n/a | n/a |"
                )

    args.out_md.parent.mkdir(parents=True, exist_ok=True)
    args.out_md.write_text("\n".join(lines) + "\n")
    args.out_json.write_text(json.dumps(bundle, indent=2) + "\n")
    print("\n".join(lines[:40]))
    print(f"\n... -> {args.out_md}")
    print(f"JSON -> {args.out_json}")


if __name__ == "__main__":
    main()
