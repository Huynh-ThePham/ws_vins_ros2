#!/usr/bin/env python3
"""Audit Semantic-GeoDF configs/results for no-leak main-result protocol.

The audit is intentionally conservative:
  - main configs must keep sem_policy_dynamic_level == -1;
  - oracle-labelled manifests are rejected unless explicitly allowed;
  - selected params must come from a complete train split unless draft is allowed;
  - fair bag-rate is required for result manifests by default.
"""
from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path


DEFAULT_CONFIGS = (
    Path("src/config/euroc/euroc_stereo_imu_sem_geodf_config.yaml"),
    Path("src/config/viode/viode_stereo_imu_sem_geodf_config.yaml"),
)


def read_flat_yaml(path: Path) -> dict[str, str]:
    data: dict[str, str] = {}
    for raw in path.read_text().splitlines():
        line = raw.strip()
        if not line or line.startswith("#") or line.startswith("%") or ":" not in line:
            continue
        key, value = line.split(":", 1)
        value = value.split("#", 1)[0].strip().strip('"').strip("'")
        data[key.strip()] = value
    return data


def as_int(value: str | None, default: int = 0) -> int:
    if value is None or value == "":
        return default
    return int(float(value))


def as_float(value: str | None, default: float = 0.0) -> float:
    if value is None or value == "":
        return default
    return float(value)


def fail(msgs: list[str], path: Path, msg: str) -> None:
    msgs.append(f"{path}: {msg}")


def audit_config(path: Path, *, allow_oracle: bool) -> list[str]:
    issues: list[str] = []
    if not path.is_file():
        fail(issues, path, "missing config")
        return issues

    cfg = read_flat_yaml(path)
    if as_int(cfg.get("sem_geodf_fusion"), 0) != 1:
        return issues

    level = as_int(cfg.get("sem_policy_dynamic_level"), -1)
    if level != -1 and not allow_oracle:
        fail(
            issues,
            path,
            f"sem_policy_dynamic_level={level}; main protocol requires -1 online policy",
        )

    if as_int(cfg.get("sem_geodf_backend_weight"), 0):
        min_w = as_float(cfg.get("sem_geodf_backend_min_weight"), 0.25)
        sem_w = as_float(cfg.get("sem_geodf_backend_semantic_weight"), 0.55)
        geo_w = as_float(cfg.get("sem_geodf_backend_geo_weight"), 0.75)
        agree_w = as_float(cfg.get("sem_geodf_backend_agree_weight"), 0.25)
        recovery = as_float(cfg.get("sem_geodf_backend_recovery"), 0.20)
        if not (0.0 < min_w <= 1.0):
            fail(issues, path, f"invalid sem_geodf_backend_min_weight={min_w}")
        for key, value in (
            ("sem_geodf_backend_semantic_weight", sem_w),
            ("sem_geodf_backend_geo_weight", geo_w),
            ("sem_geodf_backend_agree_weight", agree_w),
        ):
            if not (min_w <= value <= 1.0):
                fail(issues, path, f"{key}={value} outside [{min_w}, 1.0]")
        if not (0.0 <= recovery <= 1.0):
            fail(issues, path, f"sem_geodf_backend_recovery={recovery} outside [0, 1]")

    return issues


def audit_selected_params(path: Path, *, allow_draft: bool) -> list[str]:
    issues: list[str] = []
    if not path.is_file():
        fail(issues, path, "missing selected params file")
        return issues
    text = path.read_text()
    if "sem_policy_dynamic_level" in text:
        fail(issues, path, "selected params must not set oracle sem_policy_dynamic_level")
    if "status=draft_incomplete_train" in text and not allow_draft:
        fail(issues, path, "draft_incomplete_train selected params are not valid for main results")
    if "status=paper_train_complete" not in text and not allow_draft:
        fail(issues, path, "missing status=paper_train_complete marker")
    return issues


def audit_manifest(path: Path, *, allow_oracle: bool, allow_nonfair: bool) -> list[str]:
    issues: list[str] = []
    try:
        manifest = json.loads(path.read_text())
    except json.JSONDecodeError as exc:
        fail(issues, path, f"invalid JSON: {exc}")
        return issues

    if manifest.get("oracle_ablation") and not allow_oracle:
        fail(issues, path, "oracle_ablation=true in main result root")
    level = manifest.get("sem_policy_dynamic_level")
    if level not in (None, -1) and not allow_oracle:
        fail(issues, path, f"sem_policy_dynamic_level={level} in main result root")
    if manifest.get("protocol_fair") is False and not allow_nonfair:
        fail(issues, path, "protocol_fair=false")
    return issues


def main() -> None:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--configs", nargs="*", type=Path, default=None)
    ap.add_argument("--selected-params", type=Path)
    ap.add_argument("--results-root", type=Path)
    ap.add_argument("--allow-oracle", action="store_true")
    ap.add_argument("--allow-draft-selected", action="store_true")
    ap.add_argument("--allow-nonfair", action="store_true")
    ap.add_argument("--quiet", action="store_true")
    args = ap.parse_args()

    ws = Path(__file__).resolve().parents[1]
    config_paths = args.configs if args.configs is not None else list(DEFAULT_CONFIGS)
    issues: list[str] = []

    for cfg in config_paths:
        path = cfg if cfg.is_absolute() else ws / cfg
        issues.extend(audit_config(path, allow_oracle=args.allow_oracle))

    if args.selected_params:
        path = args.selected_params if args.selected_params.is_absolute() else ws / args.selected_params
        issues.extend(
            audit_selected_params(path, allow_draft=args.allow_draft_selected)
        )

    if args.results_root:
        root = args.results_root if args.results_root.is_absolute() else ws / args.results_root
        for manifest in sorted(root.rglob("run_manifest.json")):
            issues.extend(
                audit_manifest(
                    manifest,
                    allow_oracle=args.allow_oracle,
                    allow_nonfair=args.allow_nonfair,
                )
            )

    if issues:
        print("[sem-geodf-audit] FAIL", file=sys.stderr)
        for issue in issues:
            print(f"  - {issue}", file=sys.stderr)
        raise SystemExit(1)

    if not args.quiet:
        print("[sem-geodf-audit] PASS")


if __name__ == "__main__":
    main()
