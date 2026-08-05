#!/usr/bin/env python3
"""Fail-closed gate for the publication pipeline (plan P0.3).

No table, figure or LaTeX macro may be produced from a run tree until this returns
PASS. The failure modes it exists to stop, all of which were possible before:

  * a runner that used `|| true` and reported success after an environment failed;
  * a missing bag that was skipped, with the summary still generated;
  * a diverged run silently dropped because its ATE exceeded 50 m;
  * cells averaged over whatever number of trials happened to finish;
  * runs from different commits, or from a dirty worktree, mixed in one table;
  * an oracle run (sem_policy_dynamic_level >= 0) reaching a main result.

    python3 scripts/validate_experiment_matrix.py --root results/<tag>
    python3 scripts/validate_experiment_matrix.py --root results/<tag> \
        --expected src/config/paper/expected_matrix.json --report <tag>/validation.json

Exit codes: 0 = PASS, 1 = FAIL, 2 = usage/IO error.
"""
from __future__ import annotations

import argparse
import json
import sys
from collections import defaultdict
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent / "lib"))

from paper_config import parse_scalars, sha256_file, values_equal  # noqa: E402

REPO = Path(__file__).resolve().parent.parent
DEFAULT_EXPECTED = REPO / "src/config/paper/expected_matrix.json"


class Report:
    def __init__(self) -> None:
        self.errors: list[str] = []
        self.notes: list[str] = []

    def error(self, message: str) -> None:
        self.errors.append(message)

    def note(self, message: str) -> None:
        self.notes.append(message)

    @property
    def ok(self) -> bool:
        return not self.errors


def load_json(path: Path):
    try:
        return json.loads(path.read_text())
    except (OSError, json.JSONDecodeError) as exc:
        return exc


def find_runs(root: Path) -> list[Path]:
    """Every directory holding a run_manifest.json."""
    return sorted(p.parent for p in root.rglob("run_manifest.json"))


def metric_value(run_dir: Path, manifest: dict, key: str):
    """Read a metric from the manifest, or from the metrics.json it points at."""
    if key in manifest:
        return manifest[key]
    metrics_path = manifest.get("artifacts", {}).get("metrics")
    candidates = []
    if metrics_path:
        candidates.append(Path(metrics_path))
    candidates.append(run_dir / "eval" / "metrics.json")
    candidates.append(run_dir / "metrics.json")
    for candidate in candidates:
        if not candidate.is_file():
            continue
        data = load_json(candidate)
        if isinstance(data, dict) and key in data:
            return data[key]
    return None


