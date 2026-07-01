#!/usr/bin/env python3
"""Read/write EuRoC N-repeat study manifest and trial progress."""
from __future__ import annotations

import argparse
import json
from datetime import datetime, timezone
from pathlib import Path

SEQS = ["MH_01_easy", "MH_02_easy", "MH_03_medium", "MH_04_difficult", "MH_05_difficult"]
METHODS = ["baseline", "weighted"]


def _trial_counts(root: Path, n: int) -> dict:
    cells: dict[str, dict] = {}
    for seq in SEQS:
        for method in METHODS:
            key = f"{seq}_{method}"
            done = sum(
                1
                for i in range(1, n + 1)
                if (root / key / f"trial_{i}" / "eval" / "metrics.json").is_file()
            )
            cells[key] = {"done": done, "target": n}
    return cells


def cmd_write(args: argparse.Namespace) -> None:
    root = Path(args.root)
    root.mkdir(parents=True, exist_ok=True)
    manifest = {
        "study": "euroc_n3",
        "n_trials_per_cell": args.n,
        "sequences": SEQS,
        "methods": METHODS,
        "total_cells": len(SEQS) * len(METHODS),
        "total_runs": len(SEQS) * len(METHODS) * args.n,
        "euroc_root": args.euroc_root,
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
    manifest = json.loads(path.read_text()) if path.is_file() else {"study": "euroc_n3", "n_trials_per_cell": args.n}
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
        for k in pending:
            c = cells[k]
            print(f"  {k}: {c['done']}/{c['target']}")


def main() -> None:
    ap = argparse.ArgumentParser()
    sub = ap.add_subparsers(dest="cmd", required=True)

    w = sub.add_parser("write")
    w.add_argument("--root", default="results/euroc_repeat")
    w.add_argument("--n", type=int, default=3)
    w.add_argument("--euroc-root", required=True)
    w.add_argument("--log", default="")
    w.set_defaults(func=cmd_write)

    u = sub.add_parser("update")
    u.add_argument("--root", default="results/euroc_repeat")
    u.add_argument("--n", type=int, default=3)
    u.set_defaults(func=cmd_update)

    s = sub.add_parser("status")
    s.add_argument("--root", default="results/euroc_repeat")
    s.add_argument("--n", type=int, default=3)
    s.set_defaults(func=cmd_status)

    args = ap.parse_args()
    args.func(args)


if __name__ == "__main__":
    main()
