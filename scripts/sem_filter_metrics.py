#!/usr/bin/env python3
"""Analyze sem_stats.csv — semantic filter impact metrics."""
from __future__ import annotations

import argparse
import csv
import json
import statistics
from pathlib import Path
from typing import Any


def load_sem_rows(csv_path: Path) -> list[dict[str, Any]]:
    if not csv_path.is_file():
        return []
    rows: list[dict[str, Any]] = []
    with csv_path.open() as fh:
        for raw in csv.DictReader(fh):
            row: dict[str, Any] = {}
            for key, val in raw.items():
                if key is None:
                    continue
                k = key.strip()
                if k in ("timestamp_ns", "tracks_before", "rejected", "tracks_after", "mask_available"):
                    row[k] = int(float(val)) if val not in ("", None) else 0
                elif k in ("reject_ratio", "dynamic_pixel_ratio"):
                    row[k] = float(val) if val not in ("", None) else 0.0
                else:
                    row[k] = val
            rows.append(row)
    return rows


def analyze_sem_stats(rows: list[dict[str, Any]]) -> dict[str, Any]:
    if not rows:
        return {"frames": 0}

    reject_ratios = [float(r.get("reject_ratio", 0.0)) for r in rows]
    dynamic_ratios = [float(r.get("dynamic_pixel_ratio", 0.0)) for r in rows]
    rejected = [int(r.get("rejected", 0)) for r in rows]
    mask_ok = sum(1 for r in rows if int(r.get("mask_available", 0)) == 1)
    reject_frames = sum(1 for r in rejected if r > 0)

    return {
        "frames": len(rows),
        "mask_available_pct": 100.0 * mask_ok / len(rows),
        "mean_reject_pct": 100.0 * statistics.mean(reject_ratios),
        "max_reject_pct": 100.0 * max(reject_ratios),
        "frames_with_reject_pct": 100.0 * reject_frames / len(rows),
        "mean_dynamic_pixel_pct": 100.0 * statistics.mean(dynamic_ratios),
        "max_dynamic_pixel_pct": 100.0 * max(dynamic_ratios),
        "total_rejected": sum(rejected),
    }


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("--run-dir", type=Path, required=True)
    ap.add_argument("--json", type=Path, default=None)
    args = ap.parse_args()

    rows = load_sem_rows(args.run_dir / "sem_stats.csv")
    stats = analyze_sem_stats(rows)
    stats["run_dir"] = str(args.run_dir)

    if args.json:
        args.json.parent.mkdir(parents=True, exist_ok=True)
        args.json.write_text(json.dumps(stats, indent=2))

    if stats.get("frames", 0) == 0:
        print(f"[sem] no sem_stats.csv in {args.run_dir}")
        return

    print(
        f"[sem] frames={stats['frames']} mask_ok={stats['mask_available_pct']:.1f}% "
        f"reject={stats['mean_reject_pct']:.2f}% dynamic_px={stats['mean_dynamic_pixel_pct']:.2f}%"
    )


if __name__ == "__main__":
    main()
