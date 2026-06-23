#!/usr/bin/env python3
"""Analyze geo_df_stats.csv — filter impact metrics for paper/review."""
from __future__ import annotations

import csv
import json
import math
import re
import statistics
from pathlib import Path
from typing import Any


def _pct(n: int, d: int) -> float:
    return 100.0 * n / d if d > 0 else 0.0


def _percentile(vals: list[float], p: float) -> float:
    if not vals:
        return 0.0
    s = sorted(vals)
    k = (len(s) - 1) * p / 100.0
    f = int(k)
    c = min(f + 1, len(s) - 1)
    if f == c:
        return s[f]
    return s[f] + (s[c] - s[f]) * (k - f)


def load_geo_df_rows(csv_path: Path) -> list[dict[str, Any]]:
    if not csv_path.is_file():
        return []
    rows: list[dict[str, Any]] = []
    with csv_path.open() as f:
        reader = csv.DictReader(f)
        for raw in reader:
            row: dict[str, Any] = {}
            for k, v in raw.items():
                if k is None:
                    continue
                key = k.strip()
                if key in (
                    "timestamp_ns",
                    "tracks_before",
                    "scored",
                    "ransac_outliers",
                    "sampson_above_th",
                    "candidates",
                    "rejected",
                    "tracks_after",
                    "guard_triggered",
                    "guard_capped",
                ):
                    row[key] = int(float(v)) if v not in ("", None) else 0
                elif key in (
                    "reject_ratio",
                    "mean_sampson",
                    "median_sampson",
                    "max_sampson",
                    "geo_ms",
                ):
                    row[key] = float(v) if v not in ("", None) else 0.0
                else:
                    row[key] = v
            # Legacy CSV (no ransac_outliers / sampson_above_th / geo_ms)
            if "candidates" not in row and "rejected" in row:
                pass
            if "ransac_outliers" not in row:
                row["ransac_outliers"] = 0
            if "sampson_above_th" not in row:
                row["sampson_above_th"] = 0
            if "geo_ms" not in row:
                row["geo_ms"] = 0.0
            if "guard_capped" not in row:
                row["guard_capped"] = int(row.get("guard_triggered", 0))
            rows.append(row)
    return rows


def analyze_filter_impact(rows: list[dict[str, Any]], sampson_th: float = 3.0) -> dict[str, Any]:
    """Aggregate per-run filter impact statistics."""
    if not rows:
        return {"frames": 0}

    n = len(rows)
    scored_vals = [r["scored"] for r in rows]
    cand_vals = [r["candidates"] for r in rows]
    rej_vals = [r["rejected"] for r in rows]
    ratio_vals = [r["reject_ratio"] for r in rows]
    max_sampson_vals = [r["max_sampson"] for r in rows]
    geo_ms_vals = [r["geo_ms"] for r in rows if r["geo_ms"] > 0]

    frames_scored = sum(1 for s in scored_vals if s > 0)
    frames_with_candidates = sum(1 for c in cand_vals if c > 0)
    frames_with_reject = sum(1 for r in rej_vals if r > 0)
    frames_guard = sum(1 for r in rows if r["guard_triggered"])
    frames_guard_capped = sum(1 for r in rows if r.get("guard_capped", 0))

    total_scored = sum(scored_vals)
    total_ransac_out = sum(r.get("ransac_outliers", 0) for r in rows)
    total_sampson_hi = sum(r.get("sampson_above_th", 0) for r in rows)
    total_candidates = sum(cand_vals)
    total_rejected = sum(rej_vals)

    rej_when_active = [r for r in rej_vals if r > 0]
    cand_when_active = [c for c in cand_vals if c > 0]

    # Per-scored-track rates (macro over frames with scored>0)
    cand_per_scored_frames = [
        r["candidates"] / r["scored"] for r in rows if r["scored"] > 0
    ]
    rej_per_scored_frames = [
        r["rejected"] / r["scored"] for r in rows if r["scored"] > 0
    ]

    dual_gate_reduction = float("nan")
    if total_sampson_hi > 0:
        dual_gate_reduction = 100.0 * (1.0 - total_candidates / total_sampson_hi)

    return {
        "frames": n,
        "frames_scored_pct": _pct(frames_scored, n),
        "frames_with_candidates_pct": _pct(frames_with_candidates, n),
        "frames_with_reject_pct": _pct(frames_with_reject, n),
        "frames_guard_triggered_pct": _pct(frames_guard, n),
        "frames_guard_capped_pct": _pct(frames_guard_capped, n),
        "mean_tracks_before": statistics.mean(r["tracks_before"] for r in rows),
        "mean_scored_per_frame": statistics.mean(scored_vals),
        "mean_candidates_per_frame": statistics.mean(cand_vals),
        "mean_rejected_per_frame": statistics.mean(rej_vals),
        "mean_rejected_when_active": statistics.mean(rej_when_active) if rej_when_active else 0.0,
        "max_rejected_per_frame": max(rej_vals),
        "mean_reject_ratio": statistics.mean(ratio_vals),
        "max_reject_ratio": max(ratio_vals),
        "total_features_scored": total_scored,
        "total_ransac_outliers": total_ransac_out,
        "total_sampson_above_th": total_sampson_hi,
        "total_candidates": total_candidates,
        "total_rejected": total_rejected,
        "candidate_rate_per_scored": total_candidates / total_scored if total_scored else 0.0,
        "reject_rate_per_scored": total_rejected / total_scored if total_scored else 0.0,
        "reject_per_candidate": total_rejected / total_candidates if total_candidates else 0.0,
        "dual_gate_reduction_pct": dual_gate_reduction,
        "mean_candidate_rate_per_frame": statistics.mean(cand_per_scored_frames) if cand_per_scored_frames else 0.0,
        "mean_reject_rate_per_scored_frame": statistics.mean(rej_per_scored_frames) if rej_per_scored_frames else 0.0,
        "mean_max_sampson": statistics.mean(max_sampson_vals),
        "p95_max_sampson": _percentile(max_sampson_vals, 95),
        "frames_max_sampson_above_th_pct": _pct(
            sum(1 for m in max_sampson_vals if m > sampson_th), n
        ),
        "mean_geo_ms": statistics.mean(geo_ms_vals) if geo_ms_vals else 0.0,
        "p95_geo_ms": _percentile(geo_ms_vals, 95) if geo_ms_vals else 0.0,
    }


