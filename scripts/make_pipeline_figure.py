#!/usr/bin/env python3
"""Render the GeoDF-Adaptive front-end pipeline figure for the AECE paper.

Grayscale-safe vector/raster block diagram:
 - top row: 5-stage stereo-inertial front-end flow (GeoDF-Adaptive highlighted)
 - callout: internal (a)-(f) steps of the GeoDF-Adaptive module
Outputs SVG + PDF (vector) and PNG (300 dpi) into results/geodf_evaluation/figures/.
"""
import os
import matplotlib

matplotlib.use("Agg")
import matplotlib.pyplot as plt
from matplotlib.patches import FancyBboxPatch, FancyArrowPatch

plt.rcParams.update({
    "font.family": "DejaVu Sans",
    "font.size": 9,
    "svg.fonttype": "none",  # keep text as text (editable, crisp in grayscale)
})

OUT_DIR = "results/geodf_evaluation/figures"
os.makedirs(OUT_DIR, exist_ok=True)

# Grayscale palette (prints cleanly in B/W)
C_PLAIN = "#ffffff"
C_EDGE = "#000000"
C_HILITE = "#cfcfcf"   # GeoDF-Adaptive highlight fill
C_CALLOUT = "#f2f2f2"  # internal-steps panel fill
C_BACKEND = "#e6e6e6"  # unchanged back-end (lighter, dashed)

fig, ax = plt.subplots(figsize=(7.2, 4.9))
ax.set_xlim(0, 100)
ax.set_ylim(0, 100)
ax.axis("off")


def box(x, y, w, h, text, fc=C_PLAIN, ec=C_EDGE, lw=1.4, fs=9,
        weight="normal", ls="-", round_pad=0.02):
    p = FancyBboxPatch(
        (x, y), w, h,
        boxstyle=f"round,pad={round_pad},rounding_size=2.2",
        linewidth=lw, edgecolor=ec, facecolor=fc, linestyle=ls,
        mutation_aspect=1.0, zorder=2,
    )
    ax.add_patch(p)
    ax.text(x + w / 2.0, y + h / 2.0, text, ha="center", va="center",
            fontsize=fs, fontweight=weight, zorder=3, linespacing=1.25)
    return (x, y, w, h)


def harrow(x0, x1, y):
    ax.add_patch(FancyArrowPatch((x0, y), (x1, y),
                 arrowstyle="-|>", mutation_scale=13, lw=1.4,
                 color=C_EDGE, zorder=1, shrinkA=0, shrinkB=0))


def varrow(x, y0, y1, ls="-"):
    ax.add_patch(FancyArrowPatch((x, y0), (x, y1),
                 arrowstyle="-|>", mutation_scale=13, lw=1.4,
                 color=C_EDGE, zorder=1, ls=ls, shrinkA=0, shrinkB=0))


# ---- Top row: main front-end flow (5 stages) ----
row_y = 81
h = 13
labels = [
    "Stereo frames\n(t-1, t) + IMU",
    "KLT tracking\n(fwd-bwd)",
    "GeoDF-Adaptive\n(proposed)",
    "Feature masking\n& detection",
    "Back-end\n(unchanged)",
]
margin = 1.0
gap = 3.0
bw = (100 - 2 * margin - 4 * gap) / 5.0  # equal-width boxes
xs = [margin + i * (bw + gap) for i in range(5)]
for i, lab in enumerate(labels):
    if i == 2:
        box(xs[i], row_y, bw, h, lab, fc=C_HILITE, lw=2.4, weight="bold", fs=8.2)
    elif i == 4:
        box(xs[i], row_y, bw, h, lab, fc=C_BACKEND, ls=(0, (4, 2)), fs=8.2)
    else:
        box(xs[i], row_y, bw, h, lab, fs=8.2)

for i in range(4):
    harrow(xs[i] + bw, xs[i + 1], row_y + h / 2.0)

# ---- Callout: zoom into GeoDF-Adaptive internal steps ----
gx = xs[2] + bw / 2.0
panel_x, panel_y, panel_w, panel_h = 6, 5, 88, 63
box(panel_x, panel_y, panel_w, panel_h, "", fc=C_CALLOUT, ec="#555555",
    lw=1.2, ls=(0, (4, 2)), round_pad=0.4)
ax.text(panel_x + 3.0, panel_y + panel_h - 4.0,
        "GeoDF-Adaptive  (geometry-only, training-free, front-end module)",
        ha="left", va="center", fontsize=9.3, fontweight="bold", zorder=4)

# dashed connector from highlighted top box down into the panel
varrow(gx, row_y - 0.2, panel_y + panel_h + 0.2, ls=(0, (3, 2)))

steps = [
    ("(a)", "Lift tracks to the normalized plane and map to pseudo-pixels"),
    ("(b)", "Estimate temporal fundamental matrix F (RANSAC);\n"
            "score the Sampson residual  e_i"),
    ("(c)", "Dual gate: dynamic candidate iff (RANSAC outlier)\n"
            "AND  (e_i > \u03c4)"),
    ("(d)", "Scene-aware activation: arm only when EMA of outlier ratio\n"
            "\u2265 \u03c1_on = clamp(floor\u00b71.8 + 0.05, 0.10, 0.40)"),
    ("(e)", "Track-level temporal voting (k = 2, 30-frame warm-up)\n"
            "+ ratio guard \u2264 40% of features per frame"),
    ("(f)", "Hard-delete confirmed dynamic tracks; keep the rest"),
]
n = len(steps)
inner_top = panel_y + panel_h - 9.0
inner_bot = panel_y + 3.0
sh = (inner_top - inner_bot) / n
sx = panel_x + 3.5
sw = panel_w - 7.0
for i, (tag, txt) in enumerate(steps):
    sy = inner_top - (i + 1) * sh + 1.0
    box(sx, sy, sw, sh - 2.0, "", fc=C_PLAIN, ec=C_EDGE, lw=1.1, round_pad=0.2)
    cy = sy + (sh - 2.0) / 2.0
    ax.text(sx + 2.5, cy, tag, ha="left", va="center",
            fontsize=8.8, fontweight="bold", zorder=4)
    ax.text(sx + 9.5, cy, txt, ha="left", va="center",
            fontsize=8.0, zorder=4, linespacing=1.2)
    if i < n - 1:
        varrow(sx + sw / 2.0, sy - 0.2, sy - 2.0 + 0.2)

ax.text(50, 1.5,
        "Only the highlighted module is added; the stereo-inertial back-end is "
        "unmodified.",
        ha="center", va="center", fontsize=8, style="italic")

plt.tight_layout(pad=0.4)
base = os.path.join(OUT_DIR, "pipeline_geodf_adaptive")
fig.savefig(base + ".svg", bbox_inches="tight")
fig.savefig(base + ".pdf", bbox_inches="tight")
fig.savefig(base + ".png", dpi=300, bbox_inches="tight")
print("wrote:", base + ".svg / .pdf / .png")
