#!/usr/bin/env python3
"""Aggregate N-trial GeoDF-Hybrid (Paper #2) vs baseline/adaptive/inertial.

Reads results/viode_repeat and results/euroc_repeat. Hybrid is the proposed
two-source geometry filter with reliability-gated arbitration; inertial-only
is kept as an ablation (geodf_hybrid_enable=0).
"""
import argparse
import csv
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


def mode_stats(csv_path):
    modes = {0: 0, 1: 0, 2: 0}
    arb = {0: 0, 1: 0, 2: 0, 3: 0}
    sig = []
    dyn = 0
    stale = False
    with open(csv_path, newline="", encoding="utf-8") as f:
        reader = csv.DictReader(f)
        fields = set(reader.fieldnames or [])
        # Require the full hysteresis-era schema: a CSV lacking scene_dynamic
        # predates the cue+hysteresis arbitration and must be regenerated.
        stale = not {"imu_mode", "hybrid_arb", "scene_dynamic"} <= fields
        if stale:
            return modes, arb, 0, sig, dyn, True
        has_sig = "hybrid_signal" in fields
        has_dyn = "scene_dynamic" in fields
        for row in reader:
            try:
                modes[int(float(row["imu_mode"]))] += 1
                arb[int(float(row["hybrid_arb"]))] += 1
                if has_sig:
                    sig.append(float(row["hybrid_signal"]))
                if has_dyn and int(float(row["scene_dynamic"])) == 1:
                    dyn += 1
            except (KeyError, ValueError):
                pass
    total = sum(modes.values())
    return modes, arb, total, sig, dyn, False


def fmt_mode_stats(modes, arb, total, sig=None, dyn=0):
    if total <= 0:
        return "no rows"
    text = ", ".join(f"mode{k}={modes[k]/total*100:.1f}%" for k in sorted(modes))
    used_arb = ", ".join(
        f"arb{k}={arb[k]/total*100:.1f}%" for k in sorted(arb) if arb[k]
    )
    out = f"{text}; {used_arb}" if used_arb else f"{text}; no hybrid_arb rows"
    out += f"; dyn_latch={dyn / total * 100:.1f}%"
    if sig:
        out += f"; signal_mean={sum(sig) / len(sig):.3f}"
    return out


def stale_hybrid_stats():
    stale = []
    for p in sorted(glob.glob(f"{VIODE_ROOT}/*_hybrid/trial_*/geo_df_stats.csv")):
        try:
            with open(p, newline="", encoding="utf-8") as f:
                fields = set(csv.DictReader(f).fieldnames or [])
            if not {"imu_mode", "hybrid_arb", "scene_dynamic"} <= fields:
                stale.append(p)
        except OSError:
            continue
    return stale


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument(
        "--out",
        default="results/geodf_evaluation/PAPER_RESULTS_HYBRID_N5.md",
    )
    args = ap.parse_args()

    L = []
    L.append("# GeoDF-Hybrid — N-trial results (Paper #2)\n")
    L.append(
        "ATE RMSE in metres. **Hybrid** = two-source geometry filter with "
        "reliability-gated arbitration (P1 feature-fit vs IMU epipolar vs "
        "derotation). **Inertial-only** = ablation without arbitration. "
        "Δ% = improvement (+ is better).\n"
    )

    L.append("## VIODE — ATE (baseline vs P1 vs inertial vs hybrid)\n")
    L.append(
        "| env | level | baseline | adaptive (P1) | inertial | hybrid (P2) | "
        "Δ% hybrid vs P1 | Δ% hybrid vs inertial |"
    )
    L.append("|---|---|---|---|---|---|---|---|")
    parking_wins = 0
    parking_total = 0
    static_regress = 0
    static_total = 0
    for e in ENVS:
        for l in LEVELS:
            b = ms(load(f"{VIODE_ROOT}/{e}_{l}_baseline"))
            a = ms(load(f"{VIODE_ROOT}/{e}_{l}_adaptive"))
            i = ms(load(f"{VIODE_ROOT}/{e}_{l}_inertial"))
            h = ms(load(f"{VIODE_ROOT}/{e}_{l}_hybrid"))
            dh = imp(a, h)
            di = imp(i, h)
            dhs = f"{dh:+.1f}%" if dh is not None else "n/a"
            dis = f"{di:+.1f}%" if di is not None else "n/a"
            L.append(
                f"| {e} | {l} | {fmt(b)} | {fmt(a)} | {fmt(i)} | {fmt(h)} | "
                f"{dhs} | {dis} |"
            )
            if e == "parking_lot" and l in ("2_mid", "3_high") and dh is not None:
                parking_total += 1
                if dh > 0:
                    parking_wins += 1
            if l == "0_none" and dh is not None:
                static_total += 1
                if dh < 0:
                    static_regress += 1

    L.append("\n## Headline checks\n")
    L.append(
        f"- parking_lot mid/high: hybrid beats P1 on **{parking_wins}/{parking_total}** "
        "conditions\n"
    )
    L.append(
        f"- static safety (0_none): hybrid worse than P1 on **{static_regress}/"
        f"{static_total}** envs (target: 0)\n"
    )

    L.append("## EuRoC static safety\n")
    L.append("| sequence | baseline | adaptive (P1) | inertial | hybrid (P2) | Δ% hybrid vs P1 |")
    L.append("|---|---|---|---|---|---|")
    for seq in EUROC_SEQS:
        b = ms(load(f"{EUROC_ROOT}/{seq}_baseline"))
        a = ms(load(f"{EUROC_ROOT}/{seq}_adaptive"))
        i = ms(load(f"{EUROC_ROOT}/{seq}_inertial"))
        h = ms(load(f"{EUROC_ROOT}/{seq}_hybrid"))
        dh = imp(a, h)
        dhs = f"{dh:+.1f}%" if dh is not None else "n/a"
        L.append(f"| {seq} | {fmt(b)} | {fmt(a)} | {fmt(i)} | {fmt(h)} | {dhs} |")

    L.append("\n## Geometry mode + arbitration (hybrid trial_1 stats)\n")
    L.append(
        "Mode: 0=P1 feature-fit, 1=inertial Sampson, 2=derotation. "
        "Arb: 0=n/a, 1=forced P1, 2=dynamic→inertial, 3=dynamic→derot. "
        "dyn_latch = % frames the hysteresis latch held the inertial/derot side; "
        "signal_mean = mean hybrid arbitration cue (for inertial_floor ablation). "
        "Rows marked stale were produced before hybrid diagnostics existed and "
        "must be regenerated with `FORCE=1`.\n"
    )
    for pat in sorted(glob.glob(f"{VIODE_ROOT}/*_hybrid/trial_1/geo_df_stats.csv")):
        modes, arb, total, sig, dyn, stale = mode_stats(pat)
        tag = pat.split("/")[-3]
        if stale:
            L.append(f"- **{tag}**: stale CSV (rerun with `FORCE=1`)")
        else:
            L.append(f"- **{tag}**: {fmt_mode_stats(modes, arb, total, sig, dyn)}")

    stale = stale_hybrid_stats()
    if stale:
        L.append("\n## Reproducibility warnings\n")
        L.append(
            f"- {len(stale)} hybrid stats files are stale and lack hybrid diagnostics; "
            "delete/re-run them or run hybrid benchmarks with `FORCE=1` before citing."
        )

    os.makedirs(os.path.dirname(args.out), exist_ok=True)
    with open(args.out, "w", encoding="utf-8") as f:
        f.write("\n".join(L) + "\n")
    print("Wrote", args.out)


if __name__ == "__main__":
    main()
