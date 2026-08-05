#!/usr/bin/env python3
"""Generate high-quality Sem-GeoDF method figures for IEEE Access.

Produces PDF/PNG/SVG under paper/sem_geodf/figures/ without needing ablation
result trees (method diagrams only).

Usage:
  python3 scripts/make_sem_geodf_method_figures.py
  python3 scripts/make_sem_geodf_method_figures.py --out paper/sem_geodf/figures
"""

from __future__ import annotations

import argparse
from pathlib import Path

import matplotlib

matplotlib.use("Agg")
import matplotlib.pyplot as plt
from matplotlib.patches import Circle, FancyArrowPatch, FancyBboxPatch, Rectangle

# Print-friendly IEEE palette (no purple / cream clichés).
C = {
    "ink": "#1A2332",
    "muted": "#5A6577",
    "line": "#2F3B4C",
    "navy": "#1E3A5F",
    "teal": "#1F6F6A",
    "steel": "#3D5A80",
    "amber": "#9A7040",
    "green": "#2F6F4E",
    "coral": "#A84B3A",
    "fill_navy": "#E8EEF5",
    "fill_teal": "#E4F1EF",
    "fill_steel": "#E9EEF4",
    "fill_amber": "#F4EFE6",
    "fill_green": "#E7F0EA",
    "fill_coral": "#F5EBE8",
    "fill_gray": "#F2F4F6",
    "white": "#FFFFFF",
}


def _save(fig, figdir: Path, stem: str):
    figdir.mkdir(parents=True, exist_ok=True)
    for ext in ("pdf", "png", "svg"):
        kw = {"bbox_inches": "tight", "pad_inches": 0.08}
        if ext == "png":
            kw["dpi"] = 300
            kw["facecolor"] = "white"
        fig.savefig(figdir / f"{stem}.{ext}", **kw)
    plt.close(fig)
    print(f"wrote {figdir / stem}.*")


def _box(ax, x, y, w, h, title, body="", *, fc, ec, title_fs=9, body_fs=7.2, radius=0.08):
    p = FancyBboxPatch(
        (x, y),
        w,
        h,
        boxstyle=f"round,pad=0.012,rounding_size={radius}",
        linewidth=1.35,
        edgecolor=ec,
        facecolor=fc,
        mutation_aspect=0.6,
    )
    ax.add_patch(p)
    if body:
        ax.text(
            x + w / 2,
            y + h * 0.62,
            title,
            ha="center",
            va="center",
            fontsize=title_fs,
            fontweight="bold",
            color=C["ink"],
            linespacing=1.15,
        )
        ax.text(
            x + w / 2,
            y + h * 0.28,
            body,
            ha="center",
            va="center",
            fontsize=body_fs,
            color=C["muted"],
            linespacing=1.2,
        )
    else:
        ax.text(
            x + w / 2,
            y + h / 2,
            title,
            ha="center",
            va="center",
            fontsize=title_fs,
            fontweight="bold",
            color=C["ink"],
            linespacing=1.15,
        )
    return (x + w / 2, y + h / 2)


def _arrow(ax, p0, p1, *, color=None, lw=1.35, style="-|>", connectionstyle="arc3,rad=0"):
    color = color or C["line"]
    ax.add_patch(
        FancyArrowPatch(
            p0,
            p1,
            arrowstyle=style,
            mutation_scale=11,
            linewidth=lw,
            color=color,
            connectionstyle=connectionstyle,
            shrinkA=2,
            shrinkB=2,
        )
    )


