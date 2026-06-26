#!/usr/bin/env python3
"""Grayscale bar chart: Paper #1 regression vs Paper #2 recovery on parking_lot."""
import os
import matplotlib

matplotlib.use("Agg")
import matplotlib.pyplot as plt
from matplotlib.patches import Patch

plt.rcParams.update({"font.family": "DejaVu Sans", "font.size": 10, "svg.fonttype": "none"})

OUT = "results/geodf_evaluation/figures"
os.makedirs(OUT, exist_ok=True)

# From N=5 baseline/adaptive (P1) and partial inertial trials (update after full N=5)
labels = ["PL/2_mid", "PL/3_high"]
baseline = [0.144, 0.119]
adaptive_p1 = [0.197, 0.172]
inertial_p2 = [0.144, 0.126]  # placeholder means; script reads live if metrics exist

import json, glob, statistics as st

def mean_ate(tag):
    vals = []
    for p in glob.glob(f"results/viode_repeat/{tag}/trial_*/eval/metrics.json"):
        try:
            vals.append(json.load(open(p))["ate_rmse_m"])
        except Exception:
            pass
    return st.mean(vals) if vals else None

b2 = mean_ate("parking_lot_2_mid_inertial")
b3 = mean_ate("parking_lot_3_high_inertial")
if b2 and b3:
    inertial_p2 = [b2, b3]

x = range(len(labels))
w = 0.25
fig, ax = plt.subplots(figsize=(6.5, 3.6))
ax.bar([i - w for i in x], baseline, width=w, color="#cccccc", edgecolor="black", label="baseline")
ax.bar(x, adaptive_p1, width=w, color="white", edgecolor="black", hatch="////", label="GeoDF-Adaptive (P1)")
ax.bar([i + w for i in x], inertial_p2, width=w, color="#4d4d4d", edgecolor="black", label="GeoDF-Inertial (P2)")
for xi, v in zip(x, adaptive_p1):
    ax.text(xi, v + 0.008, f"{v:.3f}", ha="center", fontsize=8)
for xi, v in zip([i + w for i in x], inertial_p2):
    ax.text(xi, v + 0.008, f"{v:.3f}", ha="center", fontsize=8, fontweight="bold")
ax.set_xticks(list(x))
ax.set_xticklabels(labels)
ax.set_ylabel("ATE RMSE (m)")
ax.set_title("Parking-lot recovery: feature-fit vs inertial epipolar (VIODE)")
ax.legend(loc="upper left", fontsize=8)
ax.spines["top"].set_visible(False)
ax.spines["right"].set_visible(False)
fig.savefig(f"{OUT}/parking_lot_recovery_gray.svg", bbox_inches="tight")
fig.savefig(f"{OUT}/parking_lot_recovery_gray.png", dpi=300, bbox_inches="tight")
plt.close()
print("wrote parking_lot_recovery_gray")
