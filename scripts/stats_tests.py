#!/usr/bin/env python3
"""Statistical significance of GeoDF-Weighted vs baseline (top-tier evidence).

For every condition it loads the per-trial ATE (and RPE) samples for baseline
and weighted and reports, per condition and in aggregate:

  * mean difference and % improvement
  * 95% CI on the mean difference (Welch)
  * Welch's t-test p-value (unequal variance)
  * Mann-Whitney U p-value (non-parametric, independent samples)
  * Cohen's d and Cliff's delta effect sizes
  * variance-reduction test (Levene) for the determinism claim

Small-N caveat: with n=5 (VIODE) / n=3 (EuRoC) power is limited; both a
parametric and a non-parametric test are reported so reviewers can judge.
"""
from __future__ import annotations

import argparse
import glob
import json
import math
import statistics as st
from pathlib import Path

import numpy as np
from scipy import stats as sp

VIODE_ENVS = ["city_day", "city_night", "parking_lot"]
VIODE_LEVELS = ["0_none", "1_low", "2_mid", "3_high"]
EUROC_SEQS = ["MH_01_easy", "MH_02_easy", "MH_03_medium", "MH_04_difficult", "MH_05_difficult"]


def load(seqdir: str, key: str = "ate_rmse_m") -> list[float]:
    vals = []
    for p in sorted(glob.glob(f"{seqdir}/trial_*/eval/metrics.json")):
        try:
            v = json.load(open(p)).get(key)
            if isinstance(v, (int, float)) and v == v and v >= 0:
                vals.append(float(v))
        except Exception:
            pass
    return vals


def cohens_d(a: list[float], b: list[float]) -> float:
    if len(a) < 2 or len(b) < 2:
        return float("nan")
    na, nb = len(a), len(b)
    va, vb = st.variance(a), st.variance(b)
    pooled = math.sqrt(((na - 1) * va + (nb - 1) * vb) / (na + nb - 2))
    if pooled == 0:
        return float("inf") if st.mean(a) != st.mean(b) else 0.0
    return (st.mean(a) - st.mean(b)) / pooled


def cliffs_delta(a: list[float], b: list[float]) -> float:
    """Non-parametric effect size in [-1, 1]. >0 means a tends to exceed b."""
    if not a or not b:
        return float("nan")
    gt = sum(1 for x in a for y in b if x > y)
    lt = sum(1 for x in a for y in b if x < y)
    return (gt - lt) / (len(a) * len(b))


def welch_ci(base: list[float], prop: list[float], conf: float = 0.95) -> tuple[float, float, float]:
    """95% CI on mean(base) - mean(prop) via Welch's t."""
    if len(base) < 2 or len(prop) < 2:
        return float("nan"), float("nan"), float("nan")
    mb, mp = st.mean(base), st.mean(prop)
    vb, vp = st.variance(base), st.variance(prop)
    nb, np_ = len(base), len(prop)
    se = math.sqrt(vb / nb + vp / np_)
    diff = mb - mp
    if se == 0:
        return diff, diff, diff
    dof = (vb / nb + vp / np_) ** 2 / (
        (vb / nb) ** 2 / (nb - 1) + (vp / np_) ** 2 / (np_ - 1)
    )
    tcrit = sp.t.ppf(0.5 + conf / 2, dof)
    return diff, diff - tcrit * se, diff + tcrit * se


def compare(base: list[float], prop: list[float]) -> dict:
    out: dict = {
        "n_base": len(base), "n_prop": len(prop),
        "mean_base": st.mean(base) if base else None,
        "mean_prop": st.mean(prop) if prop else None,
        "std_base": (st.pstdev(base) if len(base) > 1 else 0.0) if base else None,
        "std_prop": (st.pstdev(prop) if len(prop) > 1 else 0.0) if prop else None,
    }
    if not base or not prop:
        return out
    out["improvement_pct"] = (
        (st.mean(base) - st.mean(prop)) / st.mean(base) * 100.0 if st.mean(base) else None
    )
    diff, lo, hi = welch_ci(base, prop)
    out["mean_diff"] = diff
    out["ci95_lo"], out["ci95_hi"] = lo, hi
    # Welch t-test
    try:
        t, p = sp.ttest_ind(base, prop, equal_var=False)
        out["welch_t"], out["welch_p"] = float(t), float(p)
    except Exception:
        out["welch_t"] = out["welch_p"] = float("nan")
    # Mann-Whitney U (independent, non-parametric)
    try:
        u, p = sp.mannwhitneyu(base, prop, alternative="two-sided")
        out["mwu_U"], out["mwu_p"] = float(u), float(p)
    except Exception:
        out["mwu_U"] = out["mwu_p"] = float("nan")
    # Variance-reduction (determinism) — Levene
    try:
        w, p = sp.levene(base, prop)
        out["levene_p"] = float(p)
    except Exception:
        out["levene_p"] = float("nan")
    out["cohens_d"] = cohens_d(base, prop)
    out["cliffs_delta"] = cliffs_delta(base, prop)
    out["std_ratio_base_over_prop"] = (
        out["std_base"] / out["std_prop"] if out["std_prop"] else float("inf")
    )
    return out


def _f(x, p=3):
    if x is None or (isinstance(x, float) and (math.isnan(x) or math.isinf(x))):
        return "—"
    return f"{x:.{p}f}"


