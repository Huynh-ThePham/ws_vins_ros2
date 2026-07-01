#!/usr/bin/env python3
"""Render grayscale-safe result figures for GeoDF-Weighted, driven by run data.

Fig 2: VIODE ATE improvement (%) per environment-level (matches PAPER_RESULTS_N5).
Fig 3: Dynamic-feature detection lift on VIODE GT masks (matches DETECTION_EVAL_VIODE).

Data sources (no fabricated fallback — figures are skipped or empty if data missing):
  Fig 2 <- results/geodf_evaluation/stats_tests.json or paper_results_n5.json
  Fig 3 <- results/viode_detection/*_weighted/detection_eval.json
"""
import json
import os
import sys

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
    print("[fig2] no run data — skipping figure")
    return None, None


def load_lifts():
    import glob
    out = {}
    for p in sorted(glob.glob("results/viode_detection/*_weighted/detection_eval.json")):
        d = _load_json(p)
        if not d:
            continue
        name = os.path.basename(os.path.dirname(p)).replace("_weighted", "")
        parts = name.split("_")
        if len(parts) >= 2:
            env = "_".join(parts[:-1])
            level = parts[-1]
            lift = d.get("precision_lift")
            if isinstance(lift, (int, float)):
                out[f"{env}/{level}"] = lift
    if out:
        print(f"[fig3] using {len(out)} detection_eval.json files")
        return out
    print("[fig3] no detection data — skipping figure")
    return None


impr_map, tag = load_improvements()
if impr_map:
    ordered = [f"{e}/{l}" for e in ENVS for l in LEVELS]
    labels = [f"{ENV_TAG[e]}/{l.split('_')[0]}" for e in ENVS for l in LEVELS]
    impr = [impr_map.get(k, float("nan")) for k in ordered]

    fig, ax = plt.subplots(figsize=(7.2, 3.4))
    x = range(len(labels))
    for xi, v in zip(x, impr):
        if v != v:
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
    ax.set_title(f"VIODE ATE improvement: GeoDF-Weighted vs baseline [{tag}]", fontsize=10.5)
    ax.legend(handles=[
        Patch(facecolor="#4d4d4d", edgecolor="black", label="improved"),
        Patch(facecolor="white", edgecolor="black", hatch="////", label="regressed"),
        Patch(facecolor="#dddddd", edgecolor="none", label="+/-3% band"),
    ], loc="lower left", fontsize=8, framealpha=0.9, ncol=3)
    ax.spines["top"].set_visible(False)
    ax.spines["right"].set_visible(False)
    save(fig, "viode_ate_delta_n5_gray")

lift_map = load_lifts()
if lift_map:
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
    ax.set_title("Dynamic-feature detection lift (GeoDF-Weighted, weight threshold)", fontsize=10.5)
    ax.spines["top"].set_visible(False)
    ax.spines["right"].set_visible(False)
    save(fig, "viode_detection_lift_gray")

if not impr_map and not lift_map:
    sys.exit(0)
