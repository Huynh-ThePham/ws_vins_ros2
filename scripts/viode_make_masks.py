#!/usr/bin/env python3
"""Build per-frame dynamic-object masks from VIODE /cam0/segmentation (rosbags, no ROS1)."""
from __future__ import annotations

import argparse
import csv
from pathlib import Path

import cv2
import numpy as np
from rosbags.rosbag1 import Reader
from rosbags.typesys import Stores, get_typestore


def load_dynamic_colors(rgb_ids: Path, vehicle_ids: Path) -> list[tuple[int, int, int]]:
    id2rgb: dict[int, tuple[int, int, int]] = {}
    with rgb_ids.open() as f:
        for row in csv.DictReader(f):
            id2rgb[int(row["id"])] = (int(row["r"]), int(row["g"]), int(row["b"]))
    dyn_ids: list[int] = []
    with vehicle_ids.open() as f:
        for line in f:
            line = line.strip()
            if not line or line.lower().startswith("id"):
                continue
            parts = [p.strip() for p in line.split(",")]
            if len(parts) < 2:
                continue
            name = parts[1].lower()
            if "dynamic" in name and "static" not in name:
                dyn_ids.append(int(parts[0]))
    colors = [id2rgb[i] for i in dyn_ids if i in id2rgb]
    print(f"[masks] dynamic ids={dyn_ids} -> {len(colors)} colors")
    return colors


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("--bag", required=True, type=Path)
    ap.add_argument("--out-dir", "--out", dest="out_dir", required=True, type=Path)
    ap.add_argument("--rgb-ids", required=True, type=Path)
    ap.add_argument("--vehicle-ids", "--ids", dest="vehicle_ids", type=Path)
    ap.add_argument("--topic", default="/cam0/segmentation")
    args = ap.parse_args()

    colors = load_dynamic_colors(args.rgb_ids, args.vehicle_ids)
    args.out_dir.mkdir(parents=True, exist_ok=True)
    store = get_typestore(Stores.ROS1_NOETIC)
    n = n_with_dyn = 0
    with Reader(args.bag) as reader:
        for connection, _timestamp, rawdata in reader.messages():
            if connection.topic != args.topic:
                continue
            msg = store.deserialize_ros1(rawdata, connection.msgtype)
            enc = getattr(msg, "encoding", "rgb8")
            h, w = int(msg.height), int(msg.width)
            data = bytes(msg.data)
            if enc == "rgb8":
                img = np.frombuffer(data, dtype=np.uint8).reshape(h, w, 3)
            elif enc == "bgr8":
                img = cv2.cvtColor(
                    np.frombuffer(data, dtype=np.uint8).reshape(h, w, 3), cv2.COLOR_BGR2RGB
                )
            else:
                raise SystemExit(f"unsupported encoding: {enc}")
            mask = np.zeros(img.shape[:2], dtype=np.uint8)
            for r, g, b in colors:
                hit = (img[:, :, 0] == r) & (img[:, :, 1] == g) & (img[:, :, 2] == b)
                mask[hit] = 255
            ts_ns = int(msg.header.stamp.sec) * 1_000_000_000 + int(msg.header.stamp.nanosec)
            cv2.imwrite(str(args.out_dir / f"{ts_ns}.png"), mask)
            n += 1
            if mask.any():
                n_with_dyn += 1
    print(f"[ok] {n} masks ({n_with_dyn} with dynamic px) -> {args.out_dir}")


if __name__ == "__main__":
    main()
