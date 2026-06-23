#!/usr/bin/env python3
"""Aggregate repeated VIODE runs into mean +/- std ATE/RPE (non-determinism)."""
from __future__ import annotations

import argparse
import json
import math
from pathlib import Path
from typing import Any


def _collect(root: Path, env: str, level: str, method: str, repeats: int) -> dict[str, list[float]]:
    ate, rpe = [], []
    for r in range(1, repeats + 1):
        f = root / f"{env}_{level}_{method}_r{r}" / "eval" / "metrics.json"
        if not f.is_file():
            continue
        try:
            m = json.loads(f.read_text())
        except json.JSONDecodeError:
            continue
        if isinstance(m.get("ate_rmse_m"), (int, float)):
            ate.append(m["ate_rmse_m"])
        if isinstance(m.get("rpe_rmse_m"), (int, float)):
            rpe.append(m["rpe_rmse_m"])
    return {"ate": ate, "rpe": rpe}


def _ms(xs: list[float]) -> tuple[float, float]:
    if not xs:
        return float("nan"), float("nan")
    mu = sum(xs) / len(xs)
    sd = math.sqrt(sum((x - mu) ** 2 for x in xs) / len(xs)) if len(xs) > 1 else 0.0
    return mu, sd


def _cell(xs: list[float]) -> str:
    if not xs:
        return "—"
    mu, sd = _ms(xs)
    return f"{mu:.3f}±{sd:.3f}"


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("--root", type=Path, default=Path("results/viode/repeat"))
    ap.add_argument("--env", default="city_day")
    ap.add_argument("--levels", default="0_none 1_low 2_mid 3_high")
    ap.add_argument("--repeats", type=int, default=3)
    ap.add_argument("--out", type=Path, default=None)
    args = ap.parse_args()

    rows = []
    bundle: dict[str, Any] = {}
    for level in args.levels.split():
        b = _collect(args.root, args.env, level, "baseline", args.repeats)
        g = _collect(args.root, args.env, level, "geodf", args.repeats)
        b_ate_mu, _ = _ms(b["ate"])
        g_ate_mu, _ = _ms(g["ate"])
        b_rpe_mu, _ = _ms(b["rpe"])
        g_rpe_mu, _ = _ms(g["rpe"])
        ate_d = (100.0 * (b_ate_mu - g_ate_mu) / b_ate_mu
                 if b_ate_mu == b_ate_mu and b_ate_mu > 0 and g_ate_mu == g_ate_mu else float("nan"))
        rpe_d = (100.0 * (b_rpe_mu - g_rpe_mu) / b_rpe_mu
                 if b_rpe_mu == b_rpe_mu and b_rpe_mu > 0 and g_rpe_mu == g_rpe_mu else float("nan"))
        bundle[level] = {"baseline": b, "geodf": g,
                         "ate_delta_pct": ate_d, "rpe_delta_pct": rpe_d,
                         "n_runs_baseline": len(b["ate"]), "n_runs_geodf": len(g["ate"])}
        rows.append([
            level, _cell(b["ate"]), _cell(g["ate"]),
            (f"{ate_d:+.1f}%" if ate_d == ate_d else "—"),
            _cell(b["rpe"]), _cell(g["rpe"]),
            (f"{rpe_d:+.1f}%" if rpe_d == rpe_d else "—"),
        ])

    headers = ["Level", f"Base ATE (n={args.repeats})", "GeoDF ATE", "ATE Δ",
               "Base RPE(1m)", "GeoDF RPE(1m)", "RPE Δ"]
    table = ["| " + " | ".join(headers) + " |",
             "| " + " | ".join("---" for _ in headers) + " |"]
    table += ["| " + " | ".join(r) + " |" for r in rows]
    lines = [
        f"# VIODE {args.env}: baseline vs GeoDF-Hard, mean±std over {args.repeats} runs",
        "",
        "VINS-Fusion is multi-threaded / non-deterministic; values are mean±std (population) "
        "over repeated runs. Positive Δ = GeoDF better. ATE/RPE vs GT `/odometry` (evo, SE(3)).",
        "",
        "\n".join(table),
        "",
    ]
    out = args.out or (args.root.parent / f"viode_{args.env}_repeat.md")
    out.write_text("\n".join(lines))
    out.with_suffix(".json").write_text(json.dumps(bundle, indent=2) + "\n")
    print(f"[ok] wrote {out}")
    print("\n".join(table))


if __name__ == "__main__":
    main()
