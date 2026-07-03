#!/usr/bin/env python3
"""Render grayscale-safe result figures for the paper, driven by run data.

Fig 2: VIODE ATE improvement (%) per environment-level (matches Table 2).
Fig 3: Dynamic-feature detection lift on VIODE GT masks (matches Table 4).

Data sources (with graceful fallback to last-known frozen values so the script
never fails if a study has not been run yet):
  Fig 2 <- results/geodf_evaluation/stats_tests.json  (or paper_results_n5.json)
  Fig 3 <- results/viode_detection/*/detection_eval.json  (or detection JSON)

Grayscale-safe: improvement encoded by fill shade + hatch + signed value labels
(not by colour), so the figures stay readable when printed in black and white.
"""
import json
import os

import matplotlib

matplotlib.use("Agg")
import matplotlib.pyplot as plt
from matplotlib.patches import Patch

plt.rcParams.update({
    "font.family": "DejaVu Sans",
    "font.size": 10,
    "svg.fonttype": "none",
})

OUT = "results/geodf_evaluation/figures"
EVAL = "results/geodf_evaluation"
os.makedirs(OUT, exist_ok=True)

ENVS = ["city_day", "city_night", "parking_lot"]
LEVELS = ["0_none", "1_low", "2_mid", "3_high"]
ENV_TAG = {"city_day": "CD", "city_night": "CN", "parking_lot": "PL"}

# Last-known frozen values (fallback only; real runs overwrite these).
FALLBACK_IMPR = {
    "city_day/0_none": -9.7, "city_day/1_low": -6.8, "city_day/2_mid": 8.1, "city_day/3_high": 24.5,
    "city_night/0_none": 41.3, "city_night/1_low": -6.7, "city_night/2_mid": 8.5, "city_night/3_high": 4.6,
    "parking_lot/0_none": 12.1, "parking_lot/1_low": 9.7, "parking_lot/2_mid": -36.4, "parking_lot/3_high": -44.3,
}
FALLBACK_LIFT = {
    "city_day/1_low": 31.72, "city_day/2_mid": 12.14, "city_day/3_high": 8.33,
    "city_night/2_mid": 3.02, "city_night/3_high": 2.89,
    "parking_lot/2_mid": 1.48, "parking_lot/3_high": 1.42,
}


def _load_json(path):
    try:
        return json.load(open(path))
    except Exception:
        return None


def save(fig, name):
    base = os.path.join(OUT, name)
    fig.savefig(base + ".svg", bbox_inches="tight")
    fig.savefig(base + ".pdf", bbox_inches="tight")
    fig.savefig(base + ".png", dpi=300, bbox_inches="tight")
    plt.close(fig)
    print("wrote", base + ".{svg,pdf,png}")


def load_improvements():
    """Return {env/level: improvement_pct} from stats or paper JSON, else fallback."""
    src = _load_json(os.path.join(EVAL, "stats_tests.json"))
    if src and src.get("viode"):
        out = {}
        for k, v in src["viode"].items():
            imp = v.get("improvement_pct")
            if isinstance(imp, (int, float)):
                out[k] = imp
        if out:
            print("[fig2] using stats_tests.json")
            return out, "N=5 (measured)"
    src = _load_json(os.path.join(EVAL, "paper_results_n5.json"))
    if src and src.get("viode"):
        out = {}
        for k, v in src["viode"].items():
            imp = v.get("improvement_pct")
            if isinstance(imp, (int, float)):
                out[k] = imp
        if out:
            print("[fig2] using paper_results_n5.json")
            return out, "N=5 (measured)"
    print("[fig2] WARNING: no run data found, using frozen fallback values")
    return dict(FALLBACK_IMPR), "N=5 (frozen fallback)"


