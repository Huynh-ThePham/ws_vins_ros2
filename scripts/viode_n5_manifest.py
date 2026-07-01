#!/usr/bin/env python3
"""Read/write VIODE N-repeat study manifest and trial progress."""
from __future__ import annotations

import argparse
import glob
import json
import os
from datetime import datetime, timezone
from pathlib import Path

ENVS = ["city_day", "city_night", "parking_lot"]
LEVELS = ["0_none", "1_low", "2_mid", "3_high"]
METHODS = ["baseline", "adaptive"]


def _trial_counts(root: Path, n: int) -> dict:
    cells: dict[str, dict] = {}
    for env in ENVS:
        for level in LEVELS:
            for method in METHODS:
                key = f"{env}_{level}_{method}"
                done = 0
                for i in range(1, n + 1):
                    if (root / key / f"trial_{i}" / "eval" / "metrics.json").is_file():
                        done += 1
                cells[key] = {"done": done, "target": n}
    return cells


def cmd_write(args: argparse.Namespace) -> None:
    root = Path(args.root)
    root.mkdir(parents=True, exist_ok=True)
    manifest = {
        "study": "viode_n5",
        "n_trials_per_cell": args.n,
        "envs": ENVS,
        "levels": LEVELS,
        "methods": METHODS,
        "total_cells": len(ENVS) * len(LEVELS) * len(METHODS),
        "total_runs": len(ENVS) * len(LEVELS) * len(METHODS) * args.n,
        "viode_root": args.viode_root,
        "prepared_at": datetime.now(timezone.utc).isoformat(),
        "prepare_log": args.log,
        "cells": _trial_counts(root, args.n),
    }
    out = root / "manifest.json"
    out.write_text(json.dumps(manifest, indent=2) + "\n")
    print(f"[manifest] wrote {out}")


def cmd_update(args: argparse.Namespace) -> None:
    root = Path(args.root)
    path = root / "manifest.json"
    if path.is_file():
        manifest = json.loads(path.read_text())
    else:
        manifest = {"study": "viode_n5", "n_trials_per_cell": args.n}
    manifest["updated_at"] = datetime.now(timezone.utc).isoformat()
    manifest["cells"] = _trial_counts(root, args.n)
    done = sum(c["done"] for c in manifest["cells"].values())
    target = sum(c["target"] for c in manifest["cells"].values())
    manifest["progress"] = {"done": done, "target": target, "pct": round(100 * done / target, 1) if target else 0}
    path.write_text(json.dumps(manifest, indent=2) + "\n")
    print(f"[manifest] progress {done}/{target} ({manifest['progress']['pct']}%) -> {path}")


def cmd_status(args: argparse.Namespace) -> None:
    root = Path(args.root)
    n = args.n
    path = root / "manifest.json"
    if path.is_file():
        manifest = json.loads(path.read_text())
        n = manifest.get("n_trials_per_cell", n)
        print(json.dumps(manifest, indent=2))
    cells = _trial_counts(root, n)
    done = sum(c["done"] for c in cells.values())
    target = sum(c["target"] for c in cells.values())
    pending = [k for k, v in cells.items() if v["done"] < v["target"]]
    print(f"\nprogress: {done}/{target}")
    if pending:
        print(f"pending cells ({len(pending)}):")
        for k in pending[:20]:
            c = cells[k]
            print(f"  {k}: {c['done']}/{c['target']}")
        if len(pending) > 20:
            print(f"  ... and {len(pending) - 20} more")


def main() -> None:
    ap = argparse.ArgumentParser()
    sub = ap.add_subparsers(dest="cmd", required=True)

    w = sub.add_parser("write", help="write fresh manifest after prepare")
    w.add_argument("--root", default="results/viode_repeat")
    w.add_argument("--n", type=int, default=5)
    w.add_argument("--viode-root", required=True)
    w.add_argument("--log", default="")
    w.set_defaults(func=cmd_write)

    u = sub.add_parser("update", help="refresh trial progress counts")
    u.add_argument("--root", default="results/viode_repeat")
    u.add_argument("--n", type=int, default=5)
    u.set_defaults(func=cmd_update)

    s = sub.add_parser("status", help="print manifest and progress")
    s.add_argument("--root", default="results/viode_repeat")
    s.add_argument("--n", type=int, default=5)
    s.set_defaults(func=cmd_status)

    args = ap.parse_args()
    args.func(args)


if __name__ == "__main__":
    main()