def analyze_run_dir(run_dir: Path, sampson_th: float = 3.0) -> dict[str, Any]:
    rows = load_geo_df_rows(run_dir / "geo_df_stats.csv")
    metrics = analyze_filter_impact(rows, sampson_th=sampson_th)
    metrics["run_dir"] = run_dir.name
    return metrics


def write_run_metrics(run_dir: Path, sampson_th: float = 3.0) -> Path | None:
    m = analyze_run_dir(run_dir, sampson_th=sampson_th)
    if m.get("frames", 0) == 0:
        return None
    out = run_dir / "geodf_filter_metrics.json"
    out.write_text(json.dumps(m, indent=2) + "\n")
    return out


def _fmt_pct(v: float) -> str:
    return f"{v:.2f}%"


def _fmt_f(v: float, prec: int = 3) -> str:
    return f"{v:.{prec}f}"


def impact_table_row(label: str, m: dict[str, Any]) -> list[str]:
    if m.get("frames", 0) == 0:
        return [label] + ["—"] * 11
    return [
        label,
        str(m["frames"]),
        _fmt_pct(m["frames_with_reject_pct"]),
        _fmt_pct(m["mean_reject_ratio"] * 100),
        str(m["total_rejected"]),
        str(m["total_candidates"]),
        _fmt_pct(m["candidate_rate_per_scored"] * 100),
        "—" if math.isnan(m["dual_gate_reduction_pct"]) else _fmt_pct(m["dual_gate_reduction_pct"]),
        _fmt_pct(m["frames_guard_triggered_pct"]),
        _fmt_f(m["mean_max_sampson"], 2),
        _fmt_f(m["mean_geo_ms"], 2) if m["mean_geo_ms"] > 0 else "—",
        _fmt_f(m["mean_rejected_when_active"], 1),
    ]


IMPACT_HEADERS = [
    "Run",
    "Frames",
    "Frames w/ reject",
    "Mean reject ratio",
    "Total rejected",
    "Total candidates",
    "Cand/scored",
    "Dual-gate reduction",
    "Guard triggered",
    "Mean max Sampson",
    "GeoDF ms",
    "Reject when active",
]


def md_table(headers: list[str], rows: list[list[str]]) -> str:
    lines = [
        "| " + " | ".join(headers) + " |",
        "| " + " | ".join("---" for _ in headers) + " |",
    ]
    for row in rows:
        lines.append("| " + " | ".join(row) + " |")
    return "\n".join(lines)


if __name__ == "__main__":
    import argparse

    ap = argparse.ArgumentParser()
    ap.add_argument("--run-dir", type=Path, required=True)
    ap.add_argument("--sampson-th", type=float, default=3.0)
    args = ap.parse_args()
    out = write_run_metrics(args.run_dir, sampson_th=args.sampson_th)
    if out:
        print(f"[ok] {out}")
    else:
        raise SystemExit(f"no geo_df_stats in {args.run_dir}")
