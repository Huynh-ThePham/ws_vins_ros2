#!/usr/bin/env python3
"""Side-by-side GeoDF benchmark comparison: ws_vins (ROS1) vs ws_vins_ros2 (ROS2)."""
from __future__ import annotations

import argparse
import csv
import json
import statistics
from pathlib import Path


def _ate(run_dir: Path) -> float | None:
    p = run_dir / "eval" / "metrics.json"
    if not p.is_file():
        return None
    data = json.loads(p.read_text())
    if "ate_rmse_m" in data:
        return data["ate_rmse_m"]
    return data.get("ate", {}).get("rmse")


def _geo_summary(run_dir: Path) -> dict[str, float] | None:
    stats = run_dir / "geo_df_stats.csv"
    if not stats.is_file():
        return None
    ratios: list[float] = []
    rejected: list[int] = []
    active: list[int] = []
    with stats.open() as f:
        for row in csv.DictReader(f):
            try:
                ratios.append(float(row["reject_ratio"]))
            except (KeyError, ValueError):
                continue
            try:
                rejected.append(int(row["rejected"]))
            except (KeyError, ValueError):
                rejected.append(0)
            if row.get("frame_active") not in (None, ""):
                try:
                    active.append(int(row["frame_active"]))
                except ValueError:
                    pass
    if not ratios:
        return None
    n = len(ratios)
    return {
        "mean_reject_ratio": statistics.mean(ratios),
        "reject_frames_pct": 100.0 * sum(1 for r in rejected if r > 0) / n,
        "active_frames_pct": 100.0 * sum(active) / len(active) if active else float("nan"),
    }


def _run_dir(root: Path, seq: str, method: str, start: str) -> Path:
    tag = start.replace(".", "p")
    return root / f"{seq}_{method}_s{tag}"


def _fmt(v: float | None, prec: int = 3) -> str:
    if v is None:
        return "—"
    try:
        if v != v:
            return "—"
    except TypeError:
        return "—"
    return f"{v:.{prec}f}"


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("--ros1-root", type=Path, required=True)
    ap.add_argument("--ros2-root", type=Path, required=True)
    ap.add_argument("--seq", required=True)
    ap.add_argument("--method", default="geodf_hard")
    ap.add_argument("--start", default="40")
    ap.add_argument("--out", type=Path, required=True)
    args = ap.parse_args()

    d1 = _run_dir(args.ros1_root, args.seq, args.method, args.start)
    d2 = _run_dir(args.ros2_root, args.seq, args.method, args.start)

    ate1, ate2 = _ate(d1), _ate(d2)
    g1, g2 = _geo_summary(d1), _geo_summary(d2)

    delta = "—"
    if ate1 is not None and ate2 is not None:
        delta = f"{(ate2 - ate1):+.3f} m ({(ate2 - ate1) / ate1 * 100:+.1f}%)"

    lines = [
        f"# GeoDF repo comparison: {args.seq} / {args.method}",
        "",
        f"| Metric | ws_vins (ROS1) | ws_vins_ros2 (ROS2) | Δ |",
        f"| --- | --- | --- | --- |",
        f"| ATE RMSE (m) | {_fmt(ate1)} | {_fmt(ate2)} | {delta} |",
    ]
    if g1 or g2:
        lines.append(
            f"| Mean reject ratio | "
            f"{_fmt(g1['mean_reject_ratio'] * 100 if g1 else None, 2)}% | "
            f"{_fmt(g2['mean_reject_ratio'] * 100 if g2 else None, 2)}% | — |"
        )
        lines.append(
            f"| Frames with rejection | "
            f"{_fmt(g1['reject_frames_pct'] if g1 else None, 1)}% | "
            f"{_fmt(g2['reject_frames_pct'] if g2 else None, 1)}% | — |"
        )
    lines += [
        "",
        f"ROS1 run: `{d1}`",
        f"ROS2 run: `{d2}`",
        "",
        "**Acceptance:** ROS2 ATE within ±5% of ROS1 for static EuRoC (parity with baseline port).",
    ]

    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_text("\n".join(lines) + "\n")
    print(f"[ok] wrote {args.out}")


if __name__ == "__main__":
    main()
