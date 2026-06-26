#!/usr/bin/env python3
"""Aggregate the N=5 single-build evaluation into mean±std tables.

Reads results/viode_repeat (VIODE) and results/euroc_repeat (EuRoC), reports ATE/RPE
mean±std (and median) per condition for baseline / adaptive (PROPOSED), the
%-improvement of PROPOSED vs baseline, and a determinism view (baseline vs PROPOSED
std) that quantifies the filter's run-to-run variance reduction on dynamic scenes.
"""
import json, glob, os, statistics as st, argparse

VIODE_ROOT = "results/viode_repeat"
EUROC_ROOT = "results/euroc_repeat"
ENVS = ["city_day", "city_night", "parking_lot"]
LEVELS = ["0_none", "1_low", "2_mid", "3_high"]
EUROC_SEQS = ["MH_01_easy", "MH_02_easy", "MH_03_medium", "MH_04_difficult", "MH_05_difficult"]


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
    return dict(n=len(v), mean=st.mean(v),
                std=(st.pstdev(v) if len(v) > 1 else 0.0),
                median=st.median(v),
                lo=min(v), hi=max(v))


def fmt(m):
    return f"{m['mean']:.3f}±{m['std']:.3f}" if m else "  n/a"


def imp(base, prop):
    if not base or not prop or base['mean'] == 0:
        return None
    return (base['mean'] - prop['mean']) / base['mean'] * 100.0


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--out", default="results/geodf_evaluation/PAPER_RESULTS_N5.md")
    args = ap.parse_args()
    L = []
    L.append("# GeoDF-VINS — N=5 single-build results (mean±std)\n")
    L.append("ATE/RPE RMSE in metres. PROPOSED = adaptive (hard reject + scene-gating + "
             "auto-ρ + temporal voting). Δ% = improvement of PROPOSED vs baseline (+ is better).\n")

    # ---- VIODE main table ----
    L.append("## VIODE — ATE/RPE (baseline vs PROPOSED)\n")
    L.append("| env | level | baseline ATE | PROPOSED ATE | Δ% | baseline RPE | PROPOSED RPE |")
    L.append("|---|---|---|---|---|---|---|")
    rows = {}
    for e in ENVS:
        for l in LEVELS:
            b = ms(load(f"{VIODE_ROOT}/{e}_{l}_baseline"))
            a = ms(load(f"{VIODE_ROOT}/{e}_{l}_adaptive"))
            rb = ms(load(f"{VIODE_ROOT}/{e}_{l}_baseline", "rpe_rmse_m"))
            ra = ms(load(f"{VIODE_ROOT}/{e}_{l}_adaptive", "rpe_rmse_m"))
            d = imp(b, a)
            rows[f"{e}_{l}"] = dict(b=b, a=a, rb=rb, ra=ra, d=d)
            ds = f"{d:+.1f}%" if d is not None else "n/a"
            L.append(f"| {e} | {l} | {fmt(b)} | {fmt(a)} | {ds} | {fmt(rb)} | {fmt(ra)} |")

    # ---- Determinism view ----
    L.append("\n## Determinism — run-to-run ATE std (lower = more repeatable)\n")
    L.append("Dynamic scenes show baseline variance that the PROPOSED filter collapses.\n")
    L.append("| env | level | baseline std | PROPOSED std | baseline range |")
    L.append("|---|---|---|---|---|")
    for e in ENVS:
        for l in LEVELS:
            r = rows[f"{e}_{l}"]
            b, a = r['b'], r['a']
            if b and a:
                rng = f"[{b['lo']:.3f}, {b['hi']:.3f}]"
                L.append(f"| {e} | {l} | {b['std']:.3f} | {a['std']:.3f} | {rng} |")

    # ---- EuRoC static safety ----
    L.append("\n## EuRoC — static safety (ATE, baseline vs PROPOSED)\n")
    L.append("| seq | baseline ATE | PROPOSED ATE | Δ% |")
    L.append("|---|---|---|---|")
    for seq in EUROC_SEQS:
        b = ms(load(f"{EUROC_ROOT}/{seq}_baseline"))
        a = ms(load(f"{EUROC_ROOT}/{seq}_adaptive"))
        d = imp(b, a)
        ds = f"{d:+.1f}%" if d is not None else "n/a"
        L.append(f"| {seq} | {fmt(b)} | {fmt(a)} | {ds} |")

    # ---- aggregate verdict ----
    wins = sum(1 for k, r in rows.items() if r['d'] is not None and r['d'] > 3)
    losses = sum(1 for k, r in rows.items() if r['d'] is not None and r['d'] < -3)
    neutral = sum(1 for k, r in rows.items() if r['d'] is not None and -3 <= r['d'] <= 3)
    L.append(f"\n## VIODE ATE verdict (PROPOSED vs baseline, ±3% band)\n")
    L.append(f"- wins(>+3%)={wins}  losses(<-3%)={losses}  neutral={neutral}\n")

    os.makedirs(os.path.dirname(args.out), exist_ok=True)
    open(args.out, "w").write("\n".join(L) + "\n")
    print("\n".join(L))
    print(f"\n[ok] wrote {args.out}")


if __name__ == "__main__":
    main()
