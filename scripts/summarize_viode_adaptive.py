#!/usr/bin/env python3
"""3-way VIODE summary: baseline vs always-on vs adaptive GeoDF.

Reads <root>/<env>_<level>_<method>/eval/metrics.json for methods:
  baseline, geodf_dump|alwayson, adaptive
plus per-run geo_df_stats.csv to report mean reject ratio and (adaptive) the
fraction of frames the scene-aware gate was armed.
"""
from __future__ import annotations

import argparse
import csv
import json
from pathlib import Path


def metrics(root: Path, env: str, level: str, prefixes: list[str]) -> dict | None:
    for p in prefixes:
        f = root / f"{env}_{level}_{p}" / "eval" / "metrics.json"
        if f.exists():
            try:
                return json.loads(f.read_text())
            except json.JSONDecodeError:
                pass
    return None


def stats_csv(root: Path, env: str, level: str, prefixes: list[str]) -> Path | None:
    for p in prefixes:
        f = root / f"{env}_{level}_{p}" / "geo_df_stats.csv"
        if f.exists():
            return f
    return None


def reject_and_active(csv_path: Path | None) -> tuple[float | None, float | None]:
    if not csv_path or not csv_path.exists():
        return None, None
    rr, act, n = [], 0, 0
    with csv_path.open() as fh:
        for row in csv.DictReader(fh):
            n += 1
            try:
                rr.append(float(row["reject_ratio"]))
            except (KeyError, ValueError):
                pass
            if row.get("frame_active") not in (None, ""):
                act += int(row["frame_active"])
    mr = sum(rr) / len(rr) if rr else None
    af = act / n if n and "frame_active" in row else None
    return mr, af


def f(v, p=3):
    return f"{v:.{p}f}" if isinstance(v, (int, float)) else "—"


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("--root", type=Path, default=Path("results/viode"))
    ap.add_argument("--env", default="city_day")
    ap.add_argument("--levels", default="0_none 1_low 2_mid 3_high")
    args = ap.parse_args()

    methods = [("baseline", ["baseline"]),
               ("always-on", ["geodf_dump", "alwayson", "geodf"]),
               ("adaptive", ["adaptive", "geodf_adaptive"])]

    lines = [f"# VIODE {args.env}: baseline vs always-on vs adaptive GeoDF", "",
             "ATE/RPE vs `/odometry` (evo SE(3)). Adaptive = scene-aware self-gating "
             "(only hard-reject on dynamic frames).", "",
             "## ATE-RMSE (m) — global accuracy",
             "| Level | baseline | always-on | adaptive | adapt Δ vs base | gate armed% |",
             "|---|---:|---:|---:|---:|---:|"]
    bundle = {}
    for level in args.levels.split():
        ms = {lab: metrics(args.root, args.env, level, pf) for lab, pf in methods}
        _, armed = reject_and_active(stats_csv(args.root, args.env, level, ["adaptive", "geodf_adaptive"]))
        b = ms["baseline"].get("ate_rmse_m") if ms["baseline"] else None
        a = ms["always-on"].get("ate_rmse_m") if ms["always-on"] else None
        d = ms["adaptive"].get("ate_rmse_m") if ms["adaptive"] else None
        impr = (100.0 * (b - d) / b) if isinstance(b, (int, float)) and isinstance(d, (int, float)) and b > 0 else None
        bundle[level] = {"baseline": ms["baseline"], "always_on": ms["always-on"],
                         "adaptive": ms["adaptive"], "gate_armed_frac": armed}
        lines.append(f"| {level} | {f(b)} | {f(a)} | {f(d)} | "
                     f"{(f'{impr:+.1f}%' if isinstance(impr,float) else '—')} | "
                     f"{(f'{100*armed:.1f}%' if isinstance(armed,float) else '—')} |")

    lines += ["", "## ATE-max (m) / RPE-RMSE (m)",
              "| Level | base max | always max | adapt max | base RPE | always RPE | adapt RPE |",
              "|---|---:|---:|---:|---:|---:|---:|"]
    for level in args.levels.split():
        d = bundle[level]
        def g(side, k):
            return d[side].get(k) if d[side] else None
        lines.append(f"| {level} | {f(g('baseline','ate_max_m'))} | {f(g('always_on','ate_max_m'))} | "
                     f"{f(g('adaptive','ate_max_m'))} | {f(g('baseline','rpe_rmse_m'))} | "
                     f"{f(g('always_on','rpe_rmse_m'))} | {f(g('adaptive','rpe_rmse_m'))} |")

    out = args.root / f"viode_{args.env}_adaptive.md"
    out.write_text("\n".join(lines) + "\n")
    out.with_suffix(".json").write_text(json.dumps(bundle, indent=2) + "\n")
    print("\n".join(lines))
    print(f"\n[ok] -> {out}")


if __name__ == "__main__":
    main()