def fig_system_overview(figdir: Path):
    """Full system overview — main paper figure."""
    fig, ax = plt.subplots(figsize=(11.2, 5.6))
    ax.set_xlim(0, 22)
    ax.set_ylim(0, 11)
    ax.axis("off")

    # Title strip
    ax.add_patch(Rectangle((0.3, 10.15), 21.4, 0.65, facecolor=C["navy"], edgecolor="none"))
    ax.text(
        11.0,
        10.47,
        "Sem-GeoDF  ·  Adaptive Gated Union for Stereo–Inertial VIO",
        ha="center",
        va="center",
        fontsize=12,
        fontweight="bold",
        color="white",
    )

    # Layer labels
    ax.text(0.45, 8.85, "Sensors", fontsize=8, fontweight="bold", color=C["navy"], rotation=90, va="center")
    ax.text(0.45, 6.35, "Experts", fontsize=8, fontweight="bold", color=C["teal"], rotation=90, va="center")
    ax.text(0.45, 3.85, "Policy", fontsize=8, fontweight="bold", color=C["amber"], rotation=90, va="center")
    ax.text(0.45, 1.55, "Fusion", fontsize=8, fontweight="bold", color=C["coral"], rotation=90, va="center")

    # Sensors
    _box(ax, 1.2, 8.15, 3.2, 1.35, "Stereo cameras", "cam0 / cam1 tracks", fc=C["fill_navy"], ec=C["navy"])
    _box(ax, 4.7, 8.15, 2.6, 1.35, "IMU", "preintegration", fc=C["fill_navy"], ec=C["navy"])
    _box(
        ax,
        7.7,
        8.15,
        4.0,
        1.35,
        "YOLO dynamic mask",
        "async · non-blocking · age ≤ 150 ms",
        fc=C["fill_teal"],
        ec=C["teal"],
    )

    # Expert branches
    _box(
        ax,
        1.2,
        5.55,
        5.8,
        2.15,
        "Semantic branch",
        "pixel-ratio EMA  ·  vote confirm  ·  soft / hard mask",
        fc=C["fill_teal"],
        ec=C["teal"],
        title_fs=10,
        body_fs=7.5,
    )
    _box(
        ax,
        7.6,
        5.55,
        5.8,
        2.15,
        "GeoDF-Adaptive branch",
        "F-RANSAC ∧ Sampson²  ·  auto-ρ  ·  vote confirm",
        fc=C["fill_steel"],
        ec=C["steel"],
        title_fs=10,
        body_fs=7.5,
    )
    _box(
        ax,
        14.0,
        5.55,
        7.0,
        2.15,
        "Bidirectional overlap",
        r"$o=\max(|G{\cap}S|/|G|,\,|S{\cap}G|/|S|)$  on raw GeoDF pool",
        fc=C["fill_amber"],
        ec=C["amber"],
        title_fs=10,
        body_fs=7.5,
    )

    # Policy
    _box(
        ax,
        1.2,
        3.05,
        19.8,
        2.05,
        "Three-state online policy  (label-free)",
        "0  static-safe   →   1  dynamic-assist   →   2  strong-dynamic"
        "     ·     triggers: burst / strong EMA / overlap + hold timer"
        "     ·     never reads GT / ATE / VIODE level",
        fc=C["fill_amber"],
        ec=C["amber"],
        title_fs=10,
        body_fs=7.6,
    )

    # Fusion + backend
    _box(
        ax,
        1.2,
        0.55,
        6.2,
        2.05,
        "Gated union (OR)",
        "sem ∪ geo  under shared\nratio + min-feature guard",
        fc=C["fill_coral"],
        ec=C["coral"],
        title_fs=10,
    )
    _box(
        ax,
        8.0,
        0.55,
        6.2,
        2.05,
        "Residual weights $w_i$",
        r"fused risk $r_i$ → $\tilde w_i = 1 - r_i$ → $\sqrt{w_i}$ in Ceres",
        fc=C["fill_green"],
        ec=C["green"],
        title_fs=10,
    )
    _box(
        ax,
        14.8,
        0.55,
        6.2,
        2.05,
        "VINS-Fusion backend",
        "stereo–inertial sliding window\noptimization",
        fc=C["fill_navy"],
        ec=C["navy"],
        title_fs=10,
    )

    # Arrows sensors → experts
    _arrow(ax, (2.8, 8.15), (2.8, 7.75), color=C["teal"])
    _arrow(ax, (9.7, 8.15), (4.1, 7.75), color=C["teal"], connectionstyle="arc3,rad=0.12")
    _arrow(ax, (2.8, 8.15), (9.5, 7.75), color=C["steel"], connectionstyle="arc3,rad=-0.08")
    _arrow(ax, (6.0, 8.15), (10.5, 7.75), color=C["steel"])

    # Experts → overlap / policy
    _arrow(ax, (4.1, 5.55), (8.0, 5.1), color=C["amber"], connectionstyle="arc3,rad=0.05")
    _arrow(ax, (10.5, 5.55), (15.5, 5.1), color=C["amber"])
    _arrow(ax, (17.5, 5.55), (12.0, 5.1), color=C["amber"], connectionstyle="arc3,rad=-0.05")
    _arrow(ax, (11.0, 5.55), (11.0, 5.15), color=C["amber"])

    # Policy → fusion
    _arrow(ax, (4.3, 3.05), (4.3, 2.65), color=C["coral"])
    _arrow(ax, (11.1, 3.05), (11.1, 2.65), color=C["green"])
    _arrow(ax, (17.9, 3.05), (17.9, 2.65), color=C["navy"])

    # Fusion chain
    _arrow(ax, (7.4, 1.55), (8.0, 1.55), color=C["line"], lw=1.6)
    _arrow(ax, (14.2, 1.55), (14.8, 1.55), color=C["line"], lw=1.6)

    # Footer note
    ax.text(
        11.0,
        0.18,
        "Fail-open semantics  ·  shared reject budget  ·  train/hold-out threshold freeze",
        ha="center",
        va="center",
        fontsize=7.5,
        color=C["muted"],
        style="italic",
    )

    _save(fig, figdir, "system_overview_sem_geodf")


