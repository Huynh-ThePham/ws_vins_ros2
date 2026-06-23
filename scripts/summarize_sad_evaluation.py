#!/usr/bin/env python3
"""Comprehensive SAD-VINS evaluation report (EuRoC + VIODE), same PASS criteria as GeoDF."""
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


def sem_stats(run_dir: Path | None) -> dict | None:
    if not run_dir:
        return None
    s = run_dir / "sem_stats.csv"
    if not s.is_file():
        return None
    rej, dyn, n, rej_frames, mask_ok = [], [], 0, 0, 0
    with s.open() as fh:
        for row in csv.DictReader(fh):
            n += 1
            rej.append(float(row.get("reject_ratio", 0)))
            dyn.append(float(row.get("dynamic_pixel_ratio", 0)))
            if int(row.get("rejected", 0)) > 0:
                rej_frames += 1
            if int(row.get("mask_available", 0)) == 1:
                mask_ok += 1
    if not n:
        return None
    return {
        "mean_reject_pct": 100 * sum(rej) / n,
        "mean_dynamic_px_pct": 100 * sum(dyn) / n,
        "frames_with_reject_pct": 100 * rej_frames / n,
        "mask_available_pct": 100 * mask_ok / n,
    }


def fmt(v, p=3):
    if v is None:
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

    euroc_root = args.ws / "results" / "sad"
    viode_root = args.ws / "results" / "sad_viode"
    bundle: dict = {"euroc": {}, "viode": {}, "verdict": {}}

    lines = [
        "# SAD-VINS — Comprehensive Evaluation Report",
        "",
        "Setup: **stereo + IMU** (same as baseline). Methods: **baseline** | **sad_sem** (YOLO mask).",
        "",
        f"Static preservation criterion: |Δ ATE| ≤ {STATIC_MAX_DEG_PCT:.0f}% vs baseline on EuRoC.",
        "",
        "## 1. EuRoC Machine Hall (static preservation)",
        "",
        "| Sequence | Method | ATE RMSE (m) | Δ vs base | Reject% | DynPx% | MaskOK% | PASS |",
        "|----------|--------|-------------:|----------:|--------:|-------:|--------:|------|",
    ]

    euroc_pass = 0
    euroc_total = 0
    for seq in EUROC_SEQS:
        base_dir = find_euroc_run(euroc_root, seq, "baseline")
        sad_dir = find_euroc_run(euroc_root, seq, "sad_sem")
        base_m = load_metrics(base_dir)
        sad_m = load_metrics(sad_dir)
        b_ate = base_m.get("ate_rmse_m") if base_m else None
        s_ate = sad_m.get("ate_rmse_m") if sad_m else None
        delta = pct_delta(s_ate, b_ate)
        ss = sem_stats(sad_dir)
        for method, ate, d_run, stats in (
            ("baseline", b_ate, base_dir, None),
            ("sad_sem", s_ate, sad_dir, ss),
        ):
            pass_cell = "—"
            if method == "sad_sem" and delta is not None:
                euroc_total += 1
                ok = abs(delta) <= STATIC_MAX_DEG_PCT
                if ok:
                    euroc_pass += 1
                pass_cell = "PASS" if ok else "FAIL"
            lines.append(
                "| "
                + " | ".join([
                    seq if method == "baseline" else "",
                    method,
                    fmt(ate),
                    (f"{delta:+.1f}%" if method == "sad_sem" and delta is not None else "—"),
                    fmt(stats["mean_reject_pct"] if stats else None, 2),
                    fmt(stats["mean_dynamic_px_pct"] if stats else None, 2),
                    fmt(stats["mask_available_pct"] if stats else None, 1),
                    pass_cell,
                ])
                + " |"
            )
        bundle["euroc"][seq] = {
            "baseline_ate": b_ate, "sad_ate": s_ate, "delta_pct": delta,
            "sem_stats": ss,
        }

    lines += [
        "",
        f"**EuRoC static PASS:** {euroc_pass}/{euroc_total} sequences within ±{STATIC_MAX_DEG_PCT:.0f}%.",
        "",
        "## 2. VIODE city_day (dynamic scenes)",
        "",
        "| Level | Base ATE | SAD ATE | Δ ATE | Reject% | DynPx% | Useful? |",
        "|-------|--------:|--------:|------:|--------:|-------:|---------|",
    ]

    viode_improved = 0
    viode_safe = 0
    for level in VIODE_LEVELS:
        base_m = load_metrics(viode_root / f"city_day_{level}_baseline")
        sad_m = load_metrics(viode_root / f"city_day_{level}_sad_sem")
        b_ate = base_m.get("ate_rmse_m") if base_m else None
        s_ate = sad_m.get("ate_rmse_m") if sad_m else None
        delta = pct_delta(s_ate, b_ate)
        ss = sem_stats(viode_root / f"city_day_{level}_sad_sem")
        useful = "—"
        if delta is not None:
            if level in ("0_none", "1_low"):
                viode_safe += 1 if abs(delta) <= STATIC_MAX_DEG_PCT else 0
                useful = "safe" if abs(delta) <= STATIC_MAX_DEG_PCT else "regressed"
            else:
                if delta < -5:
                    viode_improved += 1
                    useful = "improved"
                elif abs(delta) <= STATIC_MAX_DEG_PCT:
                    useful = "neutral"
                else:
                    useful = "regressed"
        lines.append(
            "| "
            + " | ".join([
                level,
                fmt(b_ate), fmt(s_ate),
                (f"{delta:+.1f}%" if delta is not None else "—"),
                fmt(ss["mean_reject_pct"] if ss else None, 2),
                fmt(ss["mean_dynamic_px_pct"] if ss else None, 2),
                useful,
            ])
            + " |"
        )
        bundle["viode"][level] = {
            "baseline_ate": b_ate, "sad_ate": s_ate, "delta_pct": delta,
            "sem_stats": ss, "useful": useful,
        }

    filter_active = any(
        (sem_stats(find_euroc_run(euroc_root, s, "sad_sem")) or {}).get("mean_reject_pct", 0) > 0.1
        for s in EUROC_SEQS
    ) or any(
        (sem_stats(viode_root / f"city_day_{lv}_sad_sem") or {}).get("mean_reject_pct", 0) > 0.1
        for lv in VIODE_LEVELS
    )

    verdict = {
        "euroc_pass": f"{euroc_pass}/{euroc_total}",
        "viode_improved_mid_high": viode_improved,
        "filter_observed_active": filter_active,
        "overall": "INCONCLUSIVE",
    }
    if euroc_pass == euroc_total and euroc_total > 0 and filter_active:
        if viode_improved >= 1:
            verdict["overall"] = "USEFUL"
        else:
            verdict["overall"] = "SAFE_BUT_MARGINAL"
    bundle["verdict"] = verdict

    lines += [
        "",
        "## 3. Verdict",
        "",
        f"- EuRoC static preservation: **{verdict['euroc_pass']}**",
        f"- Semantic filter active (reject > 0): **{'yes' if filter_active else 'no'}**",
        f"- VIODE mid/high improved (Δ ATE < −5%): **{viode_improved}** levels",
        f"- Overall: **{verdict['overall']}**",
        "",
    ]

    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_text("\n".join(lines))
    args.json.write_text(json.dumps(bundle, indent=2))
    print(f"Wrote {args.out}")


if __name__ == "__main__":
    main()
