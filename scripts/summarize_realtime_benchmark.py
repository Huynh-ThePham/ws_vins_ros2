#!/usr/bin/env python3
"""Summarize async ROS 2 realtime / latency benchmark for SAD-VINS / SGTA-VINS."""
from __future__ import annotations

import argparse
import csv
import json
import re
import statistics
from pathlib import Path
from typing import Any


def read_bag_meta(bag_dir: Path) -> dict[str, Any]:
    meta_path = bag_dir / "metadata.yaml"
    if not meta_path.is_file():
        return {}
    text = meta_path.read_text()
    dur_match = re.search(r"duration:\s*\n\s*nanoseconds:\s*(\d+)", text)
    cam_match = re.search(
        r"name: /cam0/image_raw\s*\n\s*offered_qos_profiles:.*?\n\s*"
        r"serialization_format:.*?\n\s*type:.*?\n\s*- message_count: (\d+)",
        text,
        re.DOTALL,
    )
    if not cam_match:
        cam_match = re.search(
            r"message_count: (\d+)\s*\n\s*topic_metadata:\s*\n\s*name: /cam0/image_raw",
            text,
        )
    out: dict[str, Any] = {}
    if dur_match:
        out["bag_duration_s"] = int(dur_match.group(1)) / 1e9
    if cam_match:
        out["cam0_messages"] = int(cam_match.group(1))
    return out


def parse_yolo_log(log_path: Path) -> dict[str, Any]:
    if not log_path.is_file():
        return {}
    latencies: list[float] = []
    skipped = 0
    for line in log_path.read_text().splitlines():
        m = re.search(r"YOLO avg latency: ([0-9.]+) ms \(skipped=(\d+)\)", line)
        if m:
            latencies.append(float(m.group(1)))
            skipped = int(m.group(2))
    if not latencies:
        return {}
    return {
        "yolo_avg_ms": latencies[-1],
        "yolo_avg_ms_mean_log": statistics.mean(latencies),
        "yolo_skipped_frames": skipped,
    }


def percentile(values: list[float], q: float) -> float:
    if not values:
        return 0.0
    ordered = sorted(values)
    idx = min(len(ordered) - 1, int(q * (len(ordered) - 1)))
    return ordered[idx]


def load_csv_col(path: Path, col: str) -> list[float]:
    if not path.is_file():
        return []
    out: list[float] = []
    with path.open() as fh:
        for row in csv.DictReader(fh):
            val = row.get(col, "")
            if val in ("", None):
                continue
            try:
                out.append(float(val))
            except ValueError:
                pass
    return out


def analyze_run(run_dir: Path, meta: dict[str, Any] | None = None) -> dict[str, Any]:
    cfg = {}
    cfg_path = run_dir / "run_config.json"
    if cfg_path.is_file():
        cfg = json.loads(cfg_path.read_text())

    bag_dir = Path(cfg.get("bag", "")) if cfg.get("bag") else None
    bag_meta = read_bag_meta(bag_dir) if bag_dir and bag_dir.is_dir() else {}
    if meta:
        bag_meta.update(meta)

    sem_rows = 0
    sem_path = run_dir / "sem_stats.csv"
    if sem_path.is_file():
        with sem_path.open() as fh:
            sem_rows = sum(1 for _ in csv.DictReader(fh))

    vio_poses = 0
    vio_path = run_dir / "vio.csv"
    if vio_path.is_file():
        with vio_path.open() as fh:
            vio_poses = sum(1 for _ in fh)

    mask_lags = load_csv_col(sem_path, "mask_lag_ms")
    mask_lags = [v for v in mask_lags if v >= 0]
    mask_trusted = load_csv_col(sem_path, "mask_trusted")
    geo_ms = load_csv_col(run_dir / "geo_df_stats.csv", "geo_ms")

    metrics_path = run_dir / "eval" / "metrics.json"
    ate_rmse = None
    if metrics_path.is_file():
        ate_rmse = json.loads(metrics_path.read_text()).get("ate_rmse_m")

    cam_msgs = bag_meta.get("cam0_messages")
    bag_duration = bag_meta.get("bag_duration_s")
    bag_rate = float(cfg.get("bag_rate", 1.0))
    wall_s = cfg.get("wall_play_s")
    vins_freq_hz = float(cfg.get("vins_freq_hz", 10.0))
    bag_start_s = float(cfg.get("bag_start_s", 0.0))

    tracker_frames = sem_rows if sem_rows else vio_poses
    expected_poses = None
    effective_duration = None
    if bag_duration is not None:
        effective_duration = max(0.0, bag_duration - bag_start_s)
        expected_poses = effective_duration * vins_freq_hz

    frame_coverage = None
    pose_coverage = None
    if cam_msgs and sem_rows:
        frame_coverage = 100.0 * sem_rows / cam_msgs
    if expected_poses and vio_poses:
        pose_coverage = 100.0 * vio_poses / expected_poses

    pose_rate = None
    if bag_duration and vio_poses:
        pose_rate = vio_poses / bag_duration

    expected_wall = None
    realtime_ratio = None
    if bag_duration and wall_s:
        expected_wall = bag_duration / bag_rate if bag_rate > 0 else bag_duration
        realtime_ratio = expected_wall / wall_s if wall_s > 0 else None

    result: dict[str, Any] = {
        "run_dir": str(run_dir),
        "name": cfg.get("name", run_dir.name),
        "method": cfg.get("method"),
        "bag_rate": bag_rate,
        "process_every_n": cfg.get("process_every_n"),
        "use_yolo": cfg.get("use_yolo"),
        "bag_duration_s": bag_duration,
        "effective_duration_s": effective_duration,
        "cam0_messages": cam_msgs,
        "tracker_frames": tracker_frames,
        "vio_poses": vio_poses,
        "expected_poses": expected_poses,
        "frame_coverage_pct": frame_coverage,
        "pose_coverage_pct": pose_coverage,
        "pose_rate_hz": pose_rate,
        "wall_play_s": wall_s,
        "expected_wall_s": expected_wall,
        "realtime_ratio": realtime_ratio,
        "ate_rmse_m": ate_rmse,
        **parse_yolo_log(run_dir / "yolo_mask_node.log"),
    }

    if mask_lags:
        result.update(
            {
                "mask_lag_ms_mean": statistics.mean(mask_lags),
                "mask_lag_ms_p95": percentile(mask_lags, 0.95),
                "mask_lag_ms_max": max(mask_lags),
            }
        )
    if mask_trusted:
        result["mask_trusted_pct"] = 100.0 * sum(1 for v in mask_trusted if v >= 0.5) / len(
            mask_trusted
        )
    if geo_ms:
        result["geo_ms_mean"] = statistics.mean(geo_ms)
        result["geo_ms_p95"] = percentile(geo_ms, 0.95)

    # Realtime pass heuristics for async ROS 2 pipeline.
    passes: list[str] = []
    fails: list[str] = []
    if pose_coverage is not None:
        if pose_coverage >= 95.0:
            passes.append("pose_coverage>=95%")
        else:
            fails.append(f"pose_coverage={pose_coverage:.1f}%")
    if frame_coverage is not None and cfg.get("use_yolo"):
        if frame_coverage >= 95.0:
            passes.append("tracker_frames>=95%cam")
        else:
            fails.append(f"tracker_vs_cam={frame_coverage:.1f}%")
    if realtime_ratio is not None:
        if realtime_ratio >= 0.85:
            passes.append("wall_clock~realtime")
        else:
            fails.append(f"wall_clock_ratio={realtime_ratio:.2f}(incl_yolo_warmup)")
    if mask_lags and result.get("mask_lag_ms_p95", 0) <= 150.0:
        passes.append("mask_lag_p95<=150ms")
    elif mask_lags:
        fails.append(f"mask_lag_p95={result.get('mask_lag_ms_p95', 0):.1f}ms")

    result["realtime_pass"] = (
        (pose_coverage is None or pose_coverage >= 95.0)
        and (not mask_lags or result.get("mask_lag_ms_p95", 999) <= 150.0)
    )
    result["realtime_checks_pass"] = passes
    result["realtime_checks_fail"] = fails
    return result


