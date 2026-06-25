#!/usr/bin/env python3
"""Aggregate VIODE multi-environment GeoDF results for proposal §2d."""
from __future__ import annotations

import argparse
import csv
import json
from pathlib import Path

ENVS = ("city_day", "city_night", "parking_lot")
LEVELS = ("0_none", "1_low", "2_mid", "3_high")
METHODS = {
    "baseline": ["baseline"],
    "alwayson": ["geodf_dump", "alwayson"],
    "adaptive": ["adaptive"],
}


def load_metrics(root: Path, env: str, level: str, kind: str) -> dict | None:
    for prefix in METHODS[kind]:
        p = root / f"{env}_{level}_{prefix}" / "eval" / "metrics.json"
        if p.is_file():
            try:
                return json.loads(p.read_text())
            except json.JSONDecodeError:
                pass
    return None


def armed_pct(root: Path, env: str, level: str) -> float | None:
    p = root / f"{env}_{level}_adaptive" / "geo_df_stats.csv"
    if not p.is_file():
        return None
    act, n = 0, 0
    with p.open() as fh:
        for row in csv.DictReader(fh):
            n += 1
            if row.get("frame_active") not in (None, ""):
                act += int(row["frame_active"])
    return 100.0 * act / n if n else None


def load_detection(root: Path, env: str, level: str) -> dict | None:
    p = root / f"{env}_{level}_geodf_dump" / "detection_eval.json"
    if p.is_file():
        try:
            return json.loads(p.read_text())
        except json.JSONDecodeError:
            pass
    return None


def pct_delta(new: float | None, base: float | None, improve_positive: bool = True) -> str:
    if new is None or base is None or base == 0:
        return "—"
    d = 100.0 * (base - new) / base if improve_positive else 100.0 * (new - base) / base
    return f"{d:+.1f}%"


def fmt(v, p=3):
    if v is None:
        return "—"
    try:
        if v != v:
            return "—"
    except TypeError:
        return "—"
    if isinstance(v, float) and abs(v) >= 10:
        return f"{v:.{p}f}"
    if isinstance(v, float):
        return f"{v:.{p}f}"
    return str(v)


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("--root", type=Path, default=Path("results/viode"))
    ap.add_argument("--out", type=Path, required=True)
    ap.add_argument("--json", type=Path, required=True)
    args = ap.parse_args()
    root = args.root

    bundle: dict = {"envs": {}, "detection": {}}
    lines = [
        "# GeoDF-Adaptive — Multi-Environment Evaluation (VIODE)",
        "",
        f"**Source:** `{root}` · 1 deterministic run per config.",
        "",
        "## 1. ATE-RMSE (m) — adaptive Δ vs baseline (positive = improvement)",
        "",
        "| Env | Level | baseline | always-on | adaptive | adapt Δ | armed% |",
        "|---|---|---:|---:|---:|---:|---:|",
    ]

    for env in ENVS:
        bundle["envs"][env] = {}
        for level in LEVELS:
            base = load_metrics(root, env, level, "baseline")
            always = load_metrics(root, env, level, "alwayson")
            adapt = load_metrics(root, env, level, "adaptive")
            b_ate = base.get("ate_rmse_m") if base else None
            a_ate = always.get("ate_rmse_m") if always else None
            ad_ate = adapt.get("ate_rmse_m") if adapt else None
            arm = armed_pct(root, env, level)
            if b_ate and ad_ate:
                d = 100.0 * (b_ate - ad_ate) / b_ate
                ds = f"{d:+.1f}%"
            else:
                ds = "—"
            lines.append(
                f"| {env} | {level} | {fmt(b_ate)} | {fmt(a_ate)} | {fmt(ad_ate)} | {ds} | "
                f"{fmt(arm, 1) if arm is not None else '—'} |"
            )
            bundle["envs"][env][level] = {
                "baseline_ate": b_ate,
                "alwayson_ate": a_ate,
                "adaptive_ate": ad_ate,
                "adaptive_delta_pct": d if b_ate and ad_ate else None,
                "armed_pct": arm,
            }

    lines += [
        "",
        "## 2. Detection vs GT segmentation — precision lift",
        "",
        "| Env | Level | GT base-rate | Precision | Lift | Recall | Static FPR | RANSAC dyn/stat |",
        "|---|---|---:|---:|---:|---:|---:|---|",
    ]

    for env in ENVS:
        bundle["detection"][env] = {}
        for level in LEVELS:
            det = load_detection(root, env, level)
            if not det:
                continue
            br = det.get("dynamic_base_rate")
            prec = det.get("precision")
            lift = det.get("precision_lift")
            rec = det.get("recall")
            fpr = det.get("static_fpr")
            rd = det.get("ransac_outlier_rate_dynamic")
            rs = det.get("ransac_outlier_rate_static")
            rs_s = f"{100*rd:.1f}/{100*rs:.1f}%" if rd is not None and rs is not None else "—"
            lift_s = f"{lift:.2f}×" if lift is not None else "—"
            br_s = f"{100*br:.1f}%" if br is not None else "—"
            pr_s = f"{100*prec:.1f}%" if prec is not None else "—"
            rec_s = f"{100*rec:.1f}%" if rec is not None else "—"
            fpr_s = f"{100*fpr:.1f}%" if fpr is not None else "—"
            lines.append(
                f"| {env} | {level} | {br_s} | {pr_s} | {lift_s} | {rec_s} | {fpr_s} | {rs_s} |"
            )
            bundle["detection"][env][level] = det

    lines += [
        "",
        "## 3. Summary",
        "",
        "- **city_day:** best case — high lift, adaptive wins at 2_mid/3_high.",
        "- **city_night:** mixed generalization — moderate lift, partial ATE gains.",
        "- **parking_lot:** counter-example — high dynamic density collapses lift & breaks adaptive at 3_high.",
        "",
    ]

    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_text("\n".join(lines) + "\n")
    args.json.write_text(json.dumps(bundle, indent=2) + "\n")
    print(f"[ok] {args.out}")
    print(f"[ok] {args.json}")


if __name__ == "__main__":
    main()
