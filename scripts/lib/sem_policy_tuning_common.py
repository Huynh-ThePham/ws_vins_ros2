#!/usr/bin/env python3
"""Train/hold-out split and scoring for Semantic policy threshold tuning."""
from __future__ import annotations

import json
import re
import sys
from dataclasses import asdict, dataclass
from pathlib import Path

_LIB = Path(__file__).resolve().parent
_SCRIPTS = _LIB.parent
if str(_SCRIPTS) not in sys.path:
    sys.path.insert(0, str(_SCRIPTS))

from simulate_sem_policy import (  # noqa: E402
    FrameSignals,
    PolicyFrame,
    PolicyParams,
    load_frames,
    replay_policy,
    summarize_policy,
    verify_replay,
)

# VIODE dynamic-level suffixes (city_day train only for tuning).
TRAIN_ENV = "city_day"
TRAIN_LEVELS = ("0_none", "1_low", "2_mid", "3_high")
HOLDOUT_ENVS = ("city_night", "parking_lot")
HOLDOUT_LEVELS = ("0_none", "1_low", "2_mid", "3_high")
MIN_POLICY_FRAMES = 500

LEVEL_WEIGHTS: dict[str, float] = {
    "0_none": 2.5,
    "1_low": 1.0,
    "2_mid": 1.0,
    "3_high": 2.5,
}


@dataclass(frozen=True)
class RunRef:
    run_dir: Path
    env: str
    level: str
    split: str  # train | holdout_viode | holdout_euroc
    trial: int | None = None

    @property
    def scene_key(self) -> str:
        if self.split == "holdout_euroc":
            return self.run_dir.name.split("_sem_geodf")[0]
        return f"{self.env}_{self.level}"

    @property
    def run_key(self) -> str:
        trial = f"_t{self.trial}" if self.trial is not None else ""
        return f"{self.scene_key}{trial}"


@dataclass
class TuningSelection:
    params: PolicyParams
    train_score: float
    per_run_train: dict[str, dict[str, float]]
    grid_rank: int


VIODE_LEVEL_PATTERN = r"(0_none|1_low|2_mid|3_high)"


def parse_viode_scene(name: str) -> tuple[str, str] | None:
    m = re.match(rf"^(city_day|city_night|parking_lot)_{VIODE_LEVEL_PATTERN}", name)
    if not m:
        return None
    return m.group(1), m.group(2)


def parse_trial(name: str) -> int | None:
    m = re.search(r"_t(\d+)(?:_|$)", name)
    return int(m.group(1)) if m else None


def _has_policy_columns(run_dir: Path) -> bool:
    stats = run_dir / "sem_geodf_stats.csv"
    if not stats.is_file():
        return False
    with stats.open() as f:
        header = f.readline()
        n_frames = sum(1 for _ in f)
    return (
        "sem_policy_state" in header
        and "sem_geo_overlap_ema" in header
        and n_frames >= MIN_POLICY_FRAMES
    )


def classify_run_dir(run_dir: Path) -> RunRef | None:
    name = run_dir.name
    if "sem_geodf_mask_gated" in name or "sequential" in name:
        return None
    if "_sem_geodf" not in name:
        return None
    stats = run_dir / "sem_geodf_stats.csv"
    if not stats.is_file():
        return None

    viode = parse_viode_scene(name)
    trial = parse_trial(name)
    if viode:
        env, level = viode
        if env == TRAIN_ENV and level in TRAIN_LEVELS:
            if not _has_policy_columns(run_dir):
                return None
            split = "train"
        elif env in HOLDOUT_ENVS and level in HOLDOUT_LEVELS:
            split = "holdout_viode"
        else:
            return None
        return RunRef(run_dir=run_dir, env=env, level=level, split=split, trial=trial)

    if name.startswith("MH_") and "_sem_geodf" in name and "mask_gated" not in name:
        return RunRef(run_dir=run_dir, env="euroc", level="", split="holdout_euroc", trial=trial)
    return None


def _stats_quality(run_dir: Path) -> tuple[int, str]:
    """Higher is better. Tie-break by run_dir.name (prefer _t1 ablation)."""
    stats = run_dir / "sem_geodf_stats.csv"
    if not stats.is_file():
        return (-1, run_dir.name)
    with stats.open() as f:
        header = f.readline().strip().split(",")
        n_frames = sum(1 for _ in f)
    score = 0
    if "sem_policy_state" in header:
        score += 100
    if "sem_policy_trigger_burst" in header:
        score += 10
    if n_frames >= 500:
        score += 20
    elif n_frames >= 100:
        score += 5
    if "fair1p0" in str(run_dir) and n_frames >= 500:
        score += 5
    if "sem_geodf_ablation" in str(run_dir):
        score += 3
    if run_dir.name.endswith("_t1") or "_t1_s" in run_dir.name:
        score += 2
    return (score, run_dir.name)


