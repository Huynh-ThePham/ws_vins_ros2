#!/usr/bin/env python3
"""Comprehensive GeoDF-Adaptive evaluation report with PASS/FAIL criteria."""
from __future__ import annotations

import argparse
import csv
import glob
import json
from pathlib import Path


EUROC_SEQS = [
    "MH_01_easy", "MH_02_easy", "MH_03_medium",
    "MH_04_difficult", "MH_05_difficult",
]
VIODE_LEVELS = ["0_none", "1_low", "2_mid", "3_high"]
STATIC_MAX_DEG_PCT = 20.0


def find_euroc_run(root: Path, seq: str, method: str) -> Path | None:
    hits = sorted(glob.glob(str(root / f"{seq}_{method}_s*")))
    return Path(hits[-1]) if hits else None


def load_metrics(run_dir: Path | None) -> dict | None:
    if not run_dir:
        return None
    f = run_dir / "eval" / "metrics.json"
    if not f.is_file():
        return None
    return json.loads(f.read_text())


def filter_stats(run_dir: Path | None) -> dict | None:
    if not run_dir:
        return None
    s = run_dir / "geo_df_stats.csv"
    if not s.is_file():
        return None
    rej, armed, n, rej_frames = [], [], 0, 0
    with s.open() as fh:
        for row in csv.DictReader(fh):
            n += 1
            rej.append(float(row.get("reject_ratio", 0)))
            if int(row.get("rejected", 0)) > 0:
                rej_frames += 1
            if row.get("frame_active") not in (None, ""):
                armed.append(int(row["frame_active"]))
    if not n:
        return None
    return {
        "mean_reject_pct": 100 * sum(rej) / n,
        "frames_with_reject_pct": 100 * rej_frames / n,
        "armed_pct": 100 * sum(armed) / len(armed) if armed else None,
    }


def fmt(v, p=3):
    if v is None:
        return "—"
    try:
        if v != v:
            return "—"
    except TypeError:
        return "—"
    return f"{v:.{p}f}"


