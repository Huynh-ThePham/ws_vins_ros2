#!/usr/bin/env python3
"""Write run_manifest.json with full provenance for one run (plan P0.4 / section 12).

Changes from the previous version, all required for a run to be traceable:

  * full 40-character git SHA, not a short one, plus git_dirty. A run from a dirty
    worktree may not enter a main paper table, and the validator enforces that.
  * full sha256 for the resolved config, the bag and the segmentation model, not a
    16-character prefix.
  * structured status: ok / diverged / failed with a failure_reason, so a diverged
    run is recorded rather than silently dropped by the asset script.
  * <output>/failure_status.json written by the estimator is folded in, so the
    estimator's own FailureReason reaches the manifest.
  * environment provenance (ROS distro, compiler, OpenCV, Ceres) and the
    same-input-support counters the fairness comparison needs.

Exits non-zero on a problem: this runs on the publication path.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import os
import platform
import re
import subprocess
import sys
from datetime import datetime, timezone
from pathlib import Path

DIVERGED_ATE_DEFAULT_M = 50.0


def run_text(cmd: list[str]) -> str | None:
    try:
        return subprocess.check_output(cmd, stderr=subprocess.DEVNULL, text=True).strip()
    except (subprocess.CalledProcessError, FileNotFoundError, OSError):
        return None


def git_provenance(ws: Path) -> dict:
    sha = run_text(["git", "-C", str(ws), "rev-parse", "HEAD"])
    status = run_text(["git", "-C", str(ws), "status", "--porcelain"])
    branch = run_text(["git", "-C", str(ws), "rev-parse", "--abbrev-ref", "HEAD"])
    return {
        # Full SHA: a short SHA cannot identify a commit unambiguously.
        "git_sha": sha,
        "git_branch": branch,
        # None (unknown) is distinct from False (verified clean); the validator
        # rejects both for a paper table, but only the latter silently.
        "git_dirty": None if status is None else bool(status),
        "git_dirty_files": None if not status else sorted(
            line[3:] for line in status.splitlines() if len(line) > 3),
    }


def sha256_file(path: str | None) -> str | None:
    """Hash a regular file, or a ROS 2 bag directory (metadata + payloads)."""
    if not path:
        return None
    p = Path(path)
    if p.is_file():
        digest = hashlib.sha256()
        with p.open("rb") as handle:
            for chunk in iter(lambda: handle.read(1024 * 1024), b""):
                digest.update(chunk)
        return digest.hexdigest()
    if p.is_dir():
        files = sorted(f for f in p.rglob("*") if f.is_file())
        if not files:
            return None
        digest = hashlib.sha256()
        for file_path in files:
            rel = file_path.relative_to(p).as_posix()
            digest.update(rel.encode("utf-8"))
            digest.update(b"\0")
            with file_path.open("rb") as handle:
                for chunk in iter(lambda: handle.read(1024 * 1024), b""):
                    digest.update(chunk)
            digest.update(b"\0")
        return digest.hexdigest()
    return None


def environment() -> dict:
    opencv_version = None
    try:
        import cv2  # noqa: F401  (optional; only for recording the version)
        opencv_version = cv2.__version__
    except Exception:
        pass

    ceres_version = None
    for candidate in ("/usr/include/ceres/version.h",
                      "/usr/local/include/ceres/version.h"):
        try:
            text = Path(candidate).read_text()
        except OSError:
            continue
        parts = {}
        for key in ("MAJOR", "MINOR", "REVISION"):
            match = re.search(rf"CERES_VERSION_{key}\s+(\d+)", text)
            if match:
                parts[key] = match.group(1)
        if len(parts) == 3:
            ceres_version = f"{parts['MAJOR']}.{parts['MINOR']}.{parts['REVISION']}"
            break

    return {
        "ros_distro": os.environ.get("ROS_DISTRO"),
        "platform": platform.platform(),
        "python_version": platform.python_version(),
        "compiler_version": run_text(["cc", "--version"]),
        "opencv_version": opencv_version,
        "ceres_version": ceres_version,
        "container_digest": os.environ.get("PAPER_CONTAINER_DIGEST"),
    }


def read_json(path: Path):
    try:
        return json.loads(path.read_text())
    except (OSError, json.JSONDecodeError):
        return None


def read_metrics(out_dir: Path) -> dict:
    for candidate in (out_dir / "eval" / "metrics.json", out_dir / "metrics.json"):
        data = read_json(candidate)
        if isinstance(data, dict):
            return data
    return {}


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--out-dir", type=Path, required=True)
    ap.add_argument("--dataset", required=True)
    ap.add_argument("--scene", required=True)
    ap.add_argument("--method", required=True)
    ap.add_argument("--trial", type=int, default=1)
    ap.add_argument("--bag-rate", type=float, default=1.0)
    ap.add_argument("--seed", type=int, default=None,
                    help="Paired seed for this trial; identical across methods.")
    ap.add_argument("--yolo", type=int, default=0)
    ap.add_argument("--status", default="ok",
                    help="ok / diverged / failed. Recomputed from the evidence below; a "
                         "caller claiming 'ok' for a diverged run is overridden.")
    ap.add_argument("--failure-reason", default="")
    ap.add_argument("--config", default="",
                    help="Resolved config the estimator was given.")
    ap.add_argument("--bag", default="")
    ap.add_argument("--model", default="",
                    help="Segmentation model file, for semantic runs.")
    ap.add_argument("--model-manifest", default="",
                    help="models/model_manifest.json, if present.")
    ap.add_argument("--protocol-fair", type=int, default=0)
    ap.add_argument("--oracle-ablation", type=int, default=0)
    ap.add_argument("--sem-policy-dynamic-level", type=int, default=-1)
    ap.add_argument("--sem-policy-params-file", default="")
    ap.add_argument("--gt", default="",
                    help="Ground-truth trajectory file; hashed into the manifest.")
    ap.add_argument("--protocol-tag", default="")
    ap.add_argument("--protocol-version", default="")
    ap.add_argument("--diverged-ate-m", type=float, default=DIVERGED_ATE_DEFAULT_M)
    ap.add_argument("--ws", type=Path, default=None)
    args = ap.parse_args()

    out_dir = args.out_dir
    if not out_dir.is_dir():
        print(f"[fatal] output directory does not exist: {out_dir}", file=sys.stderr)
        return 2

    ws = args.ws or out_dir
    while ws != ws.parent and not (ws / ".git").is_dir():
        ws = ws.parent
    if not (ws / ".git").is_dir():
        ws = Path(__file__).resolve().parents[1]

    metrics = read_metrics(out_dir)
    ate = metrics.get("ate_rmse_m")

    # The estimator writes this on its first detected failure.
    failure_status = read_json(out_dir / "failure_status.json") or {}

    status = str(args.status).lower()
    failure_reason = args.failure_reason or failure_status.get("failure_reason") or ""

    # Status is derived, not trusted. The whole point of P0.3 is that a caller
    # cannot report success for a run that diverged or never produced a trajectory.
    if failure_status.get("status") == "failed":
        status = "failed"
        failure_reason = failure_reason or failure_status.get("failure_reason") or "estimator_failure"
    elif ate is None:
        if status == "ok":
            status = "failed"
            failure_reason = failure_reason or "no_ate_metric"
    elif float(ate) > float(args.diverged_ate_m):
        # Recorded, never dropped: the success rate and the failure-penalized score
        # both depend on knowing this run happened.
        status = "diverged"
        failure_reason = failure_reason or "ate_threshold_exceeded"

    if status not in ("ok", "diverged", "failed"):
        print(f"[fatal] invalid status '{status}'", file=sys.stderr)
        return 2
    if status != "ok" and not failure_reason:
        failure_reason = "unspecified"

    resolved_config = args.config or str(out_dir / "resolved_config.yaml")
    model_manifest_path = Path(args.model_manifest) if args.model_manifest else None
    model_manifest = read_json(model_manifest_path) if model_manifest_path else None
    model_path = args.model or (model_manifest or {}).get("file", "")
    if model_path and not Path(model_path).is_file() and model_manifest_path is not None:
        candidate = model_manifest_path.parent / Path(model_path).name
        if candidate.is_file():
            model_path = str(candidate)

    manifest = {
        "timestamp_utc": datetime.now(timezone.utc).isoformat(),
        "dataset": args.dataset,
        "scene": args.scene,
        "method": args.method,
        "trial": args.trial,
        "seed": args.seed,
        "bag_rate": args.bag_rate,
        "yolo": bool(args.yolo),
        "status": status,
        "failure_reason": failure_reason or None,
        "failure_timestamp_ns": failure_status.get("failure_timestamp_ns"),
        "failure_count": failure_status.get("failure_count"),

        "config": resolved_config,
        "resolved_config_sha256": sha256_file(resolved_config),
        "bag": args.bag,
        "bag_sha256": sha256_file(args.bag),
        "gt": args.gt or None,
        "gt_sha256": sha256_file(args.gt),
        "model": model_path or None,
        "model_sha256": sha256_file(model_path),
        "model_manifest": model_manifest,
        "model_manifest_sha256": sha256_file(str(model_manifest_path) if model_manifest_path else None),

        "protocol_fair": bool(args.protocol_fair),
        "protocol_tag": args.protocol_tag or None,
        "protocol_version": args.protocol_version or args.protocol_tag or None,
        "oracle_ablation": bool(args.oracle_ablation),
        "sem_policy_dynamic_level": args.sem_policy_dynamic_level,
        "sem_policy_params_file": args.sem_policy_params_file or None,
        "sem_policy_params_sha256": sha256_file(args.sem_policy_params_file),

        **git_provenance(ws),
        "environment": environment(),

        # Metrics are copied in so the validator does not depend on the layout of
        # the evaluation output.
        "ate_rmse_m": ate,
        "metrics": metrics or None,
        "trajectory_coverage": metrics.get("trajectory_coverage"),

        "artifacts": {
            "vio_csv": str(out_dir / "vio.csv"),
            "resolved_config": str(out_dir / "resolved_config.yaml"),
            "pht_vio_log": str(out_dir / "pht_vio_node.log"),
            "yolo_log": str(out_dir / "yolo_mask_node.log"),
            "metrics": str(out_dir / "eval" / "metrics.json"),
            "geo_df_stats": str(out_dir / "geo_df_stats.csv"),
            "sem_stats": str(out_dir / "sem_stats.csv"),
            "sem_geodf_stats": str(out_dir / "sem_geodf_stats.csv"),
            "stereo_stats": str(out_dir / "stereo_stats.csv"),
            "adaptive_factor_stats": str(out_dir / "adaptive_factor_stats.csv"),
            "failure_status": str(out_dir / "failure_status.json"),
        },
    }
    for key, p in list(manifest["artifacts"].items()):
        if not Path(p).is_file():
            manifest["artifacts"][key] = None

    (out_dir / "run_manifest.json").write_text(json.dumps(manifest, indent=2) + "\n")

    if status != "ok":
        print(f"[manifest] {args.dataset}/{args.scene}/{args.method} trial {args.trial}: "
              f"{status} ({failure_reason})")
    return 0


if __name__ == "__main__":
    sys.exit(main())