def fig_pipeline_upgraded(figdir: Path):
    """Compact single-column pipeline (replaces the old sparse boxes)."""
    fig, ax = plt.subplots(figsize=(7.4, 4.8))
    ax.set_xlim(0, 14)
    ax.set_ylim(0, 9.2)
    ax.axis("off")

    ax.text(7, 8.85, "Sem-GeoDF front-end (per stereo frame)", ha="center", fontsize=11, fontweight="bold", color=C["ink"])

    _box(ax, 0.4, 7.15, 3.0, 1.25, "cam0 + IMU", "tracks + preint.", fc=C["fill_navy"], ec=C["navy"])
    _box(ax, 3.8, 7.15, 3.2, 1.25, "YOLO mask", "async · age check", fc=C["fill_teal"], ec=C["teal"])
    _box(ax, 7.5, 7.15, 2.9, 1.25, "Optical flow", "prev → cur tracks", fc=C["fill_steel"], ec=C["steel"])
    _box(ax, 10.8, 7.15, 2.7, 1.25, "Fail-open", "stale → geo only", fc=C["fill_gray"], ec=C["muted"])

    _box(ax, 0.8, 4.55, 5.5, 2.05, "Semantic expert", "EMA scene gate · vote · soft/hard", fc=C["fill_teal"], ec=C["teal"], title_fs=10)
    _box(ax, 7.5, 4.55, 5.7, 2.05, "GeoDF-Adaptive", "RANSAC∧Sampson · auto-ρ · vote", fc=C["fill_steel"], ec=C["steel"], title_fs=10)

    _box(ax, 2.2, 2.55, 9.4, 1.5, "Online policy  $s\\in\\{0,1,2\\}$", "burst / strong / overlap + hold   →   static-safe / assist / strong", fc=C["fill_amber"], ec=C["amber"], title_fs=9.5)

    _box(ax, 0.6, 0.45, 4.0, 1.55, "OR union + guard", "shared reject budget", fc=C["fill_coral"], ec=C["coral"])
    _box(ax, 5.1, 0.45, 4.0, 1.55, "Weights $w_i$", r"$\sqrt{w_i}$ residuals", fc=C["fill_green"], ec=C["green"])
    _box(ax, 9.6, 0.45, 3.8, 1.55, "VINS backend", "sliding window", fc=C["fill_navy"], ec=C["navy"])

    _arrow(ax, (1.9, 7.15), (2.5, 6.65), color=C["teal"])
    _arrow(ax, (5.4, 7.15), (3.5, 6.65), color=C["teal"])
    _arrow(ax, (8.9, 7.15), (10.0, 6.65), color=C["steel"])
    _arrow(ax, (3.5, 4.55), (5.5, 4.1), color=C["amber"])
    _arrow(ax, (10.3, 4.55), (8.5, 4.1), color=C["amber"])
    _arrow(ax, (6.9, 2.55), (2.6, 2.05), color=C["coral"])
    _arrow(ax, (6.9, 2.55), (7.1, 2.05), color=C["green"])
    _arrow(ax, (6.9, 2.55), (11.5, 2.05), color=C["navy"])
    _arrow(ax, (4.6, 1.2), (5.1, 1.2), color=C["line"])
    _arrow(ax, (9.1, 1.2), (9.6, 1.2), color=C["line"])

    _save(fig, figdir, "pipeline_sem_geodf")