def load_lifts():
    """Return {env/level: precision_lift} from detection eval JSONs, else fallback."""
    out = {}
    det_root = "results/viode_detection"
    if os.path.isdir(det_root):
        for env in ENVS:
            for level in LEVELS:
                p = os.path.join(det_root, f"{env}_{level}_adaptive_dump", "detection_eval.json")
                if not os.path.isfile(p):
                    p = os.path.join(det_root, f"{env}_{level}_geodf_dump", "detection_eval.json")
                d = _load_json(p)
                if d and isinstance(d.get("precision_lift"), (int, float)):
                    out[f"{env}/{level}"] = d["precision_lift"]
    if out:
        print(f"[fig3] using {len(out)} detection_eval.json files")
        return out
    print("[fig3] WARNING: no detection data found, using frozen fallback values")
    return dict(FALLBACK_LIFT)


# ---------- Figure 2: VIODE ATE improvement ----------
impr_map, tag = load_improvements()
ordered = [f"{e}/{l}" for e in ENVS for l in LEVELS]
labels = [f"{ENV_TAG[e]}/{l.split('_')[0]}" for e in ENVS for l in LEVELS]
impr = [impr_map.get(k, float("nan")) for k in ordered]

fig, ax = plt.subplots(figsize=(7.2, 3.4))
x = range(len(labels))
for xi, v in zip(x, impr):
    if v != v:  # NaN -> no data
        continue
    if v >= 0:
        ax.bar(xi, v, color="#4d4d4d", edgecolor="black", linewidth=0.8, zorder=3)
    else:
        ax.bar(xi, v, color="white", edgecolor="black", linewidth=0.8, hatch="////", zorder=3)
    ax.text(xi, v + (1.6 if v >= 0 else -1.6), f"{v:+.1f}", ha="center",
            va="bottom" if v >= 0 else "top", fontsize=8.2)
ax.axhline(0, color="black", linewidth=1.0, zorder=2)
ax.axhspan(-3, 3, color="#dddddd", alpha=0.6, zorder=0)
ax.set_xticks(list(x))
ax.set_xticklabels(labels, rotation=0, fontsize=8.5)
ax.set_ylabel("ATE improvement (%)")
finite = [v for v in impr if v == v]
ylim = max(55, (max(abs(min(finite)), abs(max(finite))) + 10)) if finite else 55
ax.set_ylim(-ylim, ylim)
ax.set_title(f"VIODE ATE improvement: GeoDF-Adaptive vs baseline [{tag}]", fontsize=10.5)
ax.legend(handles=[
    Patch(facecolor="#4d4d4d", edgecolor="black", label="improved"),
    Patch(facecolor="white", edgecolor="black", hatch="////", label="regressed"),
    Patch(facecolor="#dddddd", edgecolor="none", label="+/-3% band"),
], loc="lower left", fontsize=8, framealpha=0.9, ncol=3)
ax.spines["top"].set_visible(False)
ax.spines["right"].set_visible(False)
save(fig, "viode_ate_delta_n5_gray")


# ---------- Figure 3: detection lift ----------
lift_map = load_lifts()
lift_order = [k for k in (f"{e}/{l}" for e in ENVS for l in LEVELS) if k in lift_map]
dl_labels = [f"{ENV_TAG[k.split('/')[0]]}/{k.split('/')[1].split('_')[0]}" for k in lift_order]
lift = [lift_map[k] for k in lift_order]

fig, ax = plt.subplots(figsize=(7.2, 3.4))
xx = range(len(dl_labels))
ax.bar(xx, lift, color="#808080", edgecolor="black", linewidth=0.8, zorder=3)
for xi, v in zip(xx, lift):
    ax.text(xi, v + 0.7, f"{v:.1f}x", ha="center", va="bottom", fontsize=8.5)
ax.axhline(1.0, color="black", linewidth=1.1, linestyle=(0, (5, 3)), zorder=2)
ymax = max(lift) * 1.15 if lift else 36
ax.text(len(dl_labels) - 1.1, ymax * 0.86, "- - -  lift = 1x (random sampling)",
        ha="right", va="top", fontsize=9, style="italic")
ax.set_xticks(list(xx))
ax.set_xticklabels(dl_labels, fontsize=8.5)
ax.set_ylabel("precision lift (x)")
ax.set_ylim(0, ymax if lift else 36)
ax.set_title("Dynamic-feature detection lift on VIODE GT masks", fontsize=10.5)
ax.spines["top"].set_visible(False)
ax.spines["right"].set_visible(False)
save(fig, "viode_detection_lift_gray")
