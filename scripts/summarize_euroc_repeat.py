#!/usr/bin/env python3
"""Aggregate EuRoC N-repeat trials into mean±std static-safety tables."""
import argparse
import glob
import json
import os
import statistics as st

EUROC_ROOT = "results/euroc_repeat"
SEQS = ["MH_01_easy", "MH_02_easy", "MH_03_medium", "MH_04_difficult", "MH_05_difficult"]


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
    args = ap.parse_args()
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

    regressions = [
        seq for seq, r in rows.items()
        if r["d"] is not None and r["d"] < -5
    ]
    L.append("\n## Verdict\n")
    if regressions:
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
