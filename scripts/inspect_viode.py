#!/usr/bin/env python3
"""Inspect a VIODE bag: image dims/encoding + segmentation color distribution.

Run inside the vins-fusion docker (has rosbag + cv_bridge + numpy).
"""
from __future__ import annotations

import argparse
from collections import Counter

import numpy as np
import rosbag
from cv_bridge import CvBridge


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("--bag", required=True)
    ap.add_argument("--frames", type=int, default=3, help="how many cam0 frames to sample")
    args = ap.parse_args()

    bridge = CvBridge()
    seen = {"/cam0/image_raw": 0, "/cam0/segmentation": 0}
    printed_meta = set()

    with rosbag.Bag(args.bag, "r") as b:
        for topic, msg, _t in b.read_messages(topics=list(seen.keys())):
            if topic not in seen or seen[topic] >= args.frames:
                if all(seen[k] >= args.frames for k in seen):
                    break
                continue
            seen[topic] += 1
            if topic not in printed_meta:
                printed_meta.add(topic)
                print(f"\n### {topic}: encoding={msg.encoding} {msg.width}x{msg.height} "
                      f"step={msg.step} is_bigendian={msg.is_bigendian}")

            if topic == "/cam0/segmentation":
                img = bridge.imgmsg_to_cv2(msg)
                arr = img.reshape(-1, img.shape[2]) if img.ndim == 3 else img.reshape(-1, 1)
                colors = Counter(map(tuple, arr.tolist()))
                total = arr.shape[0]
                print(f"  frame {seen[topic]} t={msg.header.stamp.to_sec():.3f} "
                      f"shape={img.shape} dtype={img.dtype} unique_colors={len(colors)}")
                for color, cnt in colors.most_common(12):
                    print(f"    color={color} px={cnt} ({100.0*cnt/total:.2f}%)")
            else:
                img = bridge.imgmsg_to_cv2(msg)
                print(f"  frame {seen[topic]} shape={img.shape} dtype={img.dtype} "
                      f"min={img.min()} max={img.max()}")


if __name__ == "__main__":
    main()