def fig_policy_fsm(figdir: Path):
    fig, ax = plt.subplots(figsize=(8.6, 4.4))
    ax.set_xlim(0, 16)
    ax.set_ylim(0, 8)
    ax.axis("off")
    ax.text(8, 7.55, "Three-state adaptive policy (online, label-free)", ha="center", fontsize=11, fontweight="bold", color=C["ink"])

    states = [
        (1.0, 3.6, "0  static-safe", "soft mask if scene on\nhard cull OFF", C["fill_green"], C["green"]),
        (6.0, 3.6, "1  dynamic-assist", "hold soft mask\nhard cull if scene on", C["fill_amber"], C["amber"]),
        (11.0, 3.6, "2  strong-dynamic", "full OR armed\nshared reject budget", C["fill_coral"], C["coral"]),
    ]
    for x, y, t, b, fc, ec in states:
        _box(ax, x, y, 4.0, 2.35, t, b, fc=fc, ec=ec, title_fs=10, body_fs=7.5)

    # Escalation arrows
    _arrow(ax, (5.0, 4.9), (6.0, 4.9), color=C["amber"], lw=1.6)
    ax.text(5.5, 5.25, "burst / overlap\n/ hold", ha="center", fontsize=6.8, color=C["muted"])
    _arrow(ax, (10.0, 4.9), (11.0, 4.9), color=C["coral"], lw=1.6)
    ax.text(10.5, 5.25, "strong EMA /\nassist+geo", ha="center", fontsize=6.8, color=C["muted"])

    # De-escalation
    _arrow(ax, (8.0, 3.55), (5.0, 3.2), color=C["green"], connectionstyle="arc3,rad=-0.25", lw=1.2)
    _arrow(ax, (13.0, 3.55), (10.0, 3.2), color=C["amber"], connectionstyle="arc3,rad=-0.25", lw=1.2)
    ax.text(6.3, 2.85, "hold expires", fontsize=6.5, color=C["muted"], ha="center")

    # Trigger panel
    _box(
        ax,
        1.2,
        0.45,
        13.6,
        1.9,
        "Online triggers only",
        r"burst: $r_t \geq \tau_b$   ·   strong: $\bar{r}_t \geq \tau_s$   ·   overlap: $\bar{o}_t \geq \tau_o$ on raw GeoDF pool"
        "\nno VIODE level · no GT · no ATE/RPE · no hold-out metrics",
        fc=C["fill_gray"],
        ec=C["muted"],
        title_fs=9,
        body_fs=7.4,
    )

    _save(fig, figdir, "policy_fsm_sem_geodf")


