#!/usr/bin/env python3
"""Offline replay of the online Semantic–GeoDF adaptive policy (feature_tracker.cpp).

Reads per-frame sem_geodf_stats.csv (logged signals) and replays
updateSemanticAdaptivePolicy() for arbitrary (burst, strong, hold, overlap) settings
without re-running VINS. Use only sem_geodf fusion runs with sem_adaptive_policy: 1
and sem_policy_dynamic_level: -1.

Usage:
    simulate_sem_policy.py RUN_DIR/sem_geodf_stats.csv --burst 0.18 --strong 0.20 --hold 120
    simulate_sem_policy.py STATS.csv --verify   # compare to logged policy state
    simulate_sem_policy.py STATS.csv --sweep
"""
from __future__ import annotations

import argparse
import csv
from dataclasses import dataclass
from pathlib import Path


@dataclass(frozen=True)
class PolicyParams:
    burst_ratio: float = 0.18
    strong_ratio: float = 0.20
    hold_frames: int = 120
    overlap_ratio: float = 0.35
    overlap_ema: float = 0.20
    min_geo_candidates: int = 2


@dataclass
class FrameSignals:
    mask_available: bool
    sem_mask_trusted: bool
    dynamic_pixel_ratio: float
    sem_activation_ema: float
    geo_valid: bool
    geo_frame_active: bool
    geo_overlap_pool: int
    sem_raw_count: int
    sem_geo_overlap: float
    sem_geo_overlap_ema_logged: float | None
    sem_scene_active: bool
    logged_state: int | None = None
    logged_hold: int | None = None
    logged_hard_reject: int | None = None


@dataclass
class PolicyFrame:
    state: int
    hold: int
    hard_reject: bool
    soft_mask: bool
    trigger_burst: bool
    trigger_strong: bool
    trigger_overlap: bool
    overlap_ema: float


def _truthy(row: dict[str, str], key: str) -> bool:
    try:
        return int(float(row[key])) != 0
    except (KeyError, ValueError):
        return False


def _float(row: dict[str, str], key: str, default: float = 0.0) -> float:
    try:
        return float(row[key])
    except (KeyError, ValueError):
        return default


def _int(row: dict[str, str], key: str, default: int = 0) -> int:
    try:
        return int(float(row[key]))
    except (KeyError, ValueError):
        return default


def load_frames(path: Path) -> list[FrameSignals]:
    frames: list[FrameSignals] = []
    with path.open() as f:
        for row in csv.DictReader(f):
            logged_ema_raw = row.get("sem_geo_overlap_ema")
            logged_ema: float | None = None
            if logged_ema_raw not in (None, "", "-1"):
                try:
                    v = float(logged_ema_raw)
                    if v >= 0.0:
                        logged_ema = v
                except ValueError:
                    pass
            frames.append(
                FrameSignals(
                    mask_available=_truthy(row, "mask_available"),
                    sem_mask_trusted=_truthy(row, "sem_mask_trusted"),
                    dynamic_pixel_ratio=_float(row, "dynamic_pixel_ratio"),
                    sem_activation_ema=_float(row, "sem_activation_ema"),
                    geo_valid=_truthy(row, "geo_valid")
                    if "geo_valid" in row
                    else _truthy(row, "geo_frame_active"),
                    geo_frame_active=_truthy(row, "geo_frame_active"),
                    geo_overlap_pool=_int(row, "geo_overlap_pool", _int(row, "geo_candidates")),
                    sem_raw_count=_int(row, "sem_candidates"),
                    sem_geo_overlap=_float(row, "sem_geo_overlap"),
                    sem_geo_overlap_ema_logged=logged_ema,
                    sem_scene_active=_truthy(row, "sem_scene_active"),
                    logged_state=_int(row, "sem_policy_state") if row.get("sem_policy_state") else None,
                    logged_hold=_int(row, "sem_policy_hold") if row.get("sem_policy_hold") else None,
                    logged_hard_reject=_int(row, "sem_policy_hard_reject")
                    if row.get("sem_policy_hard_reject")
                    else None,
                )
            )
    return frames


