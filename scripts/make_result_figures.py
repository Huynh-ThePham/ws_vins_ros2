#!/usr/bin/env python3
"""Render grayscale-safe result figures for the AECE paper.

Fig 2: VIODE N=5 ATE improvement (%) per environment-level (matches Table 2).
Fig 3: Dynamic-feature detection lift on VIODE GT masks (matches Table 4).

Grayscale-safe: improvement encoded by fill shade + hatch + signed value labels
(not by colour), so the figures stay readable when printed in black and white.
"""
import os
import matplotlib

matplotlib.use("Agg")
import matplotlib.pyplot as plt

plt.rcParams.update({
    "font.family": "DejaVu Sans",
    "font.size": 10,
    "svg.fonttype": "none",
})

OUT = "results/geodf_evaluation/figures"
os.makedirs(OUT, exist_ok=True)


def save(fig, name):
    base = os.path.join(OUT, name)
    fig.savefig(base + ".svg", bbox_inches="tight")
    fig.savefig(base + ".pdf", bbox_inches="tight")
    fig.savefig(base + ".png", dpi=300, bbox_inches="tight")
    plt.close(fig)
    print("wrote", base + ".{svg,pdf,png}")


# ---------- Figure 2: VIODE N=5 ATE improvement ----------
labels = ["CD/0", "CD/1", "CD/2", "CD/3",
          "CN/0", "CN/1", "CN/2", "CN/3",
          "PL/0", "PL/1", "PL/2", "PL/3"]
impr = [-9.7, -6.8, 8.1, 24.5, 41.3, -6.7, 8.5, 4.6, 12.1, 9.7, -36.4, -44.3]

fig, ax = plt.subplots(figsize=(7.2, 3.4))
x = range(len(labels))
for xi, v in zip(x, impr):
    if v >= 0:
        ax.bar(xi, v, color="#4d4d4d", edgecolor="black", linewidth=0.8, zorder=3)
    else:
        ax.bar(xi, v, color="white", edgecolor="black", linewidth=0.8,
               hatch="////", zorder=3)
    ax.text(xi, v + (1.6 if v >= 0 else -1.6),
            f"{v:+.1f}", ha="center",
            va="bottom" if v >= 0 else "top", fontsize=8.2)
ax.axhline(0, color="black", linewidth=1.0, zorder=2)
ax.axhspan(-3, 3, color="#dddddd", alpha=0.6, zorder=0)  # +/-3% decision band
ax.set_xticks(list(x))
ax.set_xticklabels(labels, rotation=0, fontsize=8.5)
ax.set_ylabel("ATE improvement (%)")
ax.set_ylim(-55, 55)
ax.set_title("VIODE N=5 ATE improvement: GeoDF-Adaptive vs baseline",
             fontsize=10.5)
# grayscale legend
from matplotlib.patches import Patch
ax.legend(handles=[
    Patch(facecolor="#4d4d4d", edgecolor="black", label="improved"),
    Patch(facecolor="white", edgecolor="black", hatch="////", label="regressed"),
    Patch(facecolor="#dddddd", edgecolor="none", label="+/-3% band"),
], loc="lower left", fontsize=8, framealpha=0.9, ncol=3)
ax.spines["top"].set_visible(False)
ax.spines["right"].set_visible(False)
save(fig, "viode_ate_delta_n5_gray")


# ---------- Figure 3: detection lift ----------
dl_labels = ["CD/1", "CD/2", "CD/3", "CN/2", "CN/3", "PL/2", "PL/3"]
lift = [31.72, 12.14, 8.33, 3.02, 2.89, 1.48, 1.42]

fig, ax = plt.subplots(figsize=(7.2, 3.4))
xx = range(len(dl_labels))
ax.bar(xx, lift, color="#808080", edgecolor="black", linewidth=0.8, zorder=3)
for xi, v in zip(xx, lift):
    ax.text(xi, v + 0.7, f"{v:.1f}x", ha="center", va="bottom", fontsize=8.5)
ax.axhline(1.0, color="black", linewidth=1.1, linestyle=(0, (5, 3)), zorder=2)
ax.text(len(dl_labels) - 1.1, 31.0, "- - -  lift = 1x (random sampling)",
        ha="right", va="top", fontsize=9, style="italic")
ax.set_xticks(list(xx))
ax.set_xticklabels(dl_labels, fontsize=8.5)
ax.set_ylabel("precision lift (x)")
ax.set_ylim(0, 36)
ax.set_title("Dynamic-feature detection lift on VIODE GT masks", fontsize=10.5)
ax.spines["top"].set_visible(False)
ax.spines["right"].set_visible(False)
save(fig, "viode_detection_lift_gray")
