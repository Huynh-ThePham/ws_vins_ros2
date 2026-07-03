#!/usr/bin/env python3
"""Summarize the 3-way stereo-3D vs 2D-F ablation."""
from __future__ import annotations

import argparse
import json
import statistics as st
from pathlib import Path

ENVS = ["city_day", "city_night", "parking_lot"]
LEVELS = ["0_none", "1_low", "2_mid", "3_high"]
SHORT_CELLS = [
    ("city_day", "0_none", "quasi-static VIODE"),
    ("city_day", "3_high", "high-dynamic VIODE"),
]
EUROC_SEQS = [
    "MH_01_easy", "MH_02_easy", "MH_03_medium",
    "MH_04_difficult", "MH_05_difficult",
]
METHODS = ["baseline", "adaptive_2d", "adaptive"]


def trial_matches_method(metric_path: Path, method: str) -> bool:
    if method == "baseline":
        return True
    log_path = metric_path.parents[1] / "pht_vio_node.log"
    try:
        text = log_path.read_text(errors="ignore")
    except OSError:
        return False
    if method == "adaptive":
        return "motion3d_enable=1" in text
    if method == "adaptive_2d":
        return "motion3d_enable=0" in text
    return True


def load_ate(root: Path, key: str, method: str, max_trials: int | None) -> dict | None:
    cell = root / f"{key}_{method}"
    vals = []
    for p in sorted(cell.glob("trial_*/eval/metrics.json")):
        tname = p.parts[-3]  # trial_k
        try:
            ti = int(tname.split("_")[1])
        except (IndexError, ValueError):
            ti = 999
        if max_trials is not None and ti > max_trials:
            continue
        if not trial_matches_method(p, method):
            continue
        try:
            v = json.loads(p.read_text()).get("ate_rmse_m")
            if isinstance(v, (int, float)) and v >= 0:
                vals.append(float(v))
        except Exception:
            pass
    if not vals:
        return None
    return {"n": len(vals), "mean": st.mean(vals), "std": st.pstdev(vals) if len(vals) > 1 else 0.0}


def imp(base: dict | None, other: dict | None) -> float | None:
    if not base or not other or base["mean"] == 0:
        return None
    return (base["mean"] - other["mean"]) / base["mean"] * 100.0


def fmt(m: dict | None) -> str:
    if not m:
        return "n/a"
    return f"{m['mean']:.3f}±{m['std']:.3f} (n={m['n']})"


