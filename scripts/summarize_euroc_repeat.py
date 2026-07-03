#!/usr/bin/env python3
"""Aggregate EuRoC N-repeat trials into mean±std static-safety tables."""
import argparse
import glob
import json
import os
import statistics as st

EUROC_ROOT = "results/euroc_repeat"
SEQS = ["MH_01_easy", "MH_02_easy", "MH_03_medium", "MH_04_difficult", "MH_05_difficult"]
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
    return dict(
        n=len(v),
        mean=st.mean(v),
        std=st.pstdev(v) if len(v) > 1 else 0.0,
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
    ap.add_argument("--root", default=EUROC_ROOT)
    ap.add_argument("--n", type=int, default=3)
    ap.add_argument("--out", default="results/geodf_evaluation/EUROC_REPEAT_N3.md")
    ap.add_argument("--out-json", default="results/geodf_evaluation/euroc_repeat_n3.json")
    ap.add_argument("--methods", default=DEFAULT_METHODS,
                    help="space-separated method names to include in the ablation table")
    args = ap.parse_args()
    methods = args.methods.split()
    ablation_methods = [m for m in methods if m not in ("baseline", "adaptive", "proposed")]
    bundle = {}

    L = [
        f"# EuRoC Machine Hall — N={args.n} repeat study (mean±std)\n",
        "Static safety check: baseline vs PROPOSED (adaptive). "
        "Δ% = ATE improvement of PROPOSED vs baseline (+ is better). "
        "Pass criterion: no regression beyond ±5% on static EuRoC.\n",
        "## ATE / RPE (baseline vs PROPOSED)\n",
        "| seq | baseline ATE | PROPOSED ATE | Δ% | baseline RPE | PROPOSED RPE |",
        "|---|---|---|---|---|---|",
    ]

    rows = {}
    for seq in SEQS:
        b = ms(load(f"{args.root}/{seq}_baseline"))
        a = ms(load(f"{args.root}/{seq}_adaptive"))
        rb = ms(load(f"{args.root}/{seq}_baseline", "rpe_rmse_m"))
        ra = ms(load(f"{args.root}/{seq}_adaptive", "rpe_rmse_m"))
        d = imp(b, a)
        rows[seq] = dict(b=b, a=a, d=d)
        ds = f"{d:+.1f}%" if d is not None else "n/a"
        L.append(f"| {seq} | {fmt(b)} | {fmt(a)} | {ds} | {fmt(rb)} | {fmt(ra)} |")
        bundle[seq] = {"baseline_ate": b, "proposed_ate": a,
                       "baseline_rpe": rb, "proposed_rpe": ra, "improvement_pct": d}

    if ablation_methods:
        L += [
            "\n## Static ablation baselines — ATE Δ% vs baseline\n",
            "Ablations keep the same stereo-inertial backend and only alter the GeoDF front-end guard. "
            "Empty cells mean the corresponding trials have not been generated in this worktree.\n",
            "| seq | " + " | ".join(ablation_methods) + " | PROPOSED |",
            "|---|" + "|".join("---" for _ in ablation_methods) + "|---|",
        ]
        for seq in SEQS:
            b = rows[seq]["b"]
            cells = []
            for m in ablation_methods:
                am = ms(load(f"{args.root}/{seq}_{m}"))
                cells.append((f"{imp(b, am):+.1f}%" if am and b else "n/a"))
                bundle[seq][f"{m}_ate"] = am
                bundle[seq][f"{m}_improvement_pct"] = imp(b, am)
            proposed = f"{rows[seq]['d']:+.1f}%" if rows[seq]["d"] is not None else "n/a"
            L.append(f"| {seq} | " + " | ".join(cells) + f" | {proposed} |")

    L += [
        "\n## Repeatability — run-to-run ATE std\n",
        "| seq | baseline std | PROPOSED std | baseline range |",
        "|---|---|---|---|",
    ]
    for seq in SEQS:
        b, a = rows[seq]["b"], rows[seq]["a"]
        if b and a:
            rng = f"[{b['lo']:.3f}, {b['hi']:.3f}]"
            L.append(f"| {seq} | {b['std']:.3f} | {a['std']:.3f} | {rng} |")

    evaluated = sum(1 for r in rows.values() if r["d"] is not None)
    regressions = [
        seq for seq, r in rows.items()
        if r["d"] is not None and r["d"] < -5
    ]
    L.append("\n## Verdict\n")
    if not evaluated:
        L.append("- **NO DATA**: generate the repeat trials before using this table in a manuscript.")
    elif regressions:
        L.append(f"- **FAIL**: PROPOSED regresses >5% on {', '.join(regressions)}")
    else:
        L.append("- **PASS**: no sequence exceeds 5% ATE regression vs baseline")

    os.makedirs(os.path.dirname(args.out), exist_ok=True)
    open(args.out, "w").write("\n".join(L) + "\n")
    if args.out_json:
        os.makedirs(os.path.dirname(args.out_json), exist_ok=True)
        open(args.out_json, "w").write(json.dumps(bundle, indent=2) + "\n")
        print(f"[ok] wrote {args.out_json}")
    print("\n".join(L))
    print(f"\n[ok] wrote {args.out}")


if __name__ == "__main__":
    main()