def fig_fusion_compare(figdir: Path):
    fig, ax = plt.subplots(figsize=(9.2, 4.2))
    ax.set_xlim(0, 18)
    ax.set_ylim(0, 8)
    ax.axis("off")
    ax.text(9, 7.55, "How fusion styles differ", ha="center", fontsize=11, fontweight="bold", color=C["ink"])

    panels = [
        (0.5, "AND", "reject only if\nboth agree", "misses movers\nwhen one silent", C["fill_gray"], C["muted"]),
        (6.2, "Sequential", "Geo then Sem\n(no shared budget)", "can over-cull\nor under-cull", C["fill_steel"], C["steel"]),
        (11.9, "Sem-GeoDF OR", "policy-gated union\n+ shared ratio guard", "static-safe off\nuntil evidence", C["fill_coral"], C["coral"]),
    ]
    for x, title, mid, bot, fc, ec in panels:
        _box(ax, x, 1.2, 5.2, 5.6, "", "", fc=fc, ec=ec, radius=0.12)
        ax.text(x + 2.6, 5.95, title, ha="center", fontsize=11, fontweight="bold", color=C["ink"])
        # mini Venn-ish
        ax.add_patch(Circle((x + 1.9, 4.35), 0.85, fill=False, ec=C["teal"], lw=1.8))
        ax.add_patch(Circle((x + 3.3, 4.35), 0.85, fill=False, ec=C["steel"], lw=1.8))
        ax.text(x + 1.55, 4.35, "S", fontsize=8, color=C["teal"], ha="center", va="center", fontweight="bold")
        ax.text(x + 3.65, 4.35, "G", fontsize=8, color=C["steel"], ha="center", va="center", fontweight="bold")
        ax.text(x + 2.6, 3.15, mid, ha="center", va="center", fontsize=7.8, color=C["ink"], linespacing=1.25)
        ax.text(x + 2.6, 1.85, bot, ha="center", va="center", fontsize=7.2, color=C["muted"], linespacing=1.25)

    # Highlight ours
    ax.add_patch(
        FancyBboxPatch(
            (11.75, 1.05),
            5.5,
            5.9,
            boxstyle="round,pad=0.01,rounding_size=0.12",
            fill=False,
            ec=C["coral"],
            lw=2.2,
            linestyle="-",
        )
    )
    ax.text(14.5, 0.55, "this paper", ha="center", fontsize=8, fontweight="bold", color=C["coral"])

    _save(fig, figdir, "fusion_compare_sem_geodf")


def fig_weight_flow(figdir: Path):
    fig, ax = plt.subplots(figsize=(9.0, 3.8))
    ax.set_xlim(0, 18)
    ax.set_ylim(0, 7)
    ax.axis("off")
    ax.text(9, 6.55, "Adaptive residual weighting for suspicious survivors", ha="center", fontsize=11, fontweight="bold", color=C["ink"])

    _box(ax, 0.4, 3.5, 3.5, 2.2, "Online evidence", "sem / geo hits\nSampson excess\nscene + overlap", fc=C["fill_teal"], ec=C["teal"])
    # Keep this figure, the EN/VI equations and sem_geodf_risk.h in lockstep:
    # fused risk r = 1 - prod(1 - rho); target weight is its INVERSE, 1 - r.
    _box(ax, 4.6, 3.5, 3.6, 2.2, r"Fused risk $r_i$", r"$1-\prod(1-\rho^b_i)$", fc=C["fill_amber"], ec=C["amber"])
    _box(ax, 9.0, 3.5, 3.8, 2.2, r"Target $\tilde w_i = 1 - r_i$", r"$\prod(1-\rho^b_i)$\n+ caps + $w_{\min}$", fc=C["fill_coral"], ec=C["coral"])
    _box(ax, 13.5, 3.5, 4.0, 2.2, r"Ceres $\sqrt{w_i}$", "residual + Jacobian\n(incl. time offset)", fc=C["fill_navy"], ec=C["navy"])

    for x0, x1 in ((3.9, 4.6), (8.2, 9.0), (12.8, 13.5)):
        _arrow(ax, (x0, 4.6), (x1, 4.6), color=C["line"], lw=1.5)

    _box(
        ax,
        1.5,
        0.5,
        15.0,
        2.2,
        "Why weights (not only hard cull)",
        "ratio / min-feature guard may keep suspicious tracks alive  ·  "
        "per-id recovery recovers wi→1 when evidence fades  ·  "
        "no GT / labels in the weight path",
        fc=C["fill_gray"],
        ec=C["muted"],
        title_fs=9,
        body_fs=7.4,
    )

    _save(fig, figdir, "backend_weight_flow")


