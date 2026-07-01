#!/usr/bin/env python3
"""One-page sensitivity table: burst × strong × hold (overlap fixed or light sweep).

Reads city_day train runs + hold-out runs, writes:
  results/sem_policy_tuning/SENSITIVITY_TABLE.md
  results/sem_policy_tuning/sensitivity_grid.csv
"""
from __future__ import annotations

import argparse
import csv
import sys
from pathlib import Path

_SCRIPT_DIR = Path(__file__).resolve().parent
sys.path.insert(0, str(_SCRIPT_DIR / "lib"))

from sem_policy_tuning_common import (  # noqa: E402
    HOLDOUT_ENVS,
    LEVEL_WEIGHTS,
    PolicyParams,
    TRAIN_LEVELS,
    default_grid,
    discover_runs,
    iter_grid,
    level_objective,
    euroc_objective,
)
from simulate_sem_policy import load_frames, replay_policy, summarize_policy  # noqa: E402


def fmt_pct(x: float) -> str:
    return f"{x * 100:.1f}"


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
    )
    ap.add_argument("--out-dir", type=Path, default=Path("results/sem_policy_tuning"))
    ap.add_argument(
        "--overlap",
        type=float,
        default=0.35,
        help="Fixed overlap_ratio for main 3D grid (paper table)",
    )
    ap.add_argument(
        "--overlap-sweep",
        action="store_true",
        help="Add appendix rows for overlap 0.30/0.40 at default burst/strong/hold",
    )
    ap.add_argument(
        "--allow-incomplete-train",
        action="store_true",
        help="Allow a draft table when some city_day train levels are missing",
    )
    args = ap.parse_args()

    ws = _SCRIPT_DIR.parent
    roots = [(ws / r).resolve() for r in args.roots]
    out_dir = (ws / args.out_dir).resolve()
    out_dir.mkdir(parents=True, exist_ok=True)

    runs = discover_runs(roots)
    train = [r for r in runs if r.split == "train"]
    holdout = [r for r in runs if r.split != "train"]
    if not train:
        print("[error] No city_day train runs.", file=sys.stderr)
        sys.exit(1)
    missing_train = sorted(set(TRAIN_LEVELS) - {r.level for r in train})
    if missing_train and not args.allow_incomplete_train:
        print(
            f"[error] Train split incomplete ({len(train)}/{len(TRAIN_LEVELS)} levels). "
            f"Missing: {', '.join(missing_train)}.",
            file=sys.stderr,
        )
        sys.exit(1)

    grid = default_grid()
    combos = iter_grid(grid, sweep_overlap=False)

    # Preload train frames
    train_data = []
    for ref in train:
        frames = load_frames(ref.run_dir / "sem_geodf_stats.csv")
        train_data.append((ref, frames))

    rows_csv: list[dict] = []
    # Aggregate train score per combo
    combo_scores: dict[tuple, float] = {}
    combo_cells: dict[tuple, dict[str, float]] = {}

    for params in combos:
        if params.overlap_ratio != args.overlap:
            p = PolicyParams(
                burst_ratio=params.burst_ratio,
                strong_ratio=params.strong_ratio,
                hold_frames=params.hold_frames,
                overlap_ratio=args.overlap,
            )
        else:
            p = params
        key = (p.burst_ratio, p.strong_ratio, p.hold_frames)
        total = 0.0
        total_w = 0.0
        cell_values: dict[str, list[float]] = {}
        for ref, frames in train_data:
            replay = replay_policy(frames, p)
            metrics = summarize_policy(frames, replay)
            obj = level_objective(ref.level, metrics)
            w = LEVEL_WEIGHTS.get(ref.level, 1.0)
            cell_values.setdefault(ref.level, []).append(obj)
            total += w * obj
            total_w += w
        cell = {
            level: sum(values) / len(values)
            for level, values in cell_values.items()
            if values
        }
        weighted_score = total / total_w if total_w else 0.0
        combo_scores[key] = weighted_score
        combo_cells[key] = cell
        rows_csv.append(
            {
                "split": "train_mean",
                "burst": p.burst_ratio,
                "strong": p.strong_ratio,
                "hold": p.hold_frames,
                "overlap": p.overlap_ratio,
                "score": weighted_score,
                **{f"obj_{lv}": cell.get(lv, 0.0) for lv in ("0_none", "1_low", "2_mid", "3_high")},
            }
        )

    default_p = PolicyParams(overlap_ratio=args.overlap)
    default_key = (default_p.burst_ratio, default_p.strong_ratio, default_p.hold_frames)

    # Hold-out at default YAML point + selected if present
    selected_path = out_dir / "selected_params.yaml"
    selected_p = default_p
    if selected_path.is_file():
        vals = {}
        for line in selected_path.read_text().splitlines():
            if ":" in line and not line.strip().startswith("#"):
                k, v = line.split(":", 1)
                vals[k.strip()] = float(v.strip())
        if vals:
            selected_p = PolicyParams(
                burst_ratio=vals.get("sem_policy_burst_ratio", 0.18),
                strong_ratio=vals.get("sem_policy_strong_ratio", 0.20),
                hold_frames=int(vals.get("sem_policy_hold_frames", 120)),
                overlap_ratio=vals.get("sem_policy_overlap_ratio", args.overlap),
            )

    def holdout_block(params: PolicyParams, label: str) -> list[str]:
        lines = [f"### Hold-out @ {label}", ""]
        lines.append("| scene | split | st>0% | hard% | scene≥1% | objective |")
        lines.append("|-------|-------|------:|------:|---------:|----------:|")
        for ref in sorted(holdout, key=lambda r: r.scene_key):
            frames = load_frames(ref.run_dir / "sem_geodf_stats.csv")
            replay = replay_policy(frames, params)
            m = summarize_policy(frames, replay)
            obj = euroc_objective(m) if ref.split == "holdout_euroc" else level_objective(ref.level, m)
            lines.append(
                f"| {ref.scene_key} | {ref.split} | {fmt_pct(m['frac_state_gt0'])} | "
                f"{fmt_pct(m['frac_hard_reject'])} | {fmt_pct(m['frac_state_ge1_scene'])} | {obj:.1f} |"
            )
            rows_csv.append(
                {
                    "split": ref.split,
                    "scene": ref.scene_key,
                    "burst": params.burst_ratio,
                    "strong": params.strong_ratio,
                    "hold": params.hold_frames,
                    "overlap": params.overlap_ratio,
                    "score": obj,
                    "frac_state_gt0": m["frac_state_gt0"],
                    "frac_hard_reject": m["frac_hard_reject"],
                    "frac_state_ge1_scene": m["frac_state_ge1_scene"],
                }
            )
        if not holdout:
            lines.append("| *(pending ablation runs)* | | | | | |")
        lines.append("")
        return lines

    present_holdout = {ref.env for ref in holdout if ref.split == "holdout_viode"}
    missing_holdout_envs = sorted(set(HOLDOUT_ENVS) - present_holdout)

    bursts = grid["burst"]
    strongs = grid["strong"]
    holds = grid["hold"]

    md: list[str] = [
        "# Semantic policy sensitivity (offline replay)",
        "",
        f"**Train:** `city_day` levels present: "
        f"{', '.join(sorted({ref.level for ref, _ in train_data}))} (selection split).  ",
        f"**Train completeness:** {len({ref.level for ref, _ in train_data})}/{len(TRAIN_LEVELS)}"
        + (f" (missing: {', '.join(missing_train)})  " if missing_train else " (complete)  "),
        f"**Train runs:** {len(train_data)} sem_geodf trials.  ",
        "**Hold-out:** `city_night`, `parking_lot`, EuRoC (report only).  ",
        "**Hold-out availability:** "
        + (
            f"missing VIODE env(s): {', '.join(missing_holdout_envs)}.  "
            if missing_holdout_envs
            else "VIODE hold-out envs present.  "
        ),
        f"**Overlap fixed:** `{args.overlap}` (`sem_policy_overlap_ema=0.20`).  ",
        "Cell values = weighted train `level_objective` (higher = better static/dynamic trade-off).  ",
        "**Bold** = default YAML; *italic* = train-selected if tuning script was run.",
        "",
    ]

    for hold in holds:
        md.append(f"## hold_frames = {hold}")
        md.append("")
        header = "| strong \\ burst | " + " | ".join(f"{b:.2f}" for b in bursts) + " |"
        md.append(header)
        md.append("|" + "---|" * (len(bursts) + 1))
        for s in strongs:
            cells = []
            for b in bursts:
                key = (b, s, hold)
                score = combo_scores.get(key, float("nan"))
                text = f"{score:.1f}"
                if key == default_key:
                    text = f"**{text}**"
                if key == (selected_p.burst_ratio, selected_p.strong_ratio, selected_p.hold_frames):
                    text = f"*{text}*"
                cells.append(text)
            md.append(f"| {s:.2f} | " + " | ".join(cells) + " |")
        md.append("")

    md.append("## Per-level train objectives at default YAML")
    md.append("")
    md.append("| level | objective |")
    md.append("|-------|----------:|")
    if default_key in combo_cells:
        for level in TRAIN_LEVELS:
            if level in combo_cells[default_key]:
                md.append(f"| {level} | {combo_cells[default_key].get(level, 0.0):.1f} |")
    md.append("")

    md.extend(holdout_block(default_p, "default YAML"))
    if (selected_p.burst_ratio, selected_p.strong_ratio, selected_p.hold_frames) != (
        default_p.burst_ratio,
        default_p.strong_ratio,
        default_p.hold_frames,
    ):
        md.extend(holdout_block(selected_p, "train-selected"))

    if args.overlap_sweep:
        md.append("## Overlap sensitivity (burst/strong/hold at default)")
        md.append("")
        md.append("| overlap | weighted train obj | 0_none | 3_high |")
        md.append("|--------:|---------------:|-------:|-------:|")
        base = PolicyParams(overlap_ratio=args.overlap)
        for ov in (0.30, 0.35, 0.40):
            p = PolicyParams(
                burst_ratio=base.burst_ratio,
                strong_ratio=base.strong_ratio,
                hold_frames=base.hold_frames,
                overlap_ratio=ov,
            )
            objs = []
            weighted_total = 0.0
            weight_total = 0.0
            per_values: dict[str, list[float]] = {}
            for ref, frames in train_data:
                m = summarize_policy(frames, replay_policy(frames, p))
                o = level_objective(ref.level, m)
                objs.append(o)
                per_values.setdefault(ref.level, []).append(o)
                w = LEVEL_WEIGHTS.get(ref.level, 1.0)
                weighted_total += w * o
                weight_total += w
            mean_o = weighted_total / weight_total if weight_total else 0.0
            per = {
                level: sum(values) / len(values)
                for level, values in per_values.items()
                if values
            }
            md.append(
                f"| {ov:.2f} | {mean_o:.1f} | {per.get('0_none', 0):.1f} | {per.get('3_high', 0):.1f} |"
            )
            rows_csv.append(
                {
                    "split": "train_overlap_sweep",
                    "burst": p.burst_ratio,
                    "strong": p.strong_ratio,
                    "hold": p.hold_frames,
                    "overlap": ov,
                    "score": mean_o,
                }
            )
        md.append("")

    out_md = out_dir / "SENSITIVITY_TABLE.md"
    out_md.write_text("\n".join(md))

    csv_path = out_dir / "sensitivity_grid.csv"
    if rows_csv:
        fields = sorted({k for row in rows_csv for k in row})
        with csv_path.open("w", newline="") as f:
            w = csv.DictWriter(f, fieldnames=fields)
            w.writeheader()
            w.writerows(rows_csv)

    print(f"Wrote {out_md}")
    print(f"Wrote {csv_path}")
    print(f"Train combos: {len(combo_scores)}  hold-out runs: {len(holdout)}")


if __name__ == "__main__":
    main()