def discover_runs(roots: list[Path]) -> list[RunRef]:
    found: dict[Path, RunRef] = {}
    for root in roots:
        if not root.is_dir():
            continue
        for stats in root.rglob("sem_geodf_stats.csv"):
            run_dir = stats.parent
            ref = classify_run_dir(run_dir)
            if ref is None:
                continue
            found[run_dir] = ref
    return sorted(
        found.values(),
        key=lambda r: (r.split, r.scene_key, r.trial if r.trial is not None else -1, str(r.run_dir)),
    )


def level_objective(level: str, metrics: dict[str, float]) -> float:
    """Higher is better. Proxy objectives on policy behaviour (not ATE)."""
    if level == "0_none":
        # Static: penalize spurious dynamic-assist / hard reject.
        return 100.0 - 60.0 * metrics["frac_state_gt0"] - 40.0 * metrics["frac_hard_reject"]
    if level == "3_high":
        # High dynamic: reward assist+ and strong state when scene gate is on.
        return (
            40.0 * metrics["frac_state_ge1_scene"]
            + 60.0 * metrics["frac_state_2_scene"]
            - 20.0 * max(0.0, 0.35 - metrics["frac_state_ge1_scene"])
        )
    # Low/mid: moderate activation; avoid excess hard reject off-scene.
    return (
        35.0 * metrics["frac_state_ge1_scene"]
        + 25.0 * metrics["frac_state_2_scene"]
        - 35.0 * metrics["frac_hard_reject"]
        + 40.0 * (1.0 - metrics["frac_hard_reject"])
    )


def euroc_objective(metrics: dict[str, float]) -> float:
    """EuRoC generalization: mostly static-safe (no VIODE labels)."""
    return 100.0 - 50.0 * metrics["frac_state_gt0"] - 50.0 * metrics["frac_hard_reject"]


def score_train_runs(
    runs: list[RunRef], params: PolicyParams
) -> tuple[float, dict[str, dict[str, float]]]:
    per_run: dict[str, dict[str, float]] = {}
    total_w = 0.0
    total_score = 0.0
    for ref in runs:
        if ref.split != "train":
            continue
        frames = load_frames(ref.run_dir / "sem_geodf_stats.csv")
        replay = replay_policy(frames, params)
        metrics = summarize_policy(frames, replay)
        obj = level_objective(ref.level, metrics)
        w = LEVEL_WEIGHTS.get(ref.level, 1.0)
        per_run[ref.run_key] = {**metrics, "objective": obj, "weight": w}
        total_w += w
        total_score += w * obj
    avg = total_score / total_w if total_w else 0.0
    return avg, per_run


def score_holdout_runs(
    runs: list[RunRef], params: PolicyParams
) -> dict[str, dict[str, float]]:
    out: dict[str, dict[str, float]] = {}
    for ref in runs:
        if ref.split == "train":
            continue
        frames = load_frames(ref.run_dir / "sem_geodf_stats.csv")
        replay = replay_policy(frames, params)
        metrics = summarize_policy(frames, replay)
        if ref.split == "holdout_euroc":
            obj = euroc_objective(metrics)
        else:
            obj = level_objective(ref.level, metrics)
        out[ref.run_key] = {**metrics, "objective": obj, "split": ref.split}
    return out


def default_grid() -> dict[str, list]:
    return {
        "burst": [0.15, 0.18, 0.21],
        "strong": [0.18, 0.20, 0.22],
        "hold": [60, 120, 180],
        "overlap": [0.30, 0.35, 0.40],
    }


def iter_grid(
    grid: dict[str, list],
    *,
    sweep_overlap: bool = False,
) -> list[PolicyParams]:
    overlaps = grid["overlap"] if sweep_overlap else [0.35]
    combos: list[PolicyParams] = []
    for burst in grid["burst"]:
        for strong in grid["strong"]:
            for hold in grid["hold"]:
                for overlap in overlaps:
                    combos.append(
                        PolicyParams(
                            burst_ratio=burst,
                            strong_ratio=strong,
                            hold_frames=hold,
                            overlap_ratio=overlap,
                        )
                    )
    return combos


def verify_default_params(runs: list[RunRef]) -> list[dict]:
    params = PolicyParams()
    rows = []
    for ref in runs:
        if ref.split != "train":
            continue
        frames = load_frames(ref.run_dir / "sem_geodf_stats.csv")
        replay = replay_policy(frames, params)
        vf = verify_replay(frames, replay)
        rows.append({"scene": ref.run_key, **vf})
    return rows


def params_to_dict(p: PolicyParams) -> dict:
    return asdict(p)


def write_json(path: Path, obj: object) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(obj, indent=2) + "\n")
