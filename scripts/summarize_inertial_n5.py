#!/usr/bin/env python3
"""Aggregate N-trial GeoDF-Inertial (Paper #2) vs baseline/adaptive (Paper #1).

Reads results/viode_repeat and results/euroc_repeat, reports mean±std ATE/RPE and
%-improvement of inertial over baseline and over adaptive (Paper #1).
"""
import argparse
import glob
import json
import os
import statistics as st

VIODE_ROOT = "results/viode_repeat"
EUROC_ROOT = "results/euroc_repeat"
ENVS = ["city_day", "city_night", "parking_lot"]
LEVELS = ["0_none", "1_low", "2_mid", "3_high"]
EUROC_SEQS = [
    "MH_01_easy", "MH_02_easy", "MH_03_medium",
    "MH_04_difficult", "MH_05_difficult",
]


def load(seqdir, key="ate_rmse_m"):
    vals = []
    for p in sorted(glob.glob(f"{seqdir}/trial_*/eval/metrics.json")):
        try:
            v = json.load(open(p)).get(key)
            if isinstance(v, (int, float)) and v >= 0:
                vals.append(v)
        except Exception:
            pass
    return vals


def ms(v):
    if not v:
        return None
    return dict(
        n=len(v),
        mean=st.mean(v),
        std=(st.pstdev(v) if len(v) > 1 else 0.0),
        median=st.median(v),
        lo=min(v),
        hi=max(v),
    )


def fmt(m):
    return f"{m['mean']:.3f}±{m['std']:.3f}" if m else "n/a"


def imp(base, prop):
    if not base or not prop or base["mean"] == 0:
        return None
    return (base["mean"] - prop["mean"]) / base["mean"] * 100.0


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument(
        "--out",
        default="results/geodf_evaluation/PAPER_RESULTS_INERTIAL_N5.md",
    )
    args = ap.parse_args()

    L = []
    L.append("# GeoDF-Inertial — N-trial results (Paper #2)\n")
    L.append(
        "ATE/RPE RMSE in metres. **Inertial** = IMU-predicted epipolar geometry "
        "(Paper #2). **Adaptive** = GeoDF-Adaptive feature-fit (Paper #1). "
        "Δ% = improvement (+ is better).\n"
    )

    L.append("## VIODE — ATE (baseline vs adaptive vs inertial)\n")
    L.append(
        "| env | level | baseline | adaptive (P1) | inertial (P2) | "
        "Δ% vs baseline | Δ% vs adaptive |"
    )
    L.append("|---|---|---|---|---|---|---|")
    parking_wins = 0
    parking_total = 0
    for e in ENVS:
        for l in LEVELS:
            b = ms(load(f"{VIODE_ROOT}/{e}_{l}_baseline"))
            a = ms(load(f"{VIODE_ROOT}/{e}_{l}_adaptive"))
            i = ms(load(f"{VIODE_ROOT}/{e}_{l}_inertial"))
            db = imp(b, i)
            da = imp(a, i)
            dbs = f"{db:+.1f}%" if db is not None else "n/a"
            das = f"{da:+.1f}%" if da is not None else "n/a"
            L.append(
                f"| {e} | {l} | {fmt(b)} | {fmt(a)} | {fmt(i)} | {dbs} | {das} |"
            )
            if e == "parking_lot" and l in ("2_mid", "3_high") and da is not None:
                parking_total += 1
                if da > 0:
                    parking_wins += 1

    L.append("\n## VIODE — headline parking_lot recovery\n")
    L.append(
        f"Inertial beats adaptive on **{parking_wins}/{parking_total}** "
        "parking_lot mid/high conditions (Paper #1 regression zone).\n"
    )

    L.append("## EuRoC static safety (inertial vs adaptive)\n")
    L.append("| sequence | baseline | adaptive (P1) | inertial (P2) | Δ% vs adaptive |")
    L.append("|---|---|---|---|---|")
    for seq in EUROC_SEQS:
        b = ms(load(f"{EUROC_ROOT}/{seq}_baseline"))
        a = ms(load(f"{EUROC_ROOT}/{seq}_adaptive"))
        i = ms(load(f"{EUROC_ROOT}/{seq}_inertial"))
        da = imp(a, i)
        das = f"{da:+.1f}%" if da is not None else "n/a"
        L.append(f"| {seq} | {fmt(b)} | {fmt(a)} | {fmt(i)} | {das} |")

    L.append("\n## GeoDF mode usage (from geo_df_stats.csv col 22 = mode)\n")
    L.append(
        "Mode codes: 0=feature-fit fallback, 1=inertial Sampson, 2=derotation.\n"
    )
    for pat in sorted(glob.glob(f"{VIODE_ROOT}/*_inertial/trial_1/geo_df_stats.csv")):
        modes = {0: 0, 1: 0, 2: 0}
        for line in open(pat):
            parts = line.strip().split(",")
            if len(parts) < 22 or parts[0] == "timestamp_ns":
                continue
            try:
                modes[int(float(parts[21]))] += 1
            except ValueError:
                pass
        total = sum(modes.values()) or 1
        tag = pat.split("/")[-3]
        L.append(
            f"- **{tag}**: mode0={modes[0]/total*100:.1f}% "
            f"mode1={modes[1]/total*100:.1f}% mode2={modes[2]/total*100:.1f}%"
        )

    os.makedirs(os.path.dirname(args.out), exist_ok=True)
    with open(args.out, "w", encoding="utf-8") as f:
        f.write("\n".join(L) + "\n")
    print("Wrote", args.out)


if __name__ == "__main__":
    main()