def validate(root: Path, expected: dict, report: Report,
             min_trials: int | None, allow_dirty: bool) -> dict:
    requirements = expected.get("requirements", {})
    diverged_threshold = float(requirements.get("diverged_ate_threshold_m", 50.0))

    run_dirs = find_runs(root)
    if not run_dirs:
        report.error(f"no run_manifest.json found under {root}; nothing to validate")
        return {}

    # (dataset, scene, method) -> {trial: record}
    cells: dict[tuple[str, str, str], dict[int, dict]] = defaultdict(dict)
    git_shas: set[str] = set()
    summary = {
        "runs_total": 0,
        "runs_ok": 0,
        "runs_diverged": 0,
        "runs_failed": 0,
        "runs_oracle": 0,
    }

    for run_dir in run_dirs:
        manifest_path = run_dir / "run_manifest.json"
        manifest = load_json(manifest_path)
        if isinstance(manifest, Exception):
            report.error(f"{manifest_path}: unreadable manifest ({manifest})")
            continue
        summary["runs_total"] += 1

        rel = run_dir.relative_to(root)
        dataset = manifest.get("dataset")
        scene = manifest.get("scene")
        method = manifest.get("method")
        trial = manifest.get("trial")
        if not all([dataset, scene, method]) or trial is None:
            report.error(f"{rel}: manifest missing dataset/scene/method/trial")
            continue

        key = (str(dataset), str(scene), str(method))
        if trial in cells[key]:
            report.error(
                f"{dataset}/{scene}/{method}: trial {trial} appears twice "
                f"({cells[key][trial]['dir']} and {rel})")
            continue
        record = {"dir": str(rel), "manifest": manifest}
        cells[key][int(trial)] = record

        status = str(manifest.get("status", "")).lower()

        # --- provenance -----------------------------------------------------
        sha = manifest.get("git_sha")
        if not sha:
            report.error(f"{rel}: no git_sha")
        else:
            git_shas.add(sha)
            if requirements.get("require_full_git_sha") and len(str(sha)) != 40:
                report.error(f"{rel}: git_sha '{sha}' is not a full 40-character SHA; a "
                             f"short SHA cannot identify a commit unambiguously")

        if requirements.get("require_clean_worktree") and not allow_dirty:
            dirty = manifest.get("git_dirty")
            if dirty is None:
                report.error(f"{rel}: manifest does not record git_dirty")
            elif dirty:
                report.error(f"{rel}: produced from a dirty worktree; it may not enter a "
                             f"main paper table")

        if requirements.get("require_resolved_config_hash"):
            if not manifest.get("resolved_config_sha256"):
                report.error(f"{rel}: no resolved_config_sha256")
            elif not (run_dir / "resolved_config.yaml").is_file():
                report.error(f"{rel}: resolved_config.yaml is missing, so the recorded "
                             f"hash cannot be verified")
            else:
                actual = sha256_file(run_dir / "resolved_config.yaml")
                if actual != manifest["resolved_config_sha256"]:
                    report.error(f"{rel}: resolved_config.yaml hash {actual[:12]} does not "
                                 f"match the manifest's "
                                 f"{str(manifest['resolved_config_sha256'])[:12]}")

        if requirements.get("require_bag_hash") and not manifest.get("bag_sha256"):
            report.error(f"{rel}: no bag_sha256; the input data is unidentified")

        # --- oracle / protocol leakage --------------------------------------
        level = manifest.get("sem_policy_dynamic_level")
        if manifest.get("oracle_ablation") or (level is not None and int(level) >= 0):
            summary["runs_oracle"] += 1
            if requirements.get("forbid_oracle_runs"):
                report.error(f"{rel}: oracle run (sem_policy_dynamic_level={level}) present "
                             f"in a main result tree")

        resolved = run_dir / "resolved_config.yaml"
        if resolved.is_file():
            scalars = parse_scalars(resolved.read_text())
            for cfg_key, want in requirements.get("required_config_values", {}).items():
                got = scalars.get(cfg_key)
                if got is None:
                    report.error(f"{rel}: resolved config does not set {cfg_key}")
                elif not values_equal(got, str(want)):
                    report.error(f"{rel}: resolved config has {cfg_key}={got}, protocol "
                                 f"requires {want}")

        if requirements.get("require_model_hash_when_semantic"):
            uses_semantic = bool(manifest.get("yolo"))
            if resolved.is_file():
                uses_semantic = uses_semantic or \
                    parse_scalars(resolved.read_text()).get("sem_enable") == "1"
            if uses_semantic and not manifest.get("model_sha256"):
                report.error(f"{rel}: semantic run without model_sha256; the segmentation "
                             f"model is unidentified")

        # --- coverage and status --------------------------------------------
        coverage = metric_value(run_dir, manifest, "trajectory_coverage")
        min_coverage = requirements.get("min_trajectory_coverage")
        if min_coverage is not None:
            if coverage is None:
                report.error(f"{rel}: no trajectory_coverage recorded; ATE cannot be "
                             f"compared across methods without it")
            elif float(coverage) < float(min_coverage):
                report.error(f"{rel}: trajectory_coverage {float(coverage):.3f} below the "
                             f"required {float(min_coverage):.2f}")

        ate = metric_value(run_dir, manifest, "ate_rmse_m")
        record["ate_rmse_m"] = ate
        record["status"] = status
        record["coverage"] = coverage

        if status == "ok":
            # A run whose ATE exceeds the divergence threshold must be RECORDED as
            # diverged, not quietly excluded downstream.
            if ate is not None and float(ate) > diverged_threshold:
                report.error(
                    f"{rel}: status 'ok' but ate_rmse_m {float(ate):.1f} exceeds the "
                    f"{diverged_threshold:.0f} m divergence threshold. The runner must "
                    f"record status 'diverged' with failure_reason "
                    f"'ate_threshold_exceeded' instead of leaving this for the asset "
                    f"script to drop silently.")
            elif ate is None:
                report.error(f"{rel}: status 'ok' but no ate_rmse_m")
            else:
                summary["runs_ok"] += 1
        elif status == "diverged":
            summary["runs_diverged"] += 1
            if not manifest.get("failure_reason"):
                report.error(f"{rel}: status 'diverged' without a failure_reason")
        elif status in ("failed", "failure"):
            summary["runs_failed"] += 1
            if not manifest.get("failure_reason"):
                report.error(f"{rel}: status 'failed' without a failure_reason")
        else:
            report.error(f"{rel}: unrecognised status '{status}' "
                         f"(expected ok / diverged / failed)")

    if requirements.get("require_same_git_sha") and len(git_shas) > 1:
        report.error(f"runs span {len(git_shas)} different commits: "
                     f"{', '.join(sorted(str(s)[:12] for s in git_shas))}. One table may "
                     f"not mix builds.")

    # --- declared matrix completeness ---------------------------------------
    for dataset, spec in expected.get("datasets", {}).items():
        required_trials = int(spec.get("trials", 1))
        if min_trials is not None and min_trials < required_trials:
            report.note(f"{dataset}: checking {min_trials} of the declared "
                        f"{required_trials} trials (--min-trials override; the declared "
                        f"count is what the paper must ultimately report)")
            required_trials = min_trials

        for scene in spec.get("scenes", []):
            for method in spec.get("methods", []):
                key = (dataset, scene, method)
                trials = cells.get(key, {})
                if not trials:
                    report.error(f"{dataset}/{scene}/{method}: no runs at all "
                                 f"(expected {required_trials})")
                    continue
                # Trials must be 1..N with nothing missing in between: a gap means a
                # run failed and was not noticed.
                missing = [t for t in range(1, required_trials + 1) if t not in trials]
                if missing:
                    report.error(f"{dataset}/{scene}/{method}: missing trial(s) "
                                 f"{missing} (have {sorted(trials)})")
                usable = [t for t, r in trials.items() if r.get("status") == "ok"]
                if not usable:
                    report.error(f"{dataset}/{scene}/{method}: every trial failed or "
                                 f"diverged; this cell has no usable result")

    # Anything present that the matrix does not declare is also a problem: it means
    # the tree and the declared protocol disagree.
    declared = {
        (dataset, scene, method)
        for dataset, spec in expected.get("datasets", {}).items()
        for scene in spec.get("scenes", [])
        for method in spec.get("methods", [])
    }
    for key in sorted(cells):
        if key not in declared:
            report.error(f"{key[0]}/{key[1]}/{key[2]}: present in the run tree but not "
                         f"declared in the expected matrix")

    return {"summary": summary, "cells": {"/".join(k): sorted(v) for k, v in cells.items()}}


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--root", required=True, type=Path, help="Run tree to validate")
    ap.add_argument("--expected", type=Path, default=DEFAULT_EXPECTED)
    ap.add_argument("--report", type=Path, default=None,
                    help="Write a JSON report here (also written as <root>/validation.json)")
    ap.add_argument("--min-trials", type=int, default=None,
                    help="Accept fewer trials than declared, for a gate run. The shortfall "
                         "is reported, never silent.")
    ap.add_argument("--allow-dirty", action="store_true",
                    help="Permit runs from a dirty worktree. Never use for a paper table.")
    args = ap.parse_args()

    if not args.root.is_dir():
        print(f"[fatal] no such run tree: {args.root}", file=sys.stderr)
        return 2
    expected = load_json(args.expected)
    if isinstance(expected, Exception):
        print(f"[fatal] cannot read {args.expected}: {expected}", file=sys.stderr)
        return 2

    report = Report()
    details = validate(args.root, expected, report, args.min_trials, args.allow_dirty)

    print(f"[validate] root: {args.root}")
    print(f"[validate] expected matrix: {args.expected}")
    if details.get("summary"):
        s = details["summary"]
        print(f"[validate] runs: {s['runs_total']} total, {s['runs_ok']} ok, "
              f"{s['runs_diverged']} diverged, {s['runs_failed']} failed, "
              f"{s['runs_oracle']} oracle")
    for note in report.notes:
        print(f"[validate] note: {note}")

    payload = {
        "root": str(args.root),
        "expected": str(args.expected),
        "result": "PASS" if report.ok else "FAIL",
        "errors": report.errors,
        "notes": report.notes,
        **details,
    }
    for path in filter(None, [args.report, args.root / "validation.json"]):
        try:
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text(json.dumps(payload, indent=2, sort_keys=True) + "\n")
        except OSError as exc:
            print(f"[warn] could not write {path}: {exc}", file=sys.stderr)

    if not report.ok:
        print(f"\nFAIL: {len(report.errors)} problem(s)\n")
        for message in report.errors:
            print(f"  - {message}")
        print("\nDo not generate publication assets from this tree.")
        return 1

    print("\nPASS: the run tree matches the declared matrix and every run is traceable.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
