#!/usr/bin/env python3
"""Decide whether an existing run directory may be reused (plan P0.4).

A run is reusable only when every provenance field matches the current protocol
identity. Presence of eval/metrics.json alone is never enough.

    python3 scripts/validate_reusable_run.py \
      --run-dir <out> \
      --expected-method union_weight \
      --expected-dataset euroc \
      --expected-scene MH_03_medium \
      --expected-trial 1 \
      --expected-seed 1001 \
      --expected-config-sha256 <sha> \
      --expected-protocol-tag sem-geodf-fair-v2 \
      --ws .

Exit 0 = reusable; exit 1 = must rerun; exit 2 = usage error.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import subprocess
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
DEFAULT_PROTOCOL = "sem-geodf-fair-v2"


def load_json(path: Path):
    try:
        return json.loads(path.read_text())
    except (OSError, json.JSONDecodeError) as exc:
        return exc


def sha256_path(path: Path | None) -> str | None:
    """Hash a file, or a ROS2 bag directory via metadata + payload digests."""
    if path is None:
        return None
    if path.is_file():
        digest = hashlib.sha256()
        with path.open("rb") as handle:
            for chunk in iter(lambda: handle.read(1024 * 1024), b""):
                digest.update(chunk)
        return digest.hexdigest()
    if path.is_dir():
        digest = hashlib.sha256()
        files = sorted(p for p in path.rglob("*") if p.is_file())
        if not files:
            return None
        for file_path in files:
            rel = file_path.relative_to(path).as_posix()
            digest.update(rel.encode("utf-8"))
            digest.update(b"\0")
            with file_path.open("rb") as handle:
                for chunk in iter(lambda: handle.read(1024 * 1024), b""):
                    digest.update(chunk)
            digest.update(b"\0")
        return digest.hexdigest()
    return None


def current_git(ws: Path) -> tuple[str | None, bool | None]:
    try:
        sha = subprocess.check_output(
            ["git", "-C", str(ws), "rev-parse", "HEAD"],
            stderr=subprocess.DEVNULL, text=True).strip()
        status = subprocess.check_output(
            ["git", "-C", str(ws), "status", "--porcelain"],
            stderr=subprocess.DEVNULL, text=True)
        return sha, bool(status.strip())
    except (subprocess.CalledProcessError, FileNotFoundError, OSError):
        return None, None


def fail(msg: str) -> int:
    print(f"[reuse] RERUN: {msg}", file=sys.stderr)
    return 1


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--run-dir", type=Path, required=True)
    ap.add_argument("--ws", type=Path, default=REPO)
    ap.add_argument("--expected-method", required=True)
    ap.add_argument("--expected-dataset", required=True)
    ap.add_argument("--expected-scene", required=True)
    ap.add_argument("--expected-trial", type=int, required=True)
    ap.add_argument("--expected-seed", type=int, required=True)
    ap.add_argument("--expected-config-sha256", default="")
    ap.add_argument("--expected-bag", type=Path, default=None)
    ap.add_argument("--expected-gt", type=Path, default=None)
    ap.add_argument("--expected-protocol-tag", default=DEFAULT_PROTOCOL)
    ap.add_argument("--expected-protocol-version", default=DEFAULT_PROTOCOL)
    ap.add_argument("--expected-model-sha256", default="")
    ap.add_argument("--expected-model-manifest-sha256", default="")
    ap.add_argument("--expected-policy-params-sha256", default="")
    ap.add_argument("--min-commit", default="",
                    help="If set, refuse reuse of runs whose git_sha is an ancestor "
                         "of this correctness commit (or missing).")
    ap.add_argument("--require-semantic-model", action="store_true")
    args = ap.parse_args()

    run_dir = args.run_dir
    if not run_dir.is_dir():
        return fail(f"missing run dir {run_dir}")

    if (run_dir / "failure_status.json").is_file():
        return fail("failure_status.json present")

    metrics = run_dir / "eval" / "metrics.json"
    if not metrics.is_file():
        return fail("eval/metrics.json missing")
    metrics_data = load_json(metrics)
    if isinstance(metrics_data, Exception) or not isinstance(metrics_data, dict):
        return fail(f"metrics schema invalid: {metrics_data}")
    if metrics_data.get("ate_rmse_m") is None:
        return fail("metrics missing ate_rmse_m")

    manifest_path = run_dir / "run_manifest.json"
    if not manifest_path.is_file():
        return fail("run_manifest.json missing")
    manifest = load_json(manifest_path)
    if isinstance(manifest, Exception) or not isinstance(manifest, dict):
        return fail(f"manifest unreadable: {manifest}")

    if str(manifest.get("status", "")).lower() != "ok":
        return fail(f"status={manifest.get('status')!r} (need ok)")

    checks = [
        ("method", args.expected_method, manifest.get("method")),
        ("dataset", args.expected_dataset, manifest.get("dataset")),
        ("scene", args.expected_scene, manifest.get("scene")),
        ("trial", args.expected_trial, manifest.get("trial")),
        ("seed", args.expected_seed, manifest.get("seed")),
    ]
    for name, want, got in checks:
        if got != want and str(got) != str(want):
            return fail(f"{name} mismatch: have {got!r}, want {want!r}")

    protocol = manifest.get("protocol_tag") or manifest.get("protocol_version")
    if protocol != args.expected_protocol_tag and protocol != args.expected_protocol_version:
        return fail(f"protocol mismatch: have {protocol!r}, want "
                    f"{args.expected_protocol_tag!r}")

    sha, dirty = current_git(args.ws)
    if not manifest.get("git_sha"):
        return fail("manifest missing git_sha")
    if len(str(manifest["git_sha"])) != 40:
        return fail("git_sha is not a full 40-char SHA")
    if sha and manifest["git_sha"] != sha:
        return fail(f"git_sha mismatch: have {manifest['git_sha'][:12]}, "
                    f"current {sha[:12]}")
    if manifest.get("git_dirty") is not False:
        return fail(f"git_dirty={manifest.get('git_dirty')!r} (need false)")
    if dirty:
        return fail("current worktree is dirty; refuse reuse")

    if args.min_commit:
        # Refuse reuse unless the manifest commit is a descendant of the
        # correctness baseline (or is the baseline itself).
        try:
            rc = subprocess.run(
                ["git", "-C", str(args.ws), "merge-base", "--is-ancestor",
                 args.min_commit, str(manifest["git_sha"])],
                capture_output=True, text=True)
        except OSError as exc:
            return fail(f"cannot check --min-commit ancestry: {exc}")
        if rc.returncode != 0:
            return fail(f"manifest git_sha is not a descendant of --min-commit "
                        f"{args.min_commit[:12]}")

    resolved = run_dir / "resolved_config.yaml"
    if not resolved.is_file():
        return fail("resolved_config.yaml missing")
    actual_cfg = sha256_path(resolved)
    recorded = manifest.get("resolved_config_sha256")
    if not recorded:
        return fail("manifest missing resolved_config_sha256")
    if actual_cfg != recorded:
        return fail("resolved_config.yaml hash does not match manifest")
    if args.expected_config_sha256 and args.expected_config_sha256 != recorded:
        return fail("resolved config does not match expected overlay resolution")

    if not manifest.get("bag_sha256"):
        return fail("manifest missing bag_sha256")
    if args.expected_bag is not None:
        bag_hash = sha256_path(args.expected_bag)
        if bag_hash != manifest["bag_sha256"]:
            return fail("bag identity mismatch")

    if args.expected_gt is not None:
        gt_hash = sha256_path(args.expected_gt)
        recorded_gt = manifest.get("gt_sha256")
        if not recorded_gt:
            return fail("manifest missing gt_sha256")
        if gt_hash != recorded_gt:
            return fail("GT identity mismatch")

    if args.require_semantic_model or args.expected_model_sha256:
        if not manifest.get("model_sha256"):
            return fail("semantic run missing model_sha256")
        if args.expected_model_sha256 and \
                manifest["model_sha256"] != args.expected_model_sha256:
            return fail("model_sha256 mismatch")

    if args.expected_model_manifest_sha256:
        got = manifest.get("model_manifest_sha256")
        if got != args.expected_model_manifest_sha256:
            return fail("model_manifest_sha256 mismatch")

    if args.expected_policy_params_sha256:
        got = manifest.get("sem_policy_params_sha256")
        if got != args.expected_policy_params_sha256:
            return fail("policy params sha mismatch")

    coverage = manifest.get("trajectory_coverage")
    if coverage is None and isinstance(manifest.get("metrics"), dict):
        coverage = manifest["metrics"].get("trajectory_coverage")
    if coverage is None:
        coverage = metrics_data.get("trajectory_coverage")
    if coverage is None:
        return fail("trajectory_coverage missing")
    if float(coverage) < 0.90:
        return fail(f"trajectory_coverage {float(coverage):.3f} below 0.90")

    print(f"[reuse] OK: {run_dir}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