def viode_rows(viode_root: Path, cells: list, max_trials: int | None) -> tuple[list[str], dict]:
    lines = [
        "| env | level | baseline ATE | old 2D-F ATE | new 3D ATE | Δ% 3D vs base | Δ(3D−2D) |",
        "|---|---|---|---|---|---|---|",
    ]
    bundle: dict = {}
    for env, level, *_ in cells:
        key = f"{env}_{level}"
        row = {m: load_ate(viode_root, key, m, max_trials) for m in METHODS}
        d3 = imp(row["baseline"], row["adaptive"])
        d32 = imp(row["adaptive_2d"], row["adaptive"])
        note = ""
        if level == "3_high":
            note = "dynamic"
        elif level == "0_none":
            note = "static"
        lines.append(
            f"| {env} | {level} | {fmt(row['baseline'])} | {fmt(row['adaptive_2d'])} | "
            f"{fmt(row['adaptive'])} | "
            f"{f'{d3:+.1f}%' if d3 is not None else 'n/a'} | "
            f"{f'{d32:+.1f}%' if d32 is not None else 'n/a'} |"
        )
        bundle[key] = {
            **{m: row[m] for m in METHODS},
            "delta_3d_vs_base_pct": d3,
            "delta_3d_vs_2d_pct": d32,
        }
    return lines, bundle


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("--viode-root", default="results/viode_repeat")
    ap.add_argument("--euroc-root", default="results/euroc_repeat")
    ap.add_argument("--out", default="results/geodf_evaluation/STEREO3D_ABLATION.md")
    ap.add_argument("--out-json", default="results/geodf_evaluation/stereo3d_ablation.json")
    ap.add_argument("--full-grid", action="store_true", help="all 12 VIODE cells")
    ap.add_argument("--max-trials", type=int, default=None, help="use trial_1..N only")
    ap.add_argument("--skip-euroc", action="store_true")
    args = ap.parse_args()

    viode_root = Path(args.viode_root)
    euroc_root = Path(args.euroc_root)
    if args.full_grid:
        cells = [(e, l, "") for e in ENVS for l in LEVELS]
        title = "full 12-condition study"
    else:
        cells = SHORT_CELLS
        title = "short study"

    n_note = f" (trial_1..{args.max_trials})" if args.max_trials else ""
    lines = [
        f"# Stereo 3D vs 2D-F ablation ({title}{n_note})",
        "",
        "| Config | Alias | Candidate gate |",
        "|---|---|---|",
        "| Baseline VINS | `baseline` | none |",
        "| Old adaptive | `adaptive_2d` | temporal **2D-F** + scene gating |",
        "| New adaptive (PROPOSED) | `adaptive` | **stereo 3D motion consistency** + scene gating |",
        "",
        "Δ% = ATE improvement vs baseline (+ is better). Δ(3D−2D) = 2D-F minus stereo 3D (+ means 3D wins).",
        "",
        "## VIODE trajectory (ATE RMSE mean±std)",
        "",
    ]
    vlines, vb = viode_rows(viode_root, cells, args.max_trials)
    lines += vlines

    # Aggregate verdict
    dyn_wins = dyn_total = static_ok = static_total = wins_3d_vs_base = eval_total = 0
    for env, level, *_ in cells:
        key = f"{env}_{level}"
        row = vb.get(key, {})
        d32 = row.get("delta_3d_vs_2d_pct")
        d3b = row.get("delta_3d_vs_base_pct")
        a2 = row.get("adaptive_2d")
        a3 = row.get("adaptive")
        if d32 is not None:
            if level == "3_high":
                dyn_total += 1
                if d32 > 3.0:
                    dyn_wins += 1
            if level in ("0_none", "1_low"):
                static_total += 1
                if a2 and a3 and a3["mean"] <= a2["mean"] * 1.03:
                    static_ok += 1
        if d3b is not None:
            eval_total += 1
            if d3b > 3.0:
                wins_3d_vs_base += 1

    if not args.skip_euroc:
        lines += ["", "## EuRoC static safety", "", "| sequence | baseline | old 2D-F | new 3D | Δ% 3D vs base | Δ(3D−2D) |", "|---|---|---|---|---|---|"]
        for seq in EUROC_SEQS:
            row = {m: load_ate(euroc_root, seq, m, args.max_trials) for m in METHODS}
            d3 = imp(row["baseline"], row["adaptive"])
            d32 = imp(row["adaptive_2d"], row["adaptive"])
            lines.append(
                f"| {seq} | {fmt(row['baseline'])} | {fmt(row['adaptive_2d'])} | "
                f"{fmt(row['adaptive'])} | {f'{d3:+.1f}%' if d3 is not None else 'n/a'} | "
                f"{f'{d32:+.1f}%' if d32 is not None else 'n/a'} |"
            )

    claim = (
        "Stereo 3D motion consistency improves dynamic VIO robustness over 2D-F gating "
        "while preserving static pass-through."
    )
    supported = dyn_wins >= max(1, dyn_total // 2) and static_ok >= max(1, static_total - 1)
    lines += [
        "",
        "## Claim check",
        "",
        f"- **Dynamic (`*/3_high`):** 3D beats 2D-F by >3% on {dyn_wins}/{dyn_total} cells",
        f"- **Low-dynamic (`*/0_none`, `*/1_low`):** 3D ATE ≤ 2D-F (+3%) on {static_ok}/{static_total} cells",
        f"- **Overall:** 3D beats baseline by >3% on {wins_3d_vs_base}/{eval_total} cells",
        "",
        f"**Claim:** {claim}",
        "",
        f"**Verdict:** {'SUPPORTED' if supported else 'PARTIAL — fill missing trials / check counter-examples'}",
        "",
        "See also `MASK_EVAL_2D_vs_3D.md` for feature-level VIODE vehicle-mask precision.",
    ]

    bundle = {"viode": vb, "verdict": {"supported": supported, "claim": claim}}
    out = Path(args.out)
    out.parent.mkdir(parents=True, exist_ok=True)
    out.write_text("\n".join(lines) + "\n")
    Path(args.out_json).write_text(json.dumps(bundle, indent=2) + "\n")
    print(f"[ok] {out}")


if __name__ == "__main__":
    main()