def replay_policy(frames: list[FrameSignals], params: PolicyParams) -> list[PolicyFrame]:
    """Match FeatureTracker::updateSemanticAdaptivePolicy (online path only)."""
    hold = 0
    overlap_ema = -1.0
    out: list[PolicyFrame] = []
    use_logged_overlap_ema = (
        params.overlap_ratio == 0.35
        and params.overlap_ema == 0.20
        and any(fr.sem_geo_overlap_ema_logged is not None for fr in frames)
    )

    for fr in frames:
        if not fr.mask_available or not fr.sem_mask_trusted:
            hold = 0
            out.append(
                PolicyFrame(
                    state=0,
                    hold=0,
                    hard_reject=False,
                    soft_mask=False,
                    trigger_burst=False,
                    trigger_strong=False,
                    trigger_overlap=False,
                    overlap_ema=0.0,
                )
            )
            continue

        sem_geo_overlap_last = 0.0
        if fr.geo_valid:
            sem_geo_overlap_last = fr.sem_geo_overlap

        if use_logged_overlap_ema and fr.sem_geo_overlap_ema_logged is not None:
            overlap_ema = fr.sem_geo_overlap_ema_logged
        else:
            if overlap_ema < 0.0:
                overlap_ema = sem_geo_overlap_last
            else:
                overlap_ema = (
                    params.overlap_ema * sem_geo_overlap_last
                    + (1.0 - params.overlap_ema) * overlap_ema
                )

        semantic_burst = fr.dynamic_pixel_ratio >= params.burst_ratio
        semantic_strong = fr.sem_activation_ema >= params.strong_ratio
        min_geo = max(1, params.min_geo_candidates)
        semantic_geo_agree = (
            fr.geo_valid
            and (fr.geo_overlap_pool >= min_geo or fr.sem_raw_count >= min_geo)
            and overlap_ema >= params.overlap_ratio
        )

        if semantic_burst or semantic_strong or semantic_geo_agree:
            hold = max(0, params.hold_frames)
        elif hold > 0:
            hold -= 1

        dynamic_assist = hold > 0
        geo_evidence = fr.geo_valid and fr.geo_overlap_pool > 0
        if semantic_strong or semantic_geo_agree or (dynamic_assist and geo_evidence):
            state = 2
        elif dynamic_assist:
            state = 1
        else:
            state = 0

        soft_mask = fr.sem_scene_active or dynamic_assist
        hard_reject = fr.sem_scene_active and (state > 0)

        out.append(
            PolicyFrame(
                state=state,
                hold=hold,
                hard_reject=hard_reject,
                soft_mask=soft_mask,
                trigger_burst=semantic_burst,
                trigger_strong=semantic_strong,
                trigger_overlap=semantic_geo_agree,
                overlap_ema=overlap_ema,
            )
        )
    return out


def summarize_policy(frames: list[FrameSignals], replay: list[PolicyFrame]) -> dict[str, float]:
    n = len(replay)
    if n == 0:
        return {}
    scene_idx = [i for i, fr in enumerate(frames) if fr.sem_scene_active]
    n_scene = len(scene_idx)

    def frac(pred) -> float:
        return sum(pred(r) for r in replay) / n

    def frac_scene(pred) -> float:
        if n_scene == 0:
            return 0.0
        return sum(pred(replay[i]) for i in scene_idx) / n_scene

    return {
        "frames": float(n),
        "frac_state_gt0": frac(lambda r: r.state > 0),
        "frac_state_2": frac(lambda r: r.state == 2),
        "frac_hard_reject": frac(lambda r: r.hard_reject),
        "frac_soft_mask": frac(lambda r: r.soft_mask),
        "frac_state_ge1_scene": frac_scene(lambda r: r.state >= 1),
        "frac_state_2_scene": frac_scene(lambda r: r.state == 2),
        "frac_hard_reject_scene": frac_scene(lambda r: r.hard_reject),
        "mean_hold": sum(r.hold for r in replay) / n,
    }


def verify_replay(frames: list[FrameSignals], replay: list[PolicyFrame]) -> dict[str, float]:
    comparable = [
        (fr, rp)
        for fr, rp in zip(frames, replay)
        if fr.logged_state is not None and fr.mask_available and fr.sem_mask_trusted
    ]
    if not comparable:
        return {"n": 0.0, "state_match_pct": 0.0, "hold_match_pct": 0.0, "hard_match_pct": 0.0}
    n = len(comparable)
    state_ok = sum(rp.state == fr.logged_state for fr, rp in comparable)
    hold_ok = sum(rp.hold == fr.logged_hold for fr, rp in comparable if fr.logged_hold is not None)
    hard_ok = sum(
        int(rp.hard_reject) == fr.logged_hard_reject
        for fr, rp in comparable
        if fr.logged_hard_reject is not None
    )
    n_hold = sum(1 for fr, _ in comparable if fr.logged_hold is not None)
    n_hard = sum(1 for fr, _ in comparable if fr.logged_hard_reject is not None)
    return {
        "n": float(n),
        "state_match_pct": 100.0 * state_ok / n,
        "hold_match_pct": 100.0 * hold_ok / n_hold if n_hold else 0.0,
        "hard_match_pct": 100.0 * hard_ok / n_hard if n_hard else 0.0,
    }


