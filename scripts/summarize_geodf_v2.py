#!/usr/bin/env python3
"""Aggregate the GeoDF v2 (B auto-rho + F stereo cross-check) comparison.

Produces a markdown report comparing:
  - Trajectory ATE/RPE: baseline vs adaptive(v1) vs adaptive_v2 (VIODE) and
    baseline vs adaptive_v2 (EuRoC static no-regression check).
  - Detection quality: always-on dump v1 (left-only) vs v2 (left OR stereo)
    precision / recall / lift / static-FPR vs simulator GT masks.
  - Mechanistic evidence from adaptive_v2 geo_df_stats.csv: auto-calibrated
    rho_on per scene, outlier floor, activation rate, stereo-added candidates.
"""
import argparse
import csv
import glob
import json
import os
import statistics as st

WS = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def load_json(path):
    try:
        with open(path) as fh:
            return json.load(fh)
    except Exception:
        return None


def ate(env_root, run, key="ate_rmse_m"):
    d = load_json(os.path.join(env_root, run, "eval", "metrics.json"))
    return d.get(key) if d else None


def det(env_root, run, key):
    d = load_json(os.path.join(env_root, run, "detection_eval.json"))
    return d.get(key) if d else None


def stats_summary(env_root, run):
    """Aggregate geo_df_stats.csv for an adaptive_v2 run."""
    p = os.path.join(env_root, run, "geo_df_stats.csv")
    if not os.path.exists(p):
        return None
    rows = list(csv.DictReader(open(p)))
    if not rows:
        return None

    def col(k):
        out = []
        for r in rows:
            v = r.get(k)
            if v not in (None, "", "nan"):
                try:
                    out.append(float(v))
                except ValueError:
                    pass
        return out

    fa = col("frame_active")
    ro = col("rho_on")
    fl = col("outlier_floor")
    sa = col("stereo_added")
    rr = col("reject_ratio")
    gm = col("geo_ms")
    return {
        "frames": len(rows),
        "armed_pct": 100.0 * sum(fa) / len(fa) if fa else None,
        "rho_on_mean": sum(ro) / len(ro) if ro else None,
        "rho_on_max": max(ro) if ro else None,
        "floor_mean": sum(fl) / len(fl) if fl else None,
        "floor_max": max(fl) if fl else None,
        "stereo_added_total": int(sum(sa)) if sa else 0,
        "stereo_added_frames": sum(1 for x in sa if x > 0) if sa else 0,
        "reject_ratio_mean": sum(rr) / len(rr) if rr else None,
        "geo_ms_mean": sum(gm) / len(gm) if gm else None,
        "geo_ms_p95": (sorted(gm)[int(0.95 * len(gm))] if gm else None),
    }


def fnum(x, nd=3):
    return f"{x:.{nd}f}" if isinstance(x, (int, float)) else "n/a"