def print_table(rows: list[dict[str, Any]]) -> None:
    headers = [
        "name",
        "rate",
        "yolo_n",
        "poses",
        "pose_cov%",
        "trk/cam%",
        "pose_hz",
        "wall_s",
        "rt_ratio",
        "yolo_ms",
        "lag_p95",
        "geo_ms",
        "ate",
        "pass",
    ]
    print("\t".join(headers))
    for r in rows:
        print(
            "\t".join(
                [
                    str(r.get("name", "")),
                    str(r.get("bag_rate", "")),
                    str(r.get("process_every_n", "-")),
                    str(r.get("vio_poses", "")),
                    f"{r.get('pose_coverage_pct', 0):.1f}" if r.get("pose_coverage_pct") else "-",
                    f"{r.get('frame_coverage_pct', 0):.1f}" if r.get("frame_coverage_pct") else "-",
                    f"{r.get('pose_rate_hz', 0):.1f}" if r.get("pose_rate_hz") else "-",
                    f"{r.get('wall_play_s', 0):.1f}" if r.get("wall_play_s") else "-",
                    f"{r.get('realtime_ratio', 0):.2f}" if r.get("realtime_ratio") else "-",
                    f"{r.get('yolo_avg_ms', 0):.1f}" if r.get("yolo_avg_ms") else "-",
                    f"{r.get('mask_lag_ms_p95', 0):.0f}" if r.get("mask_lag_ms_p95") else "-",
                    f"{r.get('geo_ms_mean', 0):.2f}" if r.get("geo_ms_mean") else "-",
                    f"{r.get('ate_rmse_m', 0):.3f}" if r.get("ate_rmse_m") is not None else "-",
                    "YES" if r.get("realtime_pass") else "no",
                ]
            )
        )


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--run-dir", type=Path, help="Single run output directory")
    parser.add_argument("--root", type=Path, help="Benchmark root with multiple runs")
    parser.add_argument("--json-out", type=Path, help="Write JSON summary")
    args = parser.parse_args()

    rows: list[dict[str, Any]] = []
    if args.run_dir:
        rows.append(analyze_run(args.run_dir))
    elif args.root:
        for child in sorted(args.root.iterdir()):
            if child.is_dir() and (child / "run_config.json").is_file():
                rows.append(analyze_run(child))
    else:
        parser.error("Provide --run-dir or --root")

    print_table(rows)
    if args.json_out:
        args.json_out.parent.mkdir(parents=True, exist_ok=True)
        args.json_out.write_text(json.dumps(rows, indent=2) + "\n")


if __name__ == "__main__":
    main()
