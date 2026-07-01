#!/usr/bin/env python3
"""Qualitative trajectory-overlay figures (baseline vs proposed vs GT).

Reads the TUM files already saved by evaluate_trajectory.py (no re-run needed):
  results/<study>/<cell>_<method>/trial_<i>/eval/{est_tum.txt, gt_tum.txt}

Produces grayscale-safe XY overlays for representative cells (a strong win, a
neutral case, and a limitation case) so the paper can show trajectory behaviour,
not only aggregate ATE. Line style (not colour) distinguishes the methods.
"""
from __future__ import annotations

import argparse
from pathlib import Path

import matplotlib

matplotlib.use("Agg")
import matplotlib.pyplot as plt

plt.rcParams.update({"font.family": "DejaVu Sans", "font.size": 10, "svg.fonttype": "none"})

# Representative cells reported honestly: win / neutral / limitation.
DEFAULT_CELLS = [
    ("city_night_0_none", "CN/0 (strong win)"),
    ("city_day_3_high", "CD/3 (win, high dynamic)"),
    ("city_day_2_mid", "CD/2 (moderate)"),
    ("parking_lot_3_high", "PL/3 (limitation)"),
]


def load_xy(tum: Path) -> tuple[list[float], list[float]]:
    xs, ys = [], []
    if not tum.is_file():
        return xs, ys
    with tum.open() as f:
        for line in f:
            line = line.strip()
            if not line or line.startswith("#"):
                continue
            p = line.split()
            if len(p) >= 3:
                xs.append(float(p[1]))
                ys.append(float(p[2]))
    return xs, ys


def plot_cell(ax, root: Path, cell: str, title: str, trial: int) -> bool:
    base = root / f"{cell}_baseline" / f"trial_{trial}" / "eval"
    prop = root / f"{cell}_adaptive" / f"trial_{trial}" / "eval"
    gx, gy = load_xy(base / "gt_tum.txt")
    if not gx:
        gx, gy = load_xy(prop / "gt_tum.txt")
    bx, by = load_xy(base / "est_tum.txt")
    px, py = load_xy(prop / "est_tum.txt")
    if not gx and not bx and not px:
        ax.set_visible(False)
        return False
    if gx:
        ax.plot(gx, gy, color="black", linewidth=1.6, label="ground truth", zorder=1)
    if bx:
        ax.plot(bx, by, color="#888888", linewidth=1.2, linestyle=(0, (5, 3)),
                label="baseline", zorder=2)
    if px:
        ax.plot(px, py, color="#1a1a1a", linewidth=1.2, linestyle=(0, (1, 1)),
                label="proposed", zorder=3)
    ax.set_title(title, fontsize=9.5)
    ax.set_aspect("equal", adjustable="datalim")
    ax.set_xlabel("x [m]", fontsize=8)
    ax.set_ylabel("y [m]", fontsize=8)
    ax.tick_params(labelsize=7.5)
    ax.legend(fontsize=7.5, loc="best", framealpha=0.9)
    ax.spines["top"].set_visible(False)
    ax.spines["right"].set_visible(False)
    return True


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("--root", type=Path, default=Path("results/viode_repeat"))
    ap.add_argument("--trial", type=int, default=1)
    ap.add_argument("--out", type=Path, default=Path("results/geodf_evaluation/figures/viode_trajectories_gray"))
    ap.add_argument("--cells", nargs="*", default=None,
                    help="cell keys like city_day_3_high (title auto)")
    args = ap.parse_args()

    cells = [(c, c) for c in args.cells] if args.cells else DEFAULT_CELLS
    n = len(cells)
    ncol = 2
    nrow = (n + ncol - 1) // ncol
    fig, axes = plt.subplots(nrow, ncol, figsize=(7.2, 3.4 * nrow))
    axes = axes.flatten() if hasattr(axes, "flatten") else [axes]

    drawn = 0
    for ax, (cell, title) in zip(axes, cells):
        if plot_cell(ax, args.root, cell, title, args.trial):
            drawn += 1
    for ax in axes[len(cells):]:
        ax.set_visible(False)

    if drawn == 0:
        print(f"[traj] no TUM data under {args.root}; nothing drawn")
        return

    fig.tight_layout()
    args.out.parent.mkdir(parents=True, exist_ok=True)
    for ext in ("svg", "pdf", "png"):
        fig.savefig(f"{args.out}.{ext}", dpi=300, bbox_inches="tight")
    plt.close(fig)
    print(f"[traj] wrote {args.out}.{{svg,pdf,png}} ({drawn} cells)")


if __name__ == "__main__":
    main()