def fig_eval_protocol(figdir: Path):
    fig, ax = plt.subplots(figsize=(9.2, 4.0))
    ax.set_xlim(0, 18)
    ax.set_ylim(0, 7.5)
    ax.axis("off")
    ax.text(9, 7.05, "Evaluation hygiene (leak-free protocol)", ha="center", fontsize=11, fontweight="bold", color=C["ink"])

    steps = [
        (0.5, "1 · Train", "VIODE city_day\nlevels 0–3", "select thresholds\non policy proxy\n(not ATE)", C["fill_teal"], C["teal"]),
        (5.0, "2 · Freeze", "selected_params.yaml", "no retune after\nhold-out ATE", C["fill_amber"], C["amber"]),
        (9.5, "3 · Hold-out", "city_night +\nparking_lot", "report ATE only\n$N{=}3$, fair 1.0×", C["fill_coral"], C["coral"]),
        (14.0, "4 · Static", "EuRoC MH01–05", "generalization\ncheck", C["fill_navy"], C["navy"]),
    ]
    for x, title, mid, bot, fc, ec in steps:
        _box(ax, x, 2.4, 3.6, 4.0, title, f"{mid}\n\n{bot}", fc=fc, ec=ec, title_fs=10, body_fs=7.2)

    for x0, x1 in ((4.1, 5.0), (8.6, 9.5), (13.1, 14.0)):
        _arrow(ax, (x0, 4.4), (x1, 4.4), color=C["line"], lw=1.6)

    ax.text(
        9,
        1.2,
        "Oracle VIODE-level override disabled by default  ·  GT used only for offline EVO scoring",
        ha="center",
        fontsize=7.8,
        color=C["muted"],
        style="italic",
    )
    ax.text(
        9,
        0.55,
        "Four-method matrix: Baseline | GeoDF-Adaptive | SAD-Sem | Sem-GeoDF",
        ha="center",
        fontsize=8,
        color=C["ink"],
        fontweight="bold",
    )

    _save(fig, figdir, "eval_protocol_sem_geodf")


def fig_overlap_diagram(figdir: Path):
    fig, ax = plt.subplots(figsize=(7.6, 3.6))
    ax.set_xlim(0, 14)
    ax.set_ylim(0, 6.5)
    ax.axis("off")
    ax.text(7, 6.1, "Bidirectional overlap on raw GeoDF candidates", ha="center", fontsize=11, fontweight="bold", color=C["ink"])

    # Two sets
    ax.add_patch(Circle((4.2, 3.2), 1.7, facecolor=C["fill_teal"], edgecolor=C["teal"], lw=1.8, alpha=0.95))
    ax.add_patch(Circle((6.3, 3.2), 1.7, facecolor=C["fill_steel"], edgecolor=C["steel"], lw=1.8, alpha=0.55))
    ax.text(3.4, 3.2, "S\nsem-raw", ha="center", va="center", fontsize=9, fontweight="bold", color=C["teal"])
    ax.text(7.1, 3.2, "G\ngeo-raw", ha="center", va="center", fontsize=9, fontweight="bold", color=C["steel"])
    ax.text(5.25, 3.2, r"$\cap$", ha="center", va="center", fontsize=12, color=C["ink"])

    _box(
        ax,
        9.0,
        1.5,
        4.5,
        3.6,
        r"$o=\max(\cdot)$",
        r"$|G{\cap}S|/|G|$"
        "\n"
        r"$|S{\cap}G|/|S|$"
        "\n\n"
        "EMA → policy trigger\n(keeps uneven sets alive)",
        fc=C["fill_amber"],
        ec=C["amber"],
        title_fs=10,
        body_fs=7.5,
    )
    _arrow(ax, (7.9, 3.2), (9.0, 3.2), color=C["amber"], lw=1.5)

    ax.text(
        5.25,
        0.55,
        "Fallback: if raw pool empty → use GeoDF confirmed",
        ha="center",
        fontsize=7.5,
        color=C["muted"],
        style="italic",
    )

    _save(fig, figdir, "overlap_bidirectional")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--out", type=Path, default=Path("paper/sem_geodf/figures"))
    args = ap.parse_args()
    figdir = args.out.resolve()

    fig_system_overview(figdir)
    fig_pipeline_upgraded(figdir)
    fig_policy_fsm(figdir)
    fig_fusion_compare(figdir)
    fig_weight_flow(figdir)
    fig_eval_protocol(figdir)
    fig_overlap_diagram(figdir)
    print("done:", figdir)


if __name__ == "__main__":
    main()
