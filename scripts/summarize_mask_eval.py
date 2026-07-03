#!/usr/bin/env python3
"""Summarize feature-level mask eval: adaptive_2d vs adaptive (stereo 3D)."""
from __future__ import annotations

import argparse
import json
from pathlib import Path

ENVS = ["city_day", "city_night", "parking_lot"]
LEVELS = ["0_none", "1_low", "2_mid", "3_high"]
METHODS = ["adaptive_2d", "adaptive"]
LABELS = {"adaptive_2d": "Old 2D-F", "adaptive": "New stereo 3D"}


def load_json(p: Path) -> dict | None:
    if not p.is_file():
        return None
    try:
        return json.loads(p.read_text())
    except Exception:
        return None


def fmt_pct(v) -> str:
    if not isinstance(v, (int, float)):
        return "n/a"
    return f"{v * 100:.2f}%"


def fmt_lift(v) -> str:
    if not isinstance(v, (int, float)):
        return "n/a"
    return f"{v:.2f}x"


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("--root", default="results/viode_detection")
    ap.add_argument("--out", default="results/geodf_evaluation/MASK_EVAL_2D_vs_3D.md")
    args = ap.parse_args()
    root = Path(args.root)

    lines = [
        "# Feature-level mask eval — 2D-F vs stereo 3D (VIODE vehicle masks)",
        "",
        "Ground truth: VIODE segmentation (moving vehicles). Prediction: GeoDF hard-reject.",
        "One dump run per (env, level, method).",
        "",
        "| env | level | method | precision | recall | lift | static-FPR | dyn base-rate |",
        "|---|---|---|---:|---:|---:|---:|---:|",
    ]
    bundle: dict = {"cells": {}}
    wins_3d = 0
    compared = 0

    for env in ENVS:
        for level in LEVELS:
            row_data = {}
            for m in METHODS:
                p = root / f"{env}_{level}_{m}_dump" / "detection_eval.json"
                d = load_json(p)
                row_data[m] = d
                if d:
                    lines.append(
                        f"| {env} | {level} | {LABELS[m]} | {fmt_pct(d.get('precision'))} | "
                        f"{fmt_pct(d.get('recall'))} | {fmt_lift(d.get('precision_lift'))} | "
                        f"{fmt_pct(d.get('static_fpr'))} | {fmt_pct(d.get('dynamic_base_rate'))} |"
                    )
                else:
                    lines.append(f"| {env} | {level} | {LABELS[m]} | n/a | n/a | n/a | n/a | n/a |")

            d2 = row_data.get("adaptive_2d")
            d3 = row_data.get("adaptive")
            lift_cmp = None
            if d2 and d3:
                p2, p3 = d2.get("precision"), d3.get("precision")
                if isinstance(p2, (int, float)) and isinstance(p3, (int, float)) and p2 > 0:
                    compared += 1
                    if p3 >= p2:
                        wins_3d += 1
                    lift_cmp = p3 / p2
            bundle["cells"][f"{env}_{level}"] = {
                "adaptive_2d": d2, "adaptive": d3, "precision_ratio_3d_over_2d": lift_cmp,
            }

    lines += [
        "",
        "## Summary",
        "",
        f"- Cells with both methods: {compared}/12",
        f"- Stereo 3D precision ≥ 2D-F: {wins_3d}/{compared or 'n/a'}",
        "",
        "**Claim support:** rejections align with VIODE dynamic masks; compare lift/precision "
        "between 2D-F and stereo 3D gates on the same adaptive activation stack.",
    ]

    out = Path(args.out)
    out.parent.mkdir(parents=True, exist_ok=True)
    out.write_text("\n".join(lines) + "\n")
    json_path = out.with_suffix(".json")
    json_path.write_text(json.dumps(bundle, indent=2) + "\n")
    print(f"[ok] {out}")


if __name__ == "__main__":
    main()