def pct_delta(new, base):
    if base is None or new is None or base == 0:
        return None
    return 100.0 * (new - base) / base


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("--ws", type=Path, required=True)
    ap.add_argument("--out", type=Path, required=True)
    ap.add_argument("--json", type=Path, required=True)
    args = ap.parse_args()

    geodf = args.ws / "results" / "geodf"
    viode = args.ws / "results" / "viode"

    bundle: dict = {"euroc": {}, "viode": {}, "verdict": {}}

    lines = [
        "# GeoDF-Adaptive — Comprehensive Evaluation Report",
        "",
        "Methods: **baseline** | **alwayson** (geodf_dump) | **adaptive** (proposed)",
        "",
        "Metrics per run: ATE RMSE, RPE RMSE, ATE max, pose count + filter stats.",
        "",
        "## 1. EuRoC Machine Hall (static preservation + accuracy)",
        "",
        "| Sequence | Method | ATE RMSE (m) | RPE RMSE (m) | ATE max (m) | Δ ATE vs base | Reject% | Armed% |",
        "|----------|--------|-------------:|-------------:|------------:|--------------:|--------:|-------:|",
    ]

    euroc_pass = 0
    euroc_total = 0
    for seq in EUROC_SEQS:
        bundle["euroc"][seq] = {}
        base_m = load_metrics(find_euroc_run(geodf, seq, "baseline"))
        base_ate = base_m.get("ate_rmse_m") if base_m else None
        for method in ("baseline", "alwayson", "adaptive"):
            rd = find_euroc_run(geodf, seq, method)
            m = load_metrics(rd)
            fs = filter_stats(rd)
            bundle["euroc"][seq][method] = {"metrics": m, "filter": fs, "path": str(rd) if rd else None}
            if not m:
                lines.append(f"| {seq} | {method} | — | — | — | — | — | — |")
                continue
            d = pct_delta(m["ate_rmse_m"], base_ate)
            ds = f"{d:+.1f}%" if d is not None and method != "baseline" else "—"
            rp = fmt(fs["mean_reject_pct"], 2) + "%" if fs else "—"
            apct = fmt(fs["armed_pct"], 1) + "%" if fs and fs.get("armed_pct") is not None else "—"
            lines.append(
                f"| {seq} | {method} | {fmt(m['ate_rmse_m'])} | {fmt(m.get('rpe_rmse_m'))} | "
                f"{fmt(m.get('ate_max_m'))} | {ds} | {rp} | {apct} |"
            )
            if method == "adaptive" and base_ate and d is not None:
                euroc_total += 1
                if abs(d) <= STATIC_MAX_DEG_PCT:
                    euroc_pass += 1

    lines += [
        "",
        f"**Static criterion:** adaptive ATE within ±{STATIC_MAX_DEG_PCT:.0f}% of baseline → "
        f"**{euroc_pass}/{euroc_total}** sequences PASS.",
        "",
        "## 2. VIODE city_day (real dynamic — accuracy gain)",
        "",
        "| Level | Method | ATE RMSE (m) | RPE RMSE (m) | ATE max (m) | Δ ATE vs base | Reject% | Armed% |",
        "|-------|--------|-------------:|-------------:|------------:|--------------:|--------:|-------:|",
    ]

    viode_dynamic_win = 0
    viode_static_ok = 0
    for level in VIODE_LEVELS:
        bundle["viode"][level] = {}
        base_m = load_metrics(viode / f"city_day_{level}_baseline")
        base_ate = base_m.get("ate_rmse_m") if base_m else None
        for method in ("baseline", "geodf_dump", "adaptive"):
            rd = viode / f"city_day_{level}_{method}"
            m = load_metrics(rd)
            fs = filter_stats(rd)
            bundle["viode"][level][method] = {"metrics": m, "filter": fs, "path": str(rd)}
            label = "alwayson" if method == "geodf_dump" else method
            if not m:
                lines.append(f"| {level} | {label} | — | — | — | — | — | — |")
                continue
            d = pct_delta(m["ate_rmse_m"], base_ate)
            ds = f"{d:+.1f}%" if d is not None and method != "baseline" else "—"
            rp = fmt(fs["mean_reject_pct"], 2) + "%" if fs else "—"
            apct = fmt(fs["armed_pct"], 1) + "%" if fs and fs.get("armed_pct") is not None else "—"
            lines.append(
                f"| {level} | {label} | {fmt(m['ate_rmse_m'])} | {fmt(m.get('rpe_rmse_m'))} | "
                f"{fmt(m.get('ate_max_m'))} | {ds} | {rp} | {apct} |"
            )
        adapt_m = load_metrics(viode / f"city_day_{level}_adaptive")
        if base_ate and adapt_m:
            d_ad = pct_delta(adapt_m["ate_rmse_m"], base_ate)
            if level in ("2_mid", "3_high") and d_ad is not None and d_ad < 0:
                viode_dynamic_win += 1
            if level in ("0_none", "1_low") and d_ad is not None and d_ad <= 5.0:
                viode_static_ok += 1

    lines += [
        "",
        f"**Dynamic criterion:** adaptive beats baseline on 2_mid/3_high → **{viode_dynamic_win}/2** PASS.",
        f"**Low-dynamic criterion:** adaptive within +5% on 0_none/1_low → **{viode_static_ok}/2** PASS.",
        "",
        "## 3. Overall verdict (GeoDF-Adaptive proposed method)",
        "",
    ]

    overall = (
        euroc_pass == euroc_total
        and euroc_total >= 4
        and viode_dynamic_win >= 1
        and viode_static_ok >= 1
    )
    bundle["verdict"] = {
        "euroc_adaptive_pass": f"{euroc_pass}/{euroc_total}",
        "viode_dynamic_improve": viode_dynamic_win,
        "viode_low_dynamic_ok": viode_static_ok,
        "overall_pass": overall,
    }

    if overall:
        lines.append("### ✅ PASS — GeoDF-Adaptive shows genuine accuracy improvement with static safety")
    else:
        lines.append("### ⚠ PARTIAL — Review failed criteria below")
    lines += [
        "",
        f"- EuRoC adaptive static safety: {euroc_pass}/{euroc_total} (need all ≤{STATIC_MAX_DEG_PCT:.0f}%)",
        f"- VIODE dynamic gain (2_mid + 3_high): {viode_dynamic_win}/2 levels improved",
        f"- VIODE low-dynamic safety (0_none + 1_low): {viode_static_ok}/2 within +5%",
        "",
        "## 4. Key comparisons for paper",
        "",
    ]

    # Paper table: adaptive vs baseline on key scenes
    key_rows = [
        ("EuRoC MH_01", find_euroc_run(geodf, "MH_01_easy", "baseline"),
         find_euroc_run(geodf, "MH_01_easy", "adaptive")),
        ("EuRoC MH_03", find_euroc_run(geodf, "MH_03_medium", "baseline"),
         find_euroc_run(geodf, "MH_03_medium", "adaptive")),
        ("VIODE 0_none", viode / "city_day_0_none_baseline", viode / "city_day_0_none_adaptive"),
        ("VIODE 3_high", viode / "city_day_3_high_baseline", viode / "city_day_3_high_adaptive"),
    ]
    lines += ["| Scene | Baseline ATE | Adaptive ATE | Improvement |", "|-------|-------------:|-------------:|------------:|"]
    for label, bdir, adir in key_rows:
        bm, am = load_metrics(bdir), load_metrics(adir)
        if bm and am:
            imp = pct_delta(am["ate_rmse_m"], bm["ate_rmse_m"])
            lines.append(
                f"| {label} | {fmt(bm['ate_rmse_m'])} | {fmt(am['ate_rmse_m'])} | "
                f"{imp:+.1f}%" if imp is not None else f"| {label} | — | — | — |"
            )

    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_text("\n".join(lines) + "\n")
    args.json.write_text(json.dumps(bundle, indent=2) + "\n")
    print(f"[ok] {args.out}")
    print(f"[ok] {args.json}")
    print("\n".join(lines[-15:]))


if __name__ == "__main__":
    main()
