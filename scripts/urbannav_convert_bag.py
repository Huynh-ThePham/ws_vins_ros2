#!/usr/bin/env python3
"""Convert UrbanNav ROS1 sensors bag → ROS2 bag (cam+imu), optional time trim.

Full bags are tens of GB; prefer --max-sec for smoke tests.

    python3 scripts/urbannav_convert_bag.py \\
      --src .../UrbanNav-HK_TST-20210517_sensors.bag \\
      --dst data/urbannav_ros2/medium/ros2_bag_s60 \\
      --max-sec 60
"""
from __future__ import annotations

import argparse
import shutil
from pathlib import Path

from rosbags.convert.converter import ConverterError, convert, create_connections_converters
from rosbags.highlevel import AnyReader
from rosbags.rosbag2 import Writer as Writer2
from rosbags.rosbag2 import StoragePlugin
from rosbags.typesys import Stores, get_typestore


TOPICS = (
    "/zed2/camera/left/image_raw",
    "/zed2/camera/right/image_raw",
    "/imu/data",
)


def convert_trimmed(src: Path, dst: Path, max_sec: float) -> None:
    src_store = get_typestore(Stores.ROS1_NOETIC)
    dst_store = get_typestore(Stores.ROS2_HUMBLE)
    writer = Writer2(dst, version=9, storage_plugin=StoragePlugin.SQLITE3)
    with AnyReader([src], default_typestore=src_store) as reader, writer:
        t0 = reader.start_time
        t1 = t0 + int(max_sec * 1e9)
        connections = [c for c in reader.connections if c.topic in TOPICS]
        if not connections:
            raise ConverterError(f"none of {TOPICS} found in {src}")
        connmap, convmap = create_connections_converters(
            connections, dst_store, reader, writer)
        n = 0
        for rconn, timestamp, data in reader.messages(connections=connections):
            if timestamp < t0:
                continue
            if timestamp > t1:
                break
            writer.write(
                connmap[(rconn.id, rconn.owner)],
                timestamp,
                convmap[rconn.msgtype](data),
            )
            n += 1
            if n % 5000 == 0:
                print(f"[urbannav] wrote {n} msgs ...", flush=True)
    print(f"[ok] wrote {n} messages (first {max_sec:g}s) -> {dst}")


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--src", type=Path, required=True)
    ap.add_argument("--dst", type=Path, required=True)
    ap.add_argument("--max-sec", type=float, default=None,
                    help="Keep only the first N seconds from bag start")
    args = ap.parse_args()

    if not args.src.is_file():
        raise SystemExit(f"missing source bag: {args.src}")

    if args.dst.exists():
        shutil.rmtree(args.dst)
    args.dst.parent.mkdir(parents=True, exist_ok=True)

    try:
        if args.max_sec is None:
            convert(
                srcs=[args.src],
                dst=args.dst,
                dst_storage="sqlite3",
                dst_version=9,
                compress=None,
                compress_mode="file",
                default_typestore=get_typestore(Stores.ROS1_NOETIC),
                typestore=get_typestore(Stores.ROS2_HUMBLE),
                exclude_topics=[],
                include_topics=list(TOPICS),
                exclude_msgtypes=[],
                include_msgtypes=[],
            )
            print(f"[ok] wrote {args.dst}")
        else:
            convert_trimmed(args.src, args.dst, args.max_sec)
    except ConverterError as exc:
        raise SystemExit(f"convert failed: {exc}") from exc
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
