#!/usr/bin/env python3
"""Shared helpers for Semantic–GeoDF ablation parsing, QC, and aggregation."""
from __future__ import annotations

import json
import re
import statistics
from dataclasses import dataclass, field
from pathlib import Path

# Canonical publication method names (longest aliases first for folder parsing).
# Legacy aliases remain parseable so old run trees can still be read; they must
# not appear as primary methods in new paper assets.
METHODS_ORDER = (
    "sem_geodf_mask_gated",
    "adaptive_arbitration",
    "union_noweight",
    "sem_geodf_noweight",
    "union_weight",
    "full_adaptive",
    "sem_geodf",
    "sequential",
    "semantic",
    "sad_sem",
    "geodf",
    "adaptive",
    "baseline",
)

# Paper default display order for the fixed-backbone main matrix.
METHODS_ORDER_DISPLAY = (
    "baseline",
    "geodf",
    "semantic",
    "union_noweight",
    "union_weight",
    "adaptive_arbitration",
)

LEGACY_METHOD_ALIASES = {
    "adaptive": "geodf",
    "geodf_adaptive": "geodf",
    "sad_sem": "semantic",
    "sem_geodf_noweight": "union_noweight",
    "sem_geodf": "union_weight",
}


def canonicalize_method(method: str) -> str:
    return LEGACY_METHOD_ALIASES.get(method, method)


@dataclass
class RunRecord:
    run_dir: Path
    scene: str
    method: str
    trial: int | None = None
    ate_rmse_m: float | None = None
    rpe_rmse_m: float | None = None
    bag_rate: float | None = None
    yolo: bool | None = None
    status: str = "unknown"
    oracle_ablation: bool = False
    sem_policy_dynamic_level: int | None = None
    protocol_fair: bool | None = None
    git_sha: str | None = None
    qc_ok: bool = True
    qc_issues: list[str] = field(default_factory=list)
    manifest: dict = field(default_factory=dict)
    metrics: dict = field(default_factory=dict)


def parse_run_name(name: str) -> tuple[str, str]:
  """Fallback when run_manifest.json is missing."""
  for method in METHODS_ORDER:
    needle = f"_{method}"
    if needle in name:
      scene = name.split(needle)[0]
      scene = re.sub(r"_t\d+.*$", "", scene)
      if "_s" in scene and scene.startswith("MH_"):
        scene = scene.rsplit("_s", 1)[0]
      return scene, method
  return name, "unknown"


def _name_method(name: str) -> str:
  _, method = parse_run_name(name)
  return method


def load_run_record(run_dir: Path, *, require_manifest: bool = False) -> RunRecord | None:
  metrics_path = run_dir / "eval" / "metrics.json"
  manifest_path = run_dir / "run_manifest.json"
  has_metrics = metrics_path.is_file()
  has_manifest = manifest_path.is_file()

  if not has_metrics and not has_manifest:
    return None

  manifest: dict = {}
  if has_manifest:
    manifest = json.loads(manifest_path.read_text())
  elif require_manifest:
    rec = RunRecord(
      run_dir=run_dir,
      scene=run_dir.name,
      method="unknown",
      status="failed",
      qc_ok=False,
      qc_issues=["missing_manifest"],
    )
    return rec

  metrics: dict = {}
  if has_metrics:
    metrics = json.loads(metrics_path.read_text())

  scene, method = parse_run_name(run_dir.name)
  if manifest.get("scene"):
    scene = str(manifest["scene"])
  if manifest.get("method"):
    method = str(manifest["method"])
  method = canonicalize_method(method)

  status = str(manifest.get("status", "ok" if has_metrics else "failed"))
  rec = RunRecord(
    run_dir=run_dir,
    scene=scene,
    method=method,
    trial=manifest.get("trial"),
    ate_rmse_m=metrics.get("ate_rmse_m") if has_metrics else None,
    rpe_rmse_m=metrics.get("rpe_rmse_m") if has_metrics else None,
    bag_rate=manifest.get("bag_rate"),
    yolo=manifest.get("yolo"),
    status=status,
    oracle_ablation=bool(manifest.get("oracle_ablation", False)),
    sem_policy_dynamic_level=manifest.get("sem_policy_dynamic_level"),
    protocol_fair=manifest.get("protocol_fair"),
    git_sha=manifest.get("git_sha"),
    manifest=manifest,
    metrics=metrics,
  )

  issues: list[str] = []
  if not has_manifest:
    issues.append("missing_manifest")
  if not has_metrics:
    issues.append("missing_metrics")
  if rec.ate_rmse_m is None:
    issues.append("missing_ate")
  if rec.status not in ("ok",):
    issues.append(f"status_{rec.status}")
  name_method = canonicalize_method(_name_method(run_dir.name))
  if method != "unknown" and name_method != "unknown" and method != name_method:
    issues.append(f"method_mismatch:{name_method}!={method}")

  vio_log = run_dir / "pht_vio_node.log"
  if not vio_log.is_file() or vio_log.stat().st_size == 0:
    issues.append("vio_log_missing_or_empty")
  elif vio_log.is_file() and "ERROR" in vio_log.read_text(errors="replace"):
    issues.append("vio_log_contains_error")

  rec.qc_issues = issues
  rec.qc_ok = len(issues) == 0
  return rec


def iter_run_records(
    root: Path,
    *,
    require_qc_ok: bool = False,
    exclude_oracle: bool = True,
    main_online_only: bool = False,
) -> list[RunRecord]:
  """Enumerate attempted trials from run_manifest.json first.

  A trial that fails before writing metrics.json is still an attempted trial and
  must appear in success-rate / penalized-metric accounting.
  """
  records: list[RunRecord] = []
  seen: set[Path] = set()

  for manifest_path in sorted(root.rglob("run_manifest.json")):
    run_dir = manifest_path.parent.resolve()
    if run_dir in seen:
      continue
    seen.add(run_dir)
    rec = load_run_record(run_dir)
    if rec is None:
      continue
    if exclude_oracle and rec.oracle_ablation:
      continue
    if main_online_only and rec.sem_policy_dynamic_level not in (None, -1):
      continue
    if require_qc_ok and not rec.qc_ok:
      continue
    records.append(rec)

  # Legacy trees that only have metrics.json (no manifest) remain readable.
  for metrics_path in sorted(root.rglob("eval/metrics.json")):
    run_dir = metrics_path.parent.parent.resolve()
    if run_dir in seen:
      continue
    seen.add(run_dir)
    rec = load_run_record(run_dir)
    if rec is None:
      continue
    if exclude_oracle and rec.oracle_ablation:
      continue
    if main_online_only and rec.sem_policy_dynamic_level not in (None, -1):
      continue
    if require_qc_ok and not rec.qc_ok:
      continue
    records.append(rec)
  return records


def mean_std(values: list[float]) -> tuple[float, float, int]:
  if not values:
    return 0.0, 0.0, 0
  mean = statistics.mean(values)
  std = statistics.stdev(values) if len(values) > 1 else 0.0
  return mean, std, len(values)
