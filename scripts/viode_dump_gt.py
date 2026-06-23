#!/usr/bin/env python3
"""Dump VIODE ground-truth /odometry from a ROS1 bag to vio.csv-compatible CSV.

Uses rosbags (no ROS1 install required). Output works with evaluate_trajectory.py.
"""
from __future__ import annotations

import argparse
from pathlib import Path

from rosbags.rosbag1 import Reader
from rosbags.typesys import Stores, get_typestore


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("--bag", required=True, type=Path, help="ROS1 .bag path")
    ap.add_argument("--out", required=True, type=Path)
    ap.add_argument("--topic", default="/odometry")
    args = ap.parse_args()

    if not args.bag.is_file():
        raise SystemExit(f"bag not found: {args.bag}")

    store = get_typestore(Stores.ROS1_NOETIC)
    args.out.parent.mkdir(parents=True, exist_ok=True)
    n = 0
    with Reader(args.bag) as reader, args.out.open("w") as f:
        for connection, _timestamp, rawdata in reader.messages():
            if connection.topic != args.topic:
                continue
            msg = store.deserialize_ros1(rawdata, connection.msgtype)
            p = msg.pose.pose.position
            q = msg.pose.pose.orientation
            ts_ns = int(msg.header.stamp.sec) * 1_000_000_000 + int(msg.header.stamp.nanosec)
            f.write(
                f"{ts_ns},{p.x:.9f},{p.y:.9f},{p.z:.9f},"
                f"{q.w:.9f},{q.x:.9f},{q.y:.9f},{q.z:.9f}\n"
            )
            n += 1
    print(f"[ok] wrote {n} GT poses -> {args.out}")


if __name__ == "__main__":
    main()
