#!/usr/bin/env python3
"""Aggregate repeatability trials into ATE mean +/- std per (level, method)."""
import argparse
import glob
import json
import os
import statistics as st

WS = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def trials_ate(root, env, level, method, key="ate_rmse_m"):
    vals = []
    for p in sorted(glob.glob(os.path.join(root, f"{env}_{level}_{method}", "trial_*", "eval", "metrics.json"))):
        try:
            v = json.load(open(p)).get(key)
            if isinstance(v, (int, float)):
                vals.append(v)
        except Exception:
            pass
    return vals


def fmt(vals):
    if not vals:
        return "n/a", None, None
    m = st.mean(vals)
    s = st.pstdev(vals) if len(vals) > 1 else 0.0
    return f"{m:.3f}±{s:.3f}", m, s


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--root", default=os.path.join(WS, "results", "viode_repeat"))
    ap.add_argument("--env", default="city_day")
    ap.add_argument("--levels", default="2_mid 3_high")
    ap.add_argument("--methods", default="baseline adaptive adaptive_v2")
    ap.add_argument("--out", default=os.path.join(WS, "results", "geodf_evaluation", "REPEATABILITY.md"))
    args = ap.parse_args()

    levels = args.levels.split()
    methods = args.methods.split()
    L = [f"# Repeatability study — ATE-RMSE mean±std (env={args.env})", "",
         "Each cell = mean±std over N trials of the SAME config and SAME build. ATE is near-",
         "deterministic within a build (std~0); compare configs only within one build.",
         "Δ = improvement of mean vs baseline mean (positive = better).", ""]
    header = "| level | " + " | ".join(methods) + " | " + \
        " | ".join(f"Δ {m} vs base" for m in methods if m != "baseline") + " |"
    L.append(header)
    L.append("|" + "---|" * (len(header.split("|")) - 2))

    for lv in levels:
        means = {}
        cells = []
        for m in methods:
            txt, mean, _ = fmt(trials_ate(args.root, args.env, lv, m))
            means[m] = mean
            n = len(trials_ate(args.root, args.env, lv, m))
            cells.append(f"{txt} (n={n})")
        deltas = []
        base = means.get("baseline")
        for m in methods:
            if m == "baseline":
                continue
            if isinstance(base, float) and isinstance(means.get(m), float) and base:
                deltas.append(f"{(base - means[m]) / base * 100:+.1f}%")
            else:
                deltas.append("n/a")
        L.append(f"| {lv} | " + " | ".join(cells) + " | " + " | ".join(deltas) + " |")

    os.makedirs(os.path.dirname(args.out), exist_ok=True)
    open(args.out, "w").write("\n".join(L) + "\n")
    print("\n".join(L))
    print(f"\n[repeat-summary] -> {args.out}")


if __name__ == "__main__":
    main()
