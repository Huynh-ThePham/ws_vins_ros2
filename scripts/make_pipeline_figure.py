#!/usr/bin/env python3
"""Render the GeoDF-Adaptive front-end pipeline figure for the AECE paper.

Matches the implementation in feature_tracker.cpp (trackImage → rejectGeoDynamic):
 - temporal KLT on left camera, then GeoDF-Adaptive, then setMask/detect, then stereo KLT
 - GeoDF internal flow: lift → 2D-F + Sampson → stereo triangulation + PnP-RANSAC 3D gate
   → scene-aware activation → temporal voting → guarded hard delete

Grayscale-safe SVG + PDF + PNG (300 dpi).
"""
import os
import matplotlib

matplotlib.use("Agg")
import matplotlib.pyplot as plt
from matplotlib.patches import FancyBboxPatch, FancyArrowPatch

plt.rcParams.update({
    "font.family": "DejaVu Sans",
    "font.size": 9,
    "svg.fonttype": "none",
})

OUT_DIR = "results/geodf_evaluation/figures"
os.makedirs(OUT_DIR, exist_ok=True)

C_PLAIN = "#ffffff"
C_EDGE = "#000000"
C_HILITE = "#d8d8d8"
C_CALLOUT = "#f4f4f4"
C_BACKEND = "#ececec"
C_IMU = "#f8f8f8"

fig, ax = plt.subplots(figsize=(7.4, 5.4))
ax.set_xlim(0, 100)
ax.set_ylim(0, 100)
ax.axis("off")


def box(x, y, w, h, text, fc=C_PLAIN, ec=C_EDGE, lw=1.4, fs=8.3,
        weight="normal", ls="-", round_pad=0.02):
    p = FancyBboxPatch(
        (x, y), w, h,
        boxstyle=f"round,pad={round_pad},rounding_size=2.0",
        linewidth=lw, edgecolor=ec, facecolor=fc, linestyle=ls,
        mutation_aspect=1.0, zorder=2,
    )
    ax.add_patch(p)
    ax.text(x + w / 2.0, y + h / 2.0, text, ha="center", va="center",
            fontsize=fs, fontweight=weight, zorder=3, linespacing=1.22)
    return (x, y, w, h)


def harrow(x0, x1, y, ls="-"):
    ax.add_patch(FancyArrowPatch((x0, y), (x1, y),
                 arrowstyle="-|>", mutation_scale=12, lw=1.3,
                 color=C_EDGE, zorder=1, ls=ls, shrinkA=0, shrinkB=0))


def varrow(x, y0, y1, ls="-"):
    ax.add_patch(FancyArrowPatch((x, y0), (x, y1),
                 arrowstyle="-|>", mutation_scale=12, lw=1.3,
                 color=C_EDGE, zorder=1, ls=ls, shrinkA=0, shrinkB=0))


# ---- Top row: front-end data flow (matches trackImage order) ----
row_y = 83
h = 12
labels = [
    "Stereo images\n+ IMU",
    "Temporal KLT\n(left t\u22121\u2192t)",
    "GeoDF-Adaptive\n(proposed)",
    "setMask +\ndetect new",
    "Stereo KLT\n(left\u2192right)",
    "VINS back-end\n(unchanged)",
]
margin = 0.8
gap = 2.2
n = len(labels)
bw = (100 - 2 * margin - (n - 1) * gap) / n
xs = [margin + i * (bw + gap) for i in range(n)]
for i, lab in enumerate(labels):
    if i == 2:
        box(xs[i], row_y, bw, h, lab, fc=C_HILITE, lw=2.2, weight="bold", fs=7.6)
    elif i == 5:
        box(xs[i], row_y, bw, h, lab, fc=C_BACKEND, ls=(0, (4, 2)), fs=7.6)
    elif i == 0:
        box(xs[i], row_y, bw, h, lab, fc=C_IMU, fs=7.6)
    else:
        box(xs[i], row_y, bw, h, lab, fs=7.6)

for i in range(n - 1):
    harrow(xs[i] + bw, xs[i + 1], row_y + h / 2.0)