def pct(new, base):
    if not isinstance(new, (int, float)) or not isinstance(base, (int, float)) or base == 0:
        return "n/a"
    return f"{(base - new) / base * 100:+.1f}%"  # positive = improvement (lower ATE)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--viode-root", default=os.path.join(WS, "results", "viode"))
    ap.add_argument("--geodf-root", default=os.path.join(WS, "results", "geodf"))
    ap.add_argument("--envs", default="city_day city_night parking_lot")
    ap.add_argument("--levels", default="0_none 1_low 2_mid 3_high")
    ap.add_argument("--euroc", default="MH_01_easy MH_02_easy MH_03_medium MH_04_difficult MH_05_difficult")
    ap.add_argument("--out", default=os.path.join(WS, "results", "geodf_evaluation", "V2_COMPARISON.md"))
    args = ap.parse_args()

    envs = args.envs.split()
    levels = args.levels.split()
    L = []
    L.append("# GeoDF-VINS-Hard v2 comparison — (B) auto rho_on + (F) stereo cross-check")
    L.append("")
    L.append("Single-session run (baseline / adaptive-v1 / adaptive-v2) so trajectory deltas "
             "are internally consistent. v2 = auto-calibrated activation threshold + right-view "
             "temporal epipolar cross-check (OR fusion, right Sampson gate = 6.0).")
    L.append("")

    # ---- Trajectory ATE ----
    L.append("## 1. VIODE trajectory ATE-RMSE (m) — lower is better")
    L.append("")
    L.append("| env | level | baseline | adaptive v1 | adaptive v2 | v2 Δ vs base | v2 Δ vs v1 |")
    L.append("|---|---|---:|---:|---:|---:|---:|")
    for env in envs:
        er = args.viode_root
        for lv in levels:
            b = ate(er, f"{env}_{lv}_baseline")
            a1 = ate(er, f"{env}_{lv}_adaptive")
            a2 = ate(er, f"{env}_{lv}_adaptive_v2")
            L.append(f"| {env} | {lv} | {fnum(b)} | {fnum(a1)} | {fnum(a2)} | "
                     f"{pct(a2, b)} | {pct(a2, a1)} |")
    L.append("")

    # ---- Detection ----
    L.append("## 2. Detection quality vs simulator GT (always-on dump): v1 vs v2")
    L.append("")
    L.append("v1 = left temporal epipolar only. v2 = left OR right-view stereo cross-check.")
    L.append("")
    L.append("| env | level | recall v1 | recall v2 | prec v1 | prec v2 | lift v1 | lift v2 | "
             "TP v1 | TP v2 | static-FPR v1 | static-FPR v2 |")
    L.append("|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|")
    for env in envs:
        er = args.viode_root
        for lv in levels:
            d1, d2 = f"{env}_{lv}_geodf_dump", f"{env}_{lv}_geodf_dump_v2"
            L.append(
                f"| {env} | {lv} | {fnum(det(er,d1,'recall'))} | {fnum(det(er,d2,'recall'))} | "
                f"{fnum(det(er,d1,'precision'))} | {fnum(det(er,d2,'precision'))} | "
                f"{fnum(det(er,d1,'precision_lift'),1)} | {fnum(det(er,d2,'precision_lift'),1)} | "
                f"{det(er,d1,'tp')} | {det(er,d2,'tp')} | "
                f"{fnum(det(er,d1,'static_fpr'),4)} | {fnum(det(er,d2,'static_fpr'),4)} |")
    L.append("")

    # ---- Mechanistic evidence (adaptive_v2 stats) ----
    L.append("## 3. Auto-calibration & stereo-cue evidence (adaptive_v2 geo_df_stats)")
    L.append("")
    L.append("rho_on is computed per-frame as floor*1.8 + 0.05 (clamped [0.10, 0.40]); "
             "stereo-added = candidates contributed by the right-view cross-check.")
    L.append("")
    L.append("| env | level | armed % | rho_on (mean/max) | outlier floor (mean/max) | "
             "stereo-added (total/frames) | geo ms (mean/p95) |")
    L.append("|---|---|---:|---:|---:|---:|---:|")
    for env in envs:
        er = args.viode_root
        for lv in levels:
            s = stats_summary(er, f"{env}_{lv}_adaptive_v2")
            if not s:
                L.append(f"| {env} | {lv} | n/a | n/a | n/a | n/a | n/a |")
                continue
            L.append(
                f"| {env} | {lv} | {fnum(s['armed_pct'],1)} | "
                f"{fnum(s['rho_on_mean'])}/{fnum(s['rho_on_max'])} | "
                f"{fnum(s['floor_mean'])}/{fnum(s['floor_max'])} | "
                f"{s['stereo_added_total']}/{s['stereo_added_frames']} | "
                f"{fnum(s['geo_ms_mean'],2)}/{fnum(s['geo_ms_p95'],2)} |")
    L.append("")

    # ---- EuRoC static no-regression ----
    L.append("## 4. EuRoC static no-regression — ATE-RMSE (m)")
    L.append("")
    L.append("| seq | baseline | adaptive v2 | Δ vs base |")
    L.append("|---|---:|---:|---:|")
    for seq in args.euroc.split():
        def find(method):
            hits = sorted(glob.glob(os.path.join(args.geodf_root, f"{seq}_{method}_s*", "eval", "metrics.json")))
            return load_json(hits[0]).get("ate_rmse_m") if hits else None
        b = find("baseline")
        a2 = find("adaptive_v2")
        L.append(f"| {seq} | {fnum(b)} | {fnum(a2)} | {pct(a2, b)} |")
    L.append("")

    os.makedirs(os.path.dirname(args.out), exist_ok=True)
    with open(args.out, "w") as fh:
        fh.write("\n".join(L) + "\n")
    print("\n".join(L))
    print(f"\n[v2-summary] -> {args.out}")


if __name__ == "__main__":
    main()