def _sig(p):
    if p is None or (isinstance(p, float) and math.isnan(p)):
        return ""
    return "***" if p < 0.001 else "**" if p < 0.01 else "*" if p < 0.05 else "ns"


def build_rows(cells: list[tuple[str, str]], root: str, key: str) -> tuple[list, dict]:
    rows, bundle = [], {}
    for label, seqbase in cells:
        b = load(f"{root}/{seqbase}_baseline", key)
        a = load(f"{root}/{seqbase}_weighted", key)
        c = compare(b, a)
        bundle[label] = c
        if c.get("mean_base") is None or c.get("mean_prop") is None:
            rows.append([label, "n/a", "n/a", "n/a", "n/a", "n/a", "n/a", "n/a"])
            continue
        rows.append([
            label,
            f"{_f(c['mean_base'])}±{_f(c['std_base'])}",
            f"{_f(c['mean_prop'])}±{_f(c['std_prop'])}",
            f"{c['improvement_pct']:+.1f}%" if c.get("improvement_pct") is not None else "—",
            f"[{_f(c['ci95_lo'])}, {_f(c['ci95_hi'])}]",
            f"{_f(c.get('welch_p'))} {_sig(c.get('welch_p'))}",
            f"{_f(c.get('mwu_p'))} {_sig(c.get('mwu_p'))}",
            _f(c.get("cohens_d"), 2),
        ])
    return rows, bundle


def md_table(headers, rows):
    out = ["| " + " | ".join(headers) + " |", "| " + " | ".join("---" for _ in headers) + " |"]
    out += ["| " + " | ".join(str(c) for c in r) + " |" for r in rows]
    return "\n".join(out)


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("--viode-root", default="results/viode_repeat")
    ap.add_argument("--euroc-root", default="results/euroc_repeat")
    ap.add_argument("--out-md", type=Path, default=Path("results/geodf_evaluation/STATS_TESTS.md"))
    ap.add_argument("--out-json", type=Path, default=Path("results/geodf_evaluation/stats_tests.json"))
    args = ap.parse_args()

    headers = ["Condition", "Baseline ATE", "Proposed ATE", "Δ%", "95% CI diff",
               "Welch p", "MWU p", "Cohen d"]
    viode_cells = [(f"{e}/{l}", f"{e}_{l}") for e in VIODE_ENVS for l in VIODE_LEVELS]
    euroc_cells = [(s, s) for s in EUROC_SEQS]

    vrows, vbundle = build_rows(viode_cells, args.viode_root, "ate_rmse_m")
    erows, ebundle = build_rows(euroc_cells, args.euroc_root, "ate_rmse_m")

    def verdict(bundle):
        sig_imp = sig_reg = 0
        for c in bundle.values():
            p = c.get("mwu_p")
            imp = c.get("improvement_pct")
            if p is None or imp is None or (isinstance(p, float) and math.isnan(p)):
                continue
            if p < 0.05 and imp > 0:
                sig_imp += 1
            elif p < 0.05 and imp < 0:
                sig_reg += 1
        return sig_imp, sig_reg

    v_imp, v_reg = verdict(vbundle)
    e_imp, e_reg = verdict(ebundle)

    lines = [
        "# Statistical significance — GeoDF-Weighted vs baseline\n",
        "Independent-sample tests over per-trial ATE-RMSE. `Welch p` = Welch t-test "
        "(unequal variance); `MWU p` = Mann-Whitney U (non-parametric). Significance: "
        "`*` p<0.05, `**` p<0.01, `***` p<0.001, `ns` not significant. Effect size = "
        "Cohen's d (pooled). Positive Δ% and positive d favour the proposed method.\n",
        "> Small-N caveat: with n=5 (VIODE) / n=3 (EuRoC) the minimum achievable MWU "
        "p-value is bounded; report both parametric and non-parametric results.\n",
        "## VIODE (n per group = 5)\n",
        md_table(headers, vrows),
        "",
        f"Significant improvements (MWU p<0.05, Δ>0): **{v_imp}/12**; "
        f"significant regressions: **{v_reg}/12**.\n",
        "## EuRoC static safety (n per group = 3)\n",
        md_table(headers, erows),
        "",
        f"Significant improvements: **{e_imp}/5**; significant regressions: **{e_reg}/5**.\n",
        "## Determinism (variance reduction) — VIODE\n",
        "Levene p<0.05 with std_base > std_prop indicates the filter significantly "
        "reduces run-to-run variance.\n",
        md_table(
            ["Condition", "std base", "std prop", "std ratio", "Levene p"],
            [
                [
                    lbl, _f(c["std_base"]), _f(c["std_prop"]),
                    _f(c.get("std_ratio_base_over_prop"), 2), f"{_f(c.get('levene_p'))} {_sig(c.get('levene_p'))}",
                ]
                for lbl, c in vbundle.items()
                if c.get("std_base") is not None and c.get("std_prop") is not None
            ],
        ),
        "",
    ]

    args.out_md.parent.mkdir(parents=True, exist_ok=True)
    args.out_md.write_text("\n".join(lines) + "\n")
    args.out_json.write_text(json.dumps({"viode": vbundle, "euroc": ebundle}, indent=2) + "\n")
    print("\n".join(lines))
    print(f"[ok] wrote {args.out_md}")
    print(f"[ok] wrote {args.out_json}")


if __name__ == "__main__":
    main()
