#!/usr/bin/env python3
"""Write run_manifest.json for one ablation run."""
from __future__ import annotations

import argparse
import hashlib
import json
import subprocess
from datetime import datetime, timezone
from pathlib import Path


def git_sha(ws: Path) -> str:
    try:
        out = subprocess.check_output(
            ["git", "-C", str(ws), "rev-parse", "--short", "HEAD"],
            stderr=subprocess.DEVNULL,
            text=True,
        )
        return out.strip()
    except (subprocess.CalledProcessError, FileNotFoundError):
        return "unknown"


def config_hash(path: str) -> str | None:
    if not path:
        return None
    p = Path(path)
    if not p.is_file():
        return None
    digest = hashlib.sha256(p.read_bytes()).hexdigest()
    return digest[:16]


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("--out-dir", type=Path, required=True)
    ap.add_argument("--dataset", required=True)
    ap.add_argument("--scene", required=True)
    ap.add_argument("--method", required=True)
    ap.add_argument("--trial", type=int, default=1)
    ap.add_argument("--bag-rate", type=float, default=1.0)
    ap.add_argument("--yolo", type=int, default=0)
    ap.add_argument("--status", default="ok")
    ap.add_argument("--config", default="")
    ap.add_argument("--bag", default="")
    ap.add_argument("--protocol-fair", type=int, default=0)
    ap.add_argument("--oracle-ablation", type=int, default=0)
    ap.add_argument("--sem-policy-dynamic-level", type=int, default=-1)
    ap.add_argument("--sem-policy-params-file", default="")
    ap.add_argument("--ws", type=Path, default=None)
    args = ap.parse_args()

    out_dir = args.out_dir
    ws = args.ws or out_dir
    while ws != ws.parent and not (ws / ".git").is_dir():
        ws = ws.parent
    if not (ws / ".git").is_dir():
        ws = Path(__file__).resolve().parents[1]

    manifest = {
        "timestamp_utc": datetime.now(timezone.utc).isoformat(),
        "dataset": args.dataset,
        "scene": args.scene,
        "method": args.method,
        "trial": args.trial,
        "bag_rate": args.bag_rate,
        "yolo": bool(args.yolo),
        "status": args.status,
        "config": args.config,
        "config_hash": config_hash(args.config),
        "bag": args.bag,
        "git_sha": git_sha(ws),
        "protocol_fair": bool(args.protocol_fair),
        "oracle_ablation": bool(args.oracle_ablation),
        "sem_policy_dynamic_level": args.sem_policy_dynamic_level,
        "sem_policy_params_file": args.sem_policy_params_file or None,
        "sem_policy_params_hash": config_hash(args.sem_policy_params_file),
        "artifacts": {
            "vio_csv": str(out_dir / "vio.csv"),
            "pht_vio_log": str(out_dir / "pht_vio_node.log"),
            "yolo_log": str(out_dir / "yolo_mask_node.log"),
            "metrics": str(out_dir / "eval" / "metrics.json"),
            "geo_df_stats": str(out_dir / "geo_df_stats.csv"),
            "sem_stats": str(out_dir / "sem_stats.csv"),
            "sem_geodf_stats": str(out_dir / "sem_geodf_stats.csv"),
        },
    }
    for key, p in list(manifest["artifacts"].items()):
        if not Path(p).is_file():
            manifest["artifacts"][key] = None

    path = out_dir / "run_manifest.json"
    path.write_text(json.dumps(manifest, indent=2) + "\n")


if __name__ == "__main__":
    main()
