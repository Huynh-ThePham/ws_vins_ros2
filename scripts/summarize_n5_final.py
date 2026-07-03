#!/usr/bin/env python3
"""Aggregate the N=5 single-build evaluation into mean±std tables.

Reads results/viode_repeat (VIODE) and results/euroc_repeat (EuRoC), reports ATE/RPE
mean±std (and median) per condition for baseline / adaptive (PROPOSED), the
%-improvement of PROPOSED vs baseline, and optional Q3 ablation rows for
always-on, fixed-rho, no-quality, and no-vote variants when those trials exist.
"""
import json, glob, os, statistics as st, argparse

VIODE_ROOT = "results/viode_repeat"
EUROC_ROOT = "results/euroc_repeat"
ENVS = ["city_day", "city_night", "parking_lot"]
LEVELS = ["0_none", "1_low", "2_mid", "3_high"]
EUROC_SEQS = ["MH_01_easy", "MH_02_easy", "MH_03_medium", "MH_04_difficult", "MH_05_difficult"]
DEFAULT_METHODS = "baseline alwayson adaptive_fixed adaptive_no_quality adaptive_no_vote adaptive"


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
    ap.add_argument("--out-json", default="results/geodf_evaluation/paper_results_n5.json")
    ap.add_argument("--viode-only", action="store_true", help="omit EuRoC static-safety section")
    ap.add_argument("--methods", default=DEFAULT_METHODS,
                    help="space-separated method names to include in the ablation table")
    args = ap.parse_args()
    methods = args.methods.split()
    bundle = {"viode": {}, "euroc": {}}
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
            bundle["viode"][f"{e}/{l}"] = {
                "baseline_ate": b, "proposed_ate": a,
                "baseline_rpe": rb, "proposed_rpe": ra,
                "improvement_pct": d,
            }

    # ---- Q3 ablation table ----
    ablation_methods = [m for m in methods if m not in ("baseline", "adaptive", "proposed")]
    if ablation_methods:
        L.append("\n## VIODE — Q3 ablation baselines (ATE Δ% vs baseline)\n")
        L.append("Ablations isolate the contribution of each guard while keeping the same "
                 "VINS backend and dataset protocol. Empty cells mean that the trials have "
                 "not been generated in this worktree.\n")
        L.append("| env | level | " + " | ".join(ablation_methods) + " | PROPOSED |")
        L.append("|---|---|" + "|".join("---" for _ in ablation_methods) + "|---|")
        for e in ENVS:
            for l in LEVELS:
                b = rows[f"{e}_{l}"]["b"]
                cells = []
                for m in ablation_methods:
                    am = ms(load(f"{VIODE_ROOT}/{e}_{l}_{m}"))
                    cells.append((f"{imp(b, am):+.1f}%" if am and b else "n/a"))
                    bundle["viode"].setdefault(f"{e}/{l}", {})[f"{m}_ate"] = am
                    bundle["viode"][f"{e}/{l}"][f"{m}_improvement_pct"] = imp(b, am)
                proposed = f"{rows[f'{e}_{l}']['d']:+.1f}%" if rows[f"{e}_{l}"]["d"] is not None else "n/a"
                L.append(f"| {e} | {l} | " + " | ".join(cells) + f" | {proposed} |")

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
    if not args.viode_only:
        L.append("\n## EuRoC — static safety (ATE, baseline vs PROPOSED)\n")
        L.append("| seq | baseline ATE | PROPOSED ATE | Δ% |")
        L.append("|---|---|---|---|")
        for seq in EUROC_SEQS:
            b = ms(load(f"{EUROC_ROOT}/{seq}_baseline"))
            a = ms(load(f"{EUROC_ROOT}/{seq}_adaptive"))
            d = imp(b, a)
            ds = f"{d:+.1f}%" if d is not None else "n/a"
            L.append(f"| {seq} | {fmt(b)} | {fmt(a)} | {ds} |")
            bundle["euroc"][seq] = {"baseline_ate": b, "proposed_ate": a, "improvement_pct": d}

        if ablation_methods:
            L.append("\n## EuRoC — static ablation baselines (ATE Δ% vs baseline)\n")
            L.append("| seq | " + " | ".join(ablation_methods) + " | PROPOSED |")
            L.append("|---|" + "|".join("---" for _ in ablation_methods) + "|---|")
            for seq in EUROC_SEQS:
                b = ms(load(f"{EUROC_ROOT}/{seq}_baseline"))
                cells = []
                for m in ablation_methods:
                    am = ms(load(f"{EUROC_ROOT}/{seq}_{m}"))
                    cells.append((f"{imp(b, am):+.1f}%" if am and b else "n/a"))
                    bundle["euroc"].setdefault(seq, {})[f"{m}_ate"] = am
                    bundle["euroc"][seq][f"{m}_improvement_pct"] = imp(b, am)
                proposed = f"{bundle['euroc'][seq]['improvement_pct']:+.1f}%" if bundle["euroc"][seq].get("improvement_pct") is not None else "n/a"
                L.append(f"| {seq} | " + " | ".join(cells) + f" | {proposed} |")

    # ---- aggregate verdict ----
    evaluated = sum(1 for r in rows.values() if r["d"] is not None)
    wins = sum(1 for k, r in rows.items() if r['d'] is not None and r['d'] > 3)
    losses = sum(1 for k, r in rows.items() if r['d'] is not None and r['d'] < -3)
    neutral = sum(1 for k, r in rows.items() if r['d'] is not None and -3 <= r['d'] <= 3)
    L.append(f"\n## VIODE ATE verdict (PROPOSED vs baseline, ±3% band)\n")
    if evaluated:
        L.append(f"- evaluated={evaluated}/12  wins(>+3%)={wins}  losses(<-3%)={losses}  neutral={neutral}\n")
    else:
        L.append("- **NO DATA**: generate the repeat trials before using this table in a manuscript.\n")

    os.makedirs(os.path.dirname(args.out), exist_ok=True)
    open(args.out, "w").write("\n".join(L) + "\n")
    if args.out_json:
        os.makedirs(os.path.dirname(args.out_json), exist_ok=True)
        open(args.out_json, "w").write(json.dumps(bundle, indent=2) + "\n")
    print("\n".join(L))
    print(f"\n[ok] wrote {args.out}")
    if args.out_json:
        print(f"[ok] wrote {args.out_json}")


if __name__ == "__main__":
    main()
