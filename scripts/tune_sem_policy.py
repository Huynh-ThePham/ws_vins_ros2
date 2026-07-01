#!/usr/bin/env python3
"""Select Semantic policy thresholds on city_day (train) only.

Discovers sem_geodf fusion runs, replays policy offline, grid-searches
(burst, strong, hold) with overlap fixed (or light sweep), writes:
  results/sem_policy_tuning/selected_params.yaml
  results/sem_policy_tuning/tuning_report.json

Hold-out behaviour (city_night, parking_lot, EuRoC) is reported but never
used for selection.
"""
from __future__ import annotations

import argparse
import sys
from pathlib import Path

_SCRIPT_DIR = Path(__file__).resolve().parent
sys.path.insert(0, str(_SCRIPT_DIR / "lib"))

from sem_policy_tuning_common import (  # noqa: E402
    TRAIN_LEVELS,
    PolicyParams,
    default_grid,
    discover_runs,
    iter_grid,
    params_to_dict,
    score_holdout_runs,
    score_train_runs,
    verify_default_params,
    write_json,
)


def main() -> None:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument(
        "--roots",
        nargs="+",
        type=Path,
        default=[
            Path("results/sem_geodf_ablation"),
            Path("results/sem_geodf_ablation/fair1p0"),
            Path("results/sem_geodf_compare"),
        ],
        help="Result roots to scan for sem_geodf_stats.csv",
    )
    ap.add_argument(
        "--out-dir",
        type=Path,
        default=Path("results/sem_policy_tuning"),
    )
    ap.add_argument(
        "--sweep-overlap",
        action="store_true",
        help="Include overlap in grid (default: fixed at YAML 0.35)",
    )
    ap.add_argument("--top-k", type=int, default=5, help="Save top-k train configs")
    ap.add_argument(
        "--allow-incomplete-train",
        action="store_true",
        help="Allow a draft report when some city_day train levels are missing",
    )
    args = ap.parse_args()

    ws = _SCRIPT_DIR.parent
    roots = [(ws / r).resolve() for r in args.roots]
    out_dir = (ws / args.out_dir).resolve()
    out_dir.mkdir(parents=True, exist_ok=True)

    runs = discover_runs(roots)
    train_runs = [r for r in runs if r.split == "train"]
    holdout_runs = [r for r in runs if r.split != "train"]

    if not train_runs:
        print("[error] No city_day train sem_geodf runs found.", file=sys.stderr)
        print(
            "  Need sem_geodf_stats.csv with sem_policy_state columns under "
            "city_day_{0_none,1_low,2_mid,3_high}_sem_geodf*",
            file=sys.stderr,
        )
        sys.exit(1)

    missing_train = sorted(set(TRAIN_LEVELS) - {r.level for r in train_runs})
    if missing_train and not args.allow_incomplete_train:
        print(
            f"[error] Train split incomplete ({len(train_runs)}/{len(TRAIN_LEVELS)} levels). "
            f"Missing policy-logged runs for: {', '.join(missing_train)}.",
            file=sys.stderr,
        )
        print(
            "  Run scripts/run_sem_policy_protocol.sh train, or pass "
            "--allow-incomplete-train only for a draft/non-paper table.",
            file=sys.stderr,
        )
        sys.exit(1)

    if missing_train:
        missing = sorted(set(TRAIN_LEVELS) - {r.level for r in train_runs})
        print(
            f"[warn] Train split incomplete ({len(train_runs)}/{len(TRAIN_LEVELS)} levels). "
            f"Missing policy-logged runs for: {', '.join(missing)}. "
            "Draft table only; do not use as paper protocol.",
            file=sys.stderr,
        )

    print(f"Train runs ({len(train_runs)}):")
    for r in train_runs:
        print(f"  {r.run_key}  <- {r.run_dir}")
    print(f"Hold-out runs ({len(holdout_runs)}):")
    for r in holdout_runs:
        print(f"  [{r.split}] {r.run_key}  <- {r.run_dir}")
    if not holdout_runs:
        print("  (none yet — rerun ablation on city_night, parking_lot, EuRoC)")

    verify = verify_default_params(train_runs)
    print("\nReplay verification (default YAML params vs logged policy):")
    for row in verify:
        print(
            f"  {row['scene']}: state={row['state_match_pct']:.1f}% "
            f"hold={row['hold_match_pct']:.1f}% hard={row['hard_match_pct']:.1f}%"
        )

    grid = default_grid()
    ranked: list[tuple[float, PolicyParams, dict]] = []
    for params in iter_grid(grid, sweep_overlap=args.sweep_overlap):
        score, per_run = score_train_runs(train_runs, params)
        ranked.append((score, params, per_run))
    ranked.sort(key=lambda x: x[0], reverse=True)

    best_score, best_params, best_train = ranked[0]
    holdout = score_holdout_runs(holdout_runs, best_params) if holdout_runs else {}

    default_params = PolicyParams()
    default_score, default_train = score_train_runs(train_runs, default_params)
    default_rank = 1 + sum(1 for s, _, _ in ranked if s > default_score)

    selected_yaml = out_dir / "selected_params.yaml"
    lines = [
        "# Semantic policy thresholds — selected on city_day train only.",
        "# Do not re-tune after viewing hold-out metrics.",
        f"# train_levels_present={','.join(sorted({r.level for r in train_runs}))}",
        f"# train_levels_missing={','.join(missing_train) if missing_train else 'none'}",
        f"# train_runs={len(train_runs)}",
        "# status=draft_incomplete_train" if missing_train else "# status=paper_train_complete",
        f"# train_score={best_score:.2f}  default_score={default_score:.2f}  default_rank={default_rank}/{len(ranked)}",
        "",
        f"sem_policy_burst_ratio: {best_params.burst_ratio}",
        f"sem_policy_strong_ratio: {best_params.strong_ratio}",
        f"sem_policy_hold_frames: {best_params.hold_frames}",
        f"sem_policy_overlap_ratio: {best_params.overlap_ratio}",
        f"sem_policy_overlap_ema: {best_params.overlap_ema}",
        f"sem_policy_min_geo_candidates: {best_params.min_geo_candidates}",
        "",
    ]
    selected_yaml.write_text("\n".join(lines))

    report = {
        "protocol": {
            "train_env": "city_day",
            "train_levels_required": list(TRAIN_LEVELS),
            "train_levels_present": sorted({r.level for r in train_runs}),
            "train_levels_missing": missing_train,
            "train_run_count": len(train_runs),
            "allow_incomplete_train": args.allow_incomplete_train,
            "holdout_envs": ["city_night", "parking_lot", "euroc"],
            "selection_metric": "weighted level_objective on offline policy replay",
            "overlap_sweep": args.sweep_overlap,
        },
        "discovered_runs": {
            "train": [r.run_key for r in train_runs],
            "holdout": [r.run_key for r in holdout_runs],
        },
        "verify_default_replay": verify,
        "selected": {
            "params": params_to_dict(best_params),
            "train_score": best_score,
            "per_run_train": best_train,
            "holdout": holdout,
        },
        "default_yaml": {
            "params": params_to_dict(default_params),
            "train_score": default_score,
            "train_rank": default_rank,
            "per_run_train": default_train,
        },
        "top_k": [
            {
                "rank": i + 1,
                "train_score": score,
                "params": params_to_dict(p),
            }
            for i, (score, p, _) in enumerate(ranked[: args.top_k])
        ],
    }
    write_json(out_dir / "tuning_report.json", report)

    print(f"\nSelected (train score {best_score:.2f}):")
    print(f"  burst={best_params.burst_ratio} strong={best_params.strong_ratio} "
          f"hold={best_params.hold_frames} overlap={best_params.overlap_ratio}")
    print(f"Default YAML rank {default_rank}/{len(ranked)} (score {default_score:.2f})")
    print(f"Wrote {selected_yaml}")
    print(f"Wrote {out_dir / 'tuning_report.json'}")


if __name__ == "__main__":
    main()