# IMU feeds back-end (dashed bypass, not through GeoDF)
ax.add_patch(FancyArrowPatch((xs[0] + bw / 2, row_y - 0.3), (xs[5] + bw / 2, row_y - 0.3),
             arrowstyle="-|>", mutation_scale=10, lw=1.0, color="#666666",
             ls=(0, (3, 3)), connectionstyle="arc3,rad=-0.55", zorder=0))
ax.text(50, row_y - 4.5, "IMU preintegration (unchanged)", ha="center", va="center",
        fontsize=7.2, color="#444444", style="italic")

# ---- GeoDF callout panel ----
gx = xs[2] + bw / 2.0
panel_x, panel_y, panel_w, panel_h = 4, 4, 92, 66
box(panel_x, panel_y, panel_w, panel_h, "", fc=C_CALLOUT, ec="#555555",
    lw=1.1, ls=(0, (4, 2)), round_pad=0.35)
ax.text(panel_x + 2.5, panel_y + panel_h - 3.5,
        "GeoDF-Adaptive module  (front-end only; no IMU / no CNN)",
        ha="left", va="center", fontsize=9.0, fontweight="bold", zorder=4)

varrow(gx, row_y - 0.2, panel_y + panel_h + 0.2, ls=(0, (3, 2)))

steps = [
    ("(a)", "Undistort & lift tracks; map to VINS pseudo-pixels"),
    ("(b)", "Temporal 2D-F on left cam (RANSAC) + Sampson residual e_i\n"
            "(support / fallback; also gates 3D activation)"),
    ("(c)", "Stereo: KLT cur left\u2192right; triangulate prev stereo (L/R at t\u22121)"),
    ("(d)", "Primary gate: PnP-RANSAC rigid motion \u2192 3D reprojection r_i\n"
            "Dynamic candidate if r_i > \u03c4_3D  (else 2D dual-gate fallback)"),
    ("(e)", "Scene-aware ARM: EMA(outlier ratio) \u2265 \u03c1_on(auto) AND quality gate\n"
            "(\u03c1_on = clamp(floor\u00b71.8 + 0.10, 0.14, 0.40); hysteresis disarm)"),
    ("(f)", "Temporal voting: flag \u2265 k=3 consecutive frames (warmup 60)\n"
            "Ratio guard: reject \u2264 40% tracks, cap 5 deletions / frame"),
    ("(g)", "If ARMED: hard-delete voted tracks; else pass-through all features"),
]
inner_top = panel_y + panel_h - 8.5
inner_bot = panel_y + 2.5
sh = (inner_top - inner_bot) / len(steps)
sx = panel_x + 2.5
sw = panel_w - 5.0
tag_w = 7.5
for i, (tag, txt) in enumerate(steps):
    sy = inner_top - (i + 1) * sh + 0.6
    bh = sh - 1.4
    box(sx, sy, sw, bh, "", fc=C_PLAIN, ec=C_EDGE, lw=1.0, round_pad=0.15)
    cy = sy + bh / 2.0
    ax.text(sx + 1.8, cy, tag, ha="left", va="center",
            fontsize=8.2, fontweight="bold", zorder=4)
    ax.text(sx + tag_w + 1.0, cy, txt, ha="left", va="center",
            fontsize=7.5, zorder=4, linespacing=1.15)
    if i < len(steps) - 1:
        varrow(sx + sw / 2.0, sy - 0.15, sy - 1.25 + 0.15)

ax.text(50, 1.2,
        "Insertion point: after temporal KLT, before setMask() — back-end estimator unchanged.",
        ha="center", va="center", fontsize=7.5, style="italic")

plt.tight_layout(pad=0.35)
base = os.path.join(OUT_DIR, "pipeline_geodf_adaptive")
fig.savefig(base + ".svg", bbox_inches="tight")
fig.savefig(base + ".pdf", bbox_inches="tight")
fig.savefig(base + ".png", dpi=300, bbox_inches="tight")
print("wrote:", base + ".svg / .pdf / .png")