def name_of(path: Path) -> str:
    if path.name == "sem_geodf_stats.csv":
        return path.parent.name
    return path.stem


def main() -> None:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("paths", nargs="+", type=Path, help="sem_geodf_stats.csv or run directories")
    ap.add_argument("--burst", type=float, default=0.18)
    ap.add_argument("--strong", type=float, default=0.20)
    ap.add_argument("--hold", type=int, default=120)
    ap.add_argument("--overlap", type=float, default=0.35)
    ap.add_argument("--overlap-ema", type=float, default=0.20)
    ap.add_argument("--min-geo", type=int, default=2)
    ap.add_argument("--verify", action="store_true", help="compare replay to logged policy columns")
    ap.add_argument("--sweep", action="store_true", help="print coarse grid of policy activation metrics")
    args = ap.parse_args()

    stats_paths: list[Path] = []
    for p in args.paths:
        if p.is_dir():
            stats_paths.append(p / "sem_geodf_stats.csv")
        else:
            stats_paths.append(p)

    params = PolicyParams(
        burst_ratio=args.burst,
        strong_ratio=args.strong,
        hold_frames=args.hold,
        overlap_ratio=args.overlap,
        overlap_ema=args.overlap_ema,
        min_geo_candidates=args.min_geo,
    )

    if args.sweep:
        bursts = [0.15, 0.18, 0.21]
        strongs = [0.18, 0.20, 0.22]
        holds = [60, 120, 180]
        print(f"overlap={params.overlap_ratio} overlap_ema={params.overlap_ema}\n")
        for hold in holds:
            print(f"=== hold={hold} ===")
            header = f"{'run':36s} " + " ".join(f"b={b:.2f}s={s:.2f}" for b in bursts for s in strongs)
            print(header)
            print("-" * len(header))
            for path in stats_paths:
                if not path.is_file():
                    continue
                frames = load_frames(path)
                cells: list[str] = []
                for b in bursts:
                    for s in strongs:
                        rp = PolicyParams(
                            burst_ratio=b,
                            strong_ratio=s,
                            hold_frames=hold,
                            overlap_ratio=params.overlap_ratio,
                            overlap_ema=params.overlap_ema,
                            min_geo_candidates=params.min_geo_candidates,
                        )
                        sm = summarize_policy(frames, replay_policy(frames, rp))
                        cells.append(f"{sm.get('frac_state_ge1_scene', 0.0) * 100:5.1f}")
                print(f"{name_of(path):36s} " + " ".join(cells))
        return

    print(
        f"burst={params.burst_ratio} strong={params.strong_ratio} "
        f"hold={params.hold_frames} overlap={params.overlap_ratio}\n"
    )
    print(
        f"{'run':40s} {'frames':>7s} {'st>0%':>6s} {'st=2%':>6s} "
        f"{'hard%':>6s} {'scene≥1%':>8s} {'scene=2%':>8s}"
    )
    print("-" * 88)
    for path in stats_paths:
        if not path.is_file():
            print(f"{name_of(path):40s} MISSING")
            continue
        frames = load_frames(path)
        replay = replay_policy(frames, params)
        sm = summarize_policy(frames, replay)
        print(
            f"{name_of(path):40s} {int(sm['frames']):7d} "
            f"{sm['frac_state_gt0'] * 100:6.1f} {sm['frac_state_2'] * 100:6.1f} "
            f"{sm['frac_hard_reject'] * 100:6.1f} "
            f"{sm['frac_state_ge1_scene'] * 100:8.1f} {sm['frac_state_2_scene'] * 100:8.1f}"
        )
        if args.verify:
            vf = verify_replay(frames, replay)
            print(
                f"  verify n={int(vf['n'])} "
                f"state={vf['state_match_pct']:.1f}% "
                f"hold={vf['hold_match_pct']:.1f}% "
                f"hard={vf['hard_match_pct']:.1f}%"
            )


if __name__ == "__main__":
    main()
