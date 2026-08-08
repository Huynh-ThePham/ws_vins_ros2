#!/usr/bin/env python3
"""Dump UrbanNav SPAN-CPT GT to EuRoC-style CSV for evaluate_trajectory.py.

Prefers the official UrbanNavDataset gt_for_slam TUM files when available;
otherwise builds a local ENU trajectory from
``_standardization/ground_truth_standardized.csv``.

    python3 scripts/urbannav_dump_gt.py --root data/UrbanNav --seq medium \\
      --out data/urbannav_gt/medium/gt_euroc.csv
"""
from __future__ import annotations

import argparse
import csv
import math
from pathlib import Path

SEQ_DIRS = {
    "medium": "UrbanNav-HK-Medium-Urban-1",
    "deep": "UrbanNav-HK-Deep-Urban-1",
    "harsh": "UrbanNav-HK-Harsh-Urban-1",
}

GT_SLAM_NAMES = {
    "medium": "gt_slam_UrbanNav-HK-TST-20210517.csv",
    "deep": "gt_slam_UrbanNav-HK-Whampoa-20210521.csv",
    "harsh": "gt_slam_UrbanNav-HK-Mongkok-20210518.csv",
}


def _find_gt_slam(seq: str) -> Path | None:
    name = GT_SLAM_NAMES[seq]
    candidates = [
        Path.home() / "Downloads/UrbanNavDataset-master/tools/evaluation/gt_for_slam" / name,
        Path("/home/theph/Downloads/UrbanNavDataset-master/tools/evaluation/gt_for_slam") / name,
    ]
    for p in candidates:
        if p.is_file():
            return p
    return None


def _quat_from_rpy_deg(roll: float, pitch: float, heading: float) -> tuple[float, float, float, float]:
    """ZYX yaw(heading)-pitch-roll → quaternion (w, x, y, z). Heading is ENU yaw."""
    yaw = math.radians(heading)
    pit = math.radians(pitch)
    rol = math.radians(roll)
    cy, sy = math.cos(yaw * 0.5), math.sin(yaw * 0.5)
    cp, sp = math.cos(pit * 0.5), math.sin(pit * 0.5)
    cr, sr = math.cos(rol * 0.5), math.sin(rol * 0.5)
    qw = cr * cp * cy + sr * sp * sy
    qx = sr * cp * cy - cr * sp * sy
    qy = cr * sp * cy + sr * cp * sy
    qz = cr * cp * sy - sr * sp * cy
    return qw, qx, qy, qz


def _ecef_to_enu(x: float, y: float, z: float,
                 x0: float, y0: float, z0: float,
                 lat0_rad: float, lon0_rad: float) -> tuple[float, float, float]:
    dx, dy, dz = x - x0, y - y0, z - z0
    sl, cl = math.sin(lat0_rad), math.cos(lat0_rad)
    so, co = math.sin(lon0_rad), math.cos(lon0_rad)
    east = -so * dx + co * dy
    north = -sl * co * dx - sl * so * dy + cl * dz
    up = cl * co * dx + cl * so * dy + sl * dz
    return east, north, up


def from_gt_slam(path: Path, out: Path) -> int:
    """TUM (t x y z qx qy qz qw) → EuRoC CSV (ns,x,y,z,qw,qx,qy,qz)."""
    n = 0
    with path.open() as fin, out.open("w") as fout:
        fout.write("# timestamp,p_RS_R_x [m],p_RS_R_y [m],p_RS_R_z [m],"
                   "q_RS_w [],q_RS_x [],q_RS_y [],q_RS_z []\n")
        for line in fin:
            line = line.strip()
            if not line or line.startswith("#"):
                continue
            parts = line.split()
            if len(parts) < 8:
                continue
            t = float(parts[0])
            x, y, z = float(parts[1]), float(parts[2]), float(parts[3])
            qx, qy, qz, qw = (float(parts[4]), float(parts[5]),
                              float(parts[6]), float(parts[7]))
            ts_ns = int(round(t * 1e9))
            fout.write(f"{ts_ns},{x:.9f},{y:.9f},{z:.9f},"
                       f"{qw:.9f},{qx:.9f},{qy:.9f},{qz:.9f}\n")
            n += 1
    return n


def from_standardized(path: Path, out: Path) -> int:
    rows = list(csv.DictReader(path.open()))
    if not rows:
        return 0
    lat0 = math.radians(float(rows[0]["lat_deg"]))
    lon0 = math.radians(float(rows[0]["lon_deg"]))
    x0, y0, z0 = (float(rows[0]["ecef_x"]), float(rows[0]["ecef_y"]),
                  float(rows[0]["ecef_z"]))
    n = 0
    with out.open("w") as fout:
        fout.write("# timestamp,p_RS_R_x [m],p_RS_R_y [m],p_RS_R_z [m],"
                   "q_RS_w [],q_RS_x [],q_RS_y [],q_RS_z []\n")
        for r in rows:
            t = float(r["utc_time"])
            e, n_, u = _ecef_to_enu(
                float(r["ecef_x"]), float(r["ecef_y"]), float(r["ecef_z"]),
                x0, y0, z0, lat0, lon0)
            qw, qx, qy, qz = _quat_from_rpy_deg(
                float(r["roll_deg"]), float(r["pitch_deg"]), float(r["heading_deg"]))
            ts_ns = int(round(t * 1e9))
            fout.write(f"{ts_ns},{e:.9f},{n_:.9f},{u:.9f},"
                       f"{qw:.9f},{qx:.9f},{qy:.9f},{qz:.9f}\n")
            n += 1
    return n


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--root", type=Path, required=True, help="UrbanNav dataset root")
    ap.add_argument("--seq", required=True, choices=sorted(SEQ_DIRS))
    ap.add_argument("--out", type=Path, required=True)
    args = ap.parse_args()

    args.out.parent.mkdir(parents=True, exist_ok=True)
    gt_slam = _find_gt_slam(args.seq)
    if gt_slam is not None:
        n = from_gt_slam(gt_slam, args.out)
        print(f"[gt] {args.seq}: {n} poses from {gt_slam} -> {args.out}")
        return 0 if n else 1

    std = args.root / SEQ_DIRS[args.seq] / "_standardization" / "ground_truth_standardized.csv"
    if not std.is_file():
        raise SystemExit(f"no GT found for {args.seq} (checked gt_slam + {std})")
    n = from_standardized(std, args.out)
    print(f"[gt] {args.seq}: {n} poses from {std} -> {args.out}")
    return 0 if n else 1


if __name__ == "__main__":
    raise SystemExit(main())
