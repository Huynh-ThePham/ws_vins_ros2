#!/usr/bin/env python3
"""Build Sem-GeoDF paper figures + LaTeX table snippets from ablation results.

Usage:
  python3 scripts/make_sem_geodf_paper_assets.py \\
    --root results/sem_geodf_ablation/paper_postfix \\
    --out paper/sem_geodf
"""
from __future__ import annotations

import argparse
import csv
import json
import statistics as st
import sys
from collections import defaultdict
from pathlib import Path

import matplotlib

matplotlib.use("Agg")
import matplotlib.pyplot as plt

_SCRIPT = Path(__file__).resolve().parent
sys.path.insert(0, str(_SCRIPT / "lib"))
from sem_geodf_ablation_common import iter_run_records, mean_std  # noqa: E402

METHODS = ("baseline", "adaptive", "sad_sem", "sem_geodf")
METHOD_LABEL = {
    "baseline": "Baseline",
    "adaptive": "GeoDF-Adaptive",
    "sad_sem": "SAD-Sem",
    "sem_geodf": "Sem-GeoDF (ours)",
}
VIODE_SCENES = [
    f"{env}_{lv}"
    for env in ("city_day", "city_night", "parking_lot")
    for lv in ("0_none", "1_low", "2_mid", "3_high")
]
EUROC_SCENES = [
    "MH_01_easy",
    "MH_02_easy",
    "MH_03_medium",
    "MH_04_difficult",
    "MH_05_difficult",
]
SHORT = {
    "city_day_0_none": "CD/0",
    "city_day_1_low": "CD/1",
    "city_day_2_mid": "CD/2",
    "city_day_3_high": "CD/3",
    "city_night_0_none": "CN/0",
    "city_night_1_low": "CN/1",
    "city_night_2_mid": "CN/2",
    "city_night_3_high": "CN/3",
    "parking_lot_0_none": "PL/0",
    "parking_lot_1_low": "PL/1",
    "parking_lot_2_mid": "PL/2",
    "parking_lot_3_high": "PL/3",
}


DIVERGED_ATE_M = 50.0
# A diverged run is scored at this ATE in the failure-penalized metric, rather than
# being removed from the average. Dropping it makes a method that fails outright look
# better than one that degrades gracefully.
FAILURE_PENALTY_ATE_M = DIVERGED_ATE_M


def collect(root: Path):
    """Per-cell ATE values, plus a full account of every run that was NOT used.

    Plan P0.3: runs above the divergence threshold used to be dropped with a bare
    `continue`, so the published tables silently excluded exactly the cases the paper
    claims to improve. Nothing is discarded now: each excluded run is counted and
    reported, and the caller must publish the success rate alongside the ATE.
    """
    by = defaultdict(list)
    excluded = defaultdict(lambda: {"diverged": 0, "failed": 0, "qc_failed": 0,
                                    "oracle": 0, "no_ate": 0})
    diverged_values = defaultdict(list)

    for rec in iter_run_records(root):
        key = (rec.scene, rec.method)
        if rec.oracle_ablation:
            excluded[key]["oracle"] += 1
            continue
        if not rec.qc_ok:
            excluded[key]["qc_failed"] += 1
            continue
        if rec.ate_rmse_m is None:
            excluded[key]["no_ate"] += 1
            continue
        if rec.ate_rmse_m > DIVERGED_ATE_M:
            excluded[key]["diverged"] += 1
            diverged_values[key].append(rec.ate_rmse_m)
            continue
        by[key].append(rec.ate_rmse_m)

    return {"ate": by, "excluded": excluded, "diverged_values": diverged_values}


def cell_counts(data, scene, method):
    """(successful, diverged, failed, total) for one matrix cell."""
    key = (scene, method)
    ok = len(data["ate"].get(key, []))
    ex = data["excluded"].get(key, {})
    diverged = ex.get("diverged", 0)
    failed = ex.get("failed", 0) + ex.get("qc_failed", 0) + ex.get("no_ate", 0)
    return ok, diverged, failed, ok + diverged + failed


def success_rate(data, scene, method):
    ok, _, _, total = cell_counts(data, scene, method)
    return (ok / total) if total else None


def failure_penalized_ate(data, scene, method):
    """Mean ATE with diverged/failed runs charged at FAILURE_PENALTY_ATE_M.

    Reported next to the plain ATE so a method cannot look better by failing
    outright rather than degrading.
    """
    key = (scene, method)
    values = list(data["ate"].get(key, []))
    ok, diverged, failed, total = cell_counts(data, scene, method)
    if not total:
        return None
    values.extend([FAILURE_PENALTY_ATE_M] * (diverged + failed))
    return sum(values) / len(values)


def ate(data, scene, method):
    """Mean/std/count over SUCCESSFUL runs only.

    Always report success_rate() and failure_penalized_ate() beside this: on its own
    it is an average over survivors.
    """
    vals = data["ate"].get((scene, method), []) if isinstance(data, dict) and "ate" in data \
        else data.get((scene, method), [])
    if not vals:
        return None, None, 0
    return mean_std(vals)


def delta_pct(a, b):
    if a is None or b is None or a == 0:
        return None
    return (b - a) / a * 100.0


def latex_escape(s: str) -> str:
    return s.replace("_", r"\_")


def _fmt_cell(mu, sd, n):
    """Format one ATE cell as mean(+/-std) with an explicit trial-count superscript.

    The superscript makes the number of trials visible per cell so a reader can
    distinguish an N=1 single run from an averaged N=3 mean (the old table hid
    this and could not tell them apart)."""
    if mu is None:
        return "---"
    if n > 1:
        body = f"{mu:.3f}$\\pm${sd:.3f}"
    else:
        body = f"{mu:.3f}"
    return f"{body}$^{{{n}}}$"


def write_ate_table(by, scenes, path: Path, caption: str, label: str):
    lines = [
        r"\begin{table*}[t]",
        r"\centering",
        rf"\caption{{{caption} Superscript is the number of trials $N$ per cell; "
        rf"cells with $N{{=}}1$ report a single run (no std). Best mean per row in bold.}}",
        rf"\label{{{label}}}",
        r"\setlength{\tabcolsep}{4pt}",
        r"\begin{tabular}{lcccc}",
        r"\toprule",
        r"Scene & Baseline & GeoDF-Adaptive & SAD-Sem & Sem-GeoDF (ours) \\",
        r"\midrule",
    ]
    for sc in scenes:
        cells = [latex_escape(sc)]
        best = None
        vals = {}
        for m in METHODS:
            mu, sd, n = ate(by, sc, m)
            vals[m] = mu
            cells.append(_fmt_cell(mu, sd, n))
            if mu is not None and (best is None or mu < best):
                best = mu
        # bold best MEAN (not best single trial)
        if best is not None:
            for i, m in enumerate(METHODS, start=1):
                if vals.get(m) is not None and abs(vals[m] - best) < 1e-9:
                    cells[i] = r"\textbf{" + cells[i] + "}"
        lines.append(" & ".join(cells) + r" \\")
    lines += [r"\bottomrule", r"\end{tabular}", r"\end{table*}", ""]
    path.write_text("\n".join(lines))
    print("wrote", path)


TRAIN_SCENES = [f"city_day_{lv}" for lv in ("0_none", "1_low", "2_mid", "3_high")]
HOLDOUT_SCENES = [
    f"{env}_{lv}"
    for env in ("city_night", "parking_lot")
    for lv in ("0_none", "1_low", "2_mid", "3_high")
]


def _delta_verdict(d):
    if d < -3:
        return "better"
    if d > 3:
        return "worse"
    return "similar"


def _delta_group_rows(by, scenes):
    rows, counts = [], {"better": 0, "similar": 0, "worse": 0}
    for sc in scenes:
        a, _, na = ate(by, sc, "adaptive")
        f, _, nf = ate(by, sc, "sem_geodf")
        d = delta_pct(a, f)
        if d is None:
            continue
        verd = _delta_verdict(d)
        counts[verd] += 1
        n_lo, n_hi = min(na, nf), max(na, nf)
        n_str = f"{n_hi}" if n_lo == n_hi else f"{n_lo}--{n_hi}"
        rows.append((latex_escape(sc), d, verd, n_str))
        # n_str is emitted in text mode (see write_delta_table) so "1--2" renders
        # as an en-dash, not two minus signs.
    return rows, counts


def write_delta_table(by, path: Path):
    lines = [
        r"\begin{table}[t]",
        r"\centering",
        r"\caption{Sem-GeoDF ATE change versus GeoDF-Adaptive "
        r"(negative $=$ better; both means over the same $N$). Decision band "
        r"$\pm 3\%$. Train (\texttt{city\_day}) is separated from hold-out; "
        r"EuRoC is static-only with a low per-cell trial count (see superscripts).}",
        r"\label{tab:delta_adaptive}",
        r"\begin{tabular}{lrrc}",
        r"\toprule",
        r"Scene & $\Delta$ vs Adaptive [\%] & Verdict & $N$ \\",
        r"\midrule",
    ]

    def emit(title, scenes, is_holdout=False):
        rows, counts = _delta_group_rows(by, scenes)
        if not rows:
            return counts
        lines.append(rf"\multicolumn{{4}}{{l}}{{\emph{{{title}}}}} \\")
        for sc, d, verd, n_str in rows:
            lines.append(f"{sc} & {d:+.1f} & {verd} & {n_str} \\\\")
        lines.append(
            rf"\multicolumn{{4}}{{l}}{{\quad subtotal: "
            rf"{counts['better']} better / {counts['similar']} similar / "
            rf"{counts['worse']} worse}} \\"
        )
        lines.append(r"\midrule")
        return counts

    emit("Train: city\\_day (thresholds selected here)", TRAIN_SCENES)
    ch = emit("Hold-out: city\\_night, parking\\_lot", HOLDOUT_SCENES, is_holdout=True)
    ce = emit("EuRoC (static generalization, low trial count)", EUROC_SCENES)

    lines += [
        rf"\multicolumn{{4}}{{l}}{{\textbf{{Hold-out headline:}} "
        rf"{ch['better']} better / {ch['similar']} similar / {ch['worse']} worse "
        rf"on the 8 dynamic hold-out scenes.}} \\",
        r"\bottomrule",
        r"\end{tabular}",
        r"\end{table}",
        "",
    ]
    path.write_text("\n".join(lines))
    print("wrote", path)


def _weight_ablation_rows(by, scenes):
    """Per-scene ATE for w_i off vs on, delta, and verdict.

    delta_pct(off, on) is the change going from weights-off to weights-on, so a
    negative value means the adaptive backend weights improved ATE."""
    rows, counts = [], {"better": 0, "similar": 0, "worse": 0}
    for sc in scenes:
        off_mu, off_sd, off_n = ate(by, sc, "sem_geodf_noweight")
        on_mu, on_sd, on_n = ate(by, sc, "sem_geodf")
        if off_mu is None or on_mu is None:
            continue
        d = delta_pct(off_mu, on_mu)
        verd = _delta_verdict(d)
        counts[verd] += 1
        n_lo, n_hi = min(off_n, on_n), max(off_n, on_n)
        n_str = f"{n_hi}" if n_lo == n_hi else f"{n_lo}--{n_hi}"
        rows.append((sc, off_mu, off_sd, off_n, on_mu, on_sd, on_n, d, verd, n_str))
    return rows, counts


def write_weight_ablation_table(by, path: Path):
    """Isolated ablation of the adaptive backend residual weights (w_i on/off).

    Both columns share the identical gated-union front-end and train-selected
    policy; the only difference is sem_geodf_backend_weight (1 vs 0). Emits a
    valid table even when no w_i-off runs exist yet, so the manuscript compiles
    before the ablation is executed."""
    lines = [
        r"\begin{table}[t]",
        r"\centering",
        r"\caption{Backend residual-weight ablation on VIODE: Sem-GeoDF with "
        r"adaptive weights (\(w_i\) on) versus the identical gated union with "
        r"\(w_i\) off (\texttt{sem\_geodf\_backend\_weight}{=}0) under the same "
        r"train-selected policy. \(\Delta\) is the ATE change from \(w_i\) off to "
        r"on (negative \(=\) weighting helps); band \(\pm 3\%\). Superscript is "
        r"the trial count \(N\).}",
        r"\label{tab:ablation_weight}",
        r"\setlength{\tabcolsep}{4pt}",
        r"\begin{tabular}{lccrc}",
        r"\toprule",
        r"Scene & \(w_i\) off & \(w_i\) on (ours) & \(\Delta\) [\%] & Verdict \\",
        r"\midrule",
    ]
    paired = []

    def emit(title, scenes):
        rows, counts = _weight_ablation_rows(by, scenes)
        if not rows:
            return
        lines.append(rf"\multicolumn{{5}}{{l}}{{\emph{{{title}}}}} \\")
        for sc, off_mu, off_sd, off_n, on_mu, on_sd, on_n, d, verd, _n in rows:
            lines.append(
                f"{latex_escape(sc)} & {_fmt_cell(off_mu, off_sd, off_n)} & "
                f"{_fmt_cell(on_mu, on_sd, on_n)} & {d:+.1f} & {verd} \\\\"
            )
            paired.append((off_mu, on_mu))
        lines.append(
            rf"\multicolumn{{5}}{{l}}{{\quad subtotal: {counts['better']} better / "
            rf"{counts['similar']} similar / {counts['worse']} worse}} \\"
        )
        lines.append(r"\midrule")

    emit("Train: city\\_day", TRAIN_SCENES)
    emit("Hold-out: city\\_night, parking\\_lot", HOLDOUT_SCENES)

    if paired:
        off_mean = sum(p[0] for p in paired) / len(paired)
        on_mean = sum(p[1] for p in paired) / len(paired)
        dmean = delta_pct(off_mean, on_mean)
        lines.append(
            rf"\textbf{{Mean}} & \textbf{{{off_mean:.3f}}} & "
            rf"\textbf{{{on_mean:.3f}}} & \textbf{{{dmean:+.1f}}} & \\"
        )
    else:
        lines.append(
            r"\multicolumn{5}{l}{\emph{No \(w_i\)-off ablation runs found in the "
            r"result tree; run METHODS including sem\_geodf\_noweight.}} \\"
        )
    lines += [r"\bottomrule", r"\end{tabular}", r"\end{table}", ""]
    path.write_text("\n".join(lines))
    print("wrote", path)


def fig_ate_delta(by, figdir: Path):
    labels, vals = [], []
    for sc in VIODE_SCENES:
        a, _, _ = ate(by, sc, "adaptive")
        f, _, _ = ate(by, sc, "sem_geodf")
        d = delta_pct(a, f)
        if d is None:
            continue
        labels.append(SHORT[sc])
        vals.append(d)
    fig, ax = plt.subplots(figsize=(7.4, 3.5))
    for i, v in enumerate(vals):
        if v >= 0:
            ax.bar(i, v, color="white", edgecolor="black", hatch="////", zorder=3)
        else:
            ax.bar(i, v, color="#4d4d4d", edgecolor="black", zorder=3)
        ax.text(
            i,
            v + (1.8 if v >= 0 else -1.8),
            f"{v:+.1f}",
            ha="center",
            va="bottom" if v >= 0 else "top",
            fontsize=8,
        )
    ax.axhline(0, color="black", lw=1)
    ax.axhspan(-3, 3, color="#dddddd", alpha=0.7, zorder=0)
    ax.set_xticks(range(len(labels)))
    ax.set_xticklabels(labels, fontsize=8.5)
    ax.set_ylabel("ATE change vs GeoDF-Adaptive [%]")
    ax.set_title("Sem-GeoDF vs GeoDF-Adaptive on VIODE (negative = better)")
    ymax = max(55, max(abs(v) for v in vals) + 8)
    ax.set_ylim(-ymax, ymax)
    fig.tight_layout()
    for ext in ("pdf", "png", "svg"):
        fig.savefig(figdir / f"viode_ate_delta_sem_geodf.{ext}", dpi=300 if ext == "png" else None)
    plt.close(fig)
    print("wrote", figdir / "viode_ate_delta_sem_geodf.*")


def fig_methods_grouped(by, figdir: Path):
    # Grouped bars for high-dynamic scenes
    scenes = [
        "city_day_3_high",
        "city_night_3_high",
        "parking_lot_3_high",
        "city_day_0_none",
        "parking_lot_0_none",
    ]
    import numpy as np

    x = np.arange(len(scenes))
    width = 0.2
    fig, ax = plt.subplots(figsize=(8.0, 3.8))
    hatches = ["", "////", "....", "xxxx"]
    for i, m in enumerate(METHODS):
        ys = []
        for sc in scenes:
            mu, _, _ = ate(by, sc, m)
            ys.append(mu if mu is not None else float("nan"))
        ax.bar(
            x + (i - 1.5) * width,
            ys,
            width,
            label=METHOD_LABEL[m],
            edgecolor="black",
            color="white" if i % 2 else "#666666",
            hatch=hatches[i],
        )
    ax.set_xticks(x)
    ax.set_xticklabels([SHORT.get(s, s) for s in scenes], fontsize=9)
    ax.set_ylabel("ATE RMSE [m]")
    ax.set_title("Selected scenes: four-method comparison")
    ax.legend(fontsize=8, ncol=2, loc="upper right")
    fig.tight_layout()
    for ext in ("pdf", "png", "svg"):
        fig.savefig(figdir / f"method_compare_selected.{ext}", dpi=300 if ext == "png" else None)
    plt.close(fig)
    print("wrote", figdir / "method_compare_selected.*")


def fig_pipeline(figdir: Path):
    """Simple box pipeline figure (vector)."""
    fig, ax = plt.subplots(figsize=(8.2, 3.2))
    ax.set_xlim(0, 12)
    ax.set_ylim(0, 4)
    ax.axis("off")

    def box(x, y, w, h, text):
        from matplotlib.patches import FancyBboxPatch

        p = FancyBboxPatch(
            (x, y),
            w,
            h,
            boxstyle="round,pad=0.02,rounding_size=0.08",
            linewidth=1.2,
            edgecolor="black",
            facecolor="white",
        )
        ax.add_patch(p)
        ax.text(x + w / 2, y + h / 2, text, ha="center", va="center", fontsize=8)

    box(0.2, 1.5, 1.8, 1.0, "Stereo\n+ IMU")
    box(2.3, 2.4, 2.2, 1.0, "YOLO mask\nsemantic gate")
    box(2.3, 0.6, 2.2, 1.0, "GeoDF-Adaptive\nepipolar gate")
    box(5.0, 1.5, 2.4, 1.0, "Adaptive policy\nstatic/assist/strong")
    box(7.8, 1.5, 2.0, 1.0, "Gated union\n(OR) + ratio")
    box(10.1, 1.5, 1.7, 1.0, "VINS\nbackend")
    # arrows
    for x0, x1, y in [
        (2.0, 2.3, 2.0),
        (4.5, 5.0, 2.9),
        (4.5, 5.0, 1.1),
        (7.4, 7.8, 2.0),
        (9.8, 10.1, 2.0),
    ]:
        ax.annotate("", xy=(x1, y), xytext=(x0, y), arrowprops=dict(arrowstyle="->", lw=1.2))
    ax.text(6.2, 0.25, "Sem-GeoDF: semantic + geometry adaptive gated union", ha="center", fontsize=9)
    fig.tight_layout()
    for ext in ("pdf", "png", "svg"):
        fig.savefig(figdir / f"pipeline_sem_geodf.{ext}", dpi=300 if ext == "png" else None)
    plt.close(fig)
    print("wrote", figdir / "pipeline_sem_geodf.*")


def _n_range(by, scenes):
    """Min/max trial count across all (scene, method) cells in a group."""
    ns = []
    for sc in scenes:
        for m in METHODS:
            _, _, n = ate(by, sc, m)
            if n:
                ns.append(n)
    if not ns:
        return 0, 0
    return min(ns), max(ns)


def _fmt_n(lo, hi):
    """Math-safe N range: plain int when uniform, else an \\mbox so the en-dash
    survives inside inline math \\(N=...\\)."""
    return f"{hi}" if lo == hi else rf"\mbox{{{lo}--{hi}}}"


def write_meta(by, path: Path):
    v_lo, v_hi = _n_range(by, VIODE_SCENES)
    e_lo, e_hi = _n_range(by, EUROC_SCENES)

    lines = [
        r"% Auto-generated by scripts/make_sem_geodf_paper_assets.py. Do not edit by hand.",
        rf"\newcommand{{\PaperResultRoot}}{{results/sem\_geodf\_ablation/paper\_postfix}}",
        rf"\newcommand{{\PaperNViode}}{{{_fmt_n(v_lo, v_hi)}}}",
        rf"\newcommand{{\PaperNEuroc}}{{{_fmt_n(e_lo, e_hi)}}}",
        # Backward-compatible: \PaperNTrials now reflects the VIODE (main) protocol.
        rf"\newcommand{{\PaperNTrials}}{{{_fmt_n(v_lo, v_hi)}}}",
        "",
    ]

    # Hold-out headline summary (used in abstract/text).
    _, counts = _delta_group_rows(by, HOLDOUT_SCENES)
    lines.append(rf"\newcommand{{\HoldoutBetter}}{{{counts['better']}}}")
    lines.append(rf"\newcommand{{\HoldoutSimilar}}{{{counts['similar']}}}")
    lines.append(rf"\newcommand{{\HoldoutWorse}}{{{counts['worse']}}}")
    lines.append("")

    # highlight numbers for abstract (computed from current data, all N-matched)
    for sc, macro in (
        ("city_night_3_high", "DeltaCityNightHigh"),
        ("parking_lot_3_high", "DeltaParkingHigh"),
        ("city_night_2_mid", "DeltaCityNightMid"),
        ("parking_lot_2_mid", "DeltaParkingMid"),
    ):
        a, _, _ = ate(by, sc, "adaptive")
        f, _, _ = ate(by, sc, "sem_geodf")
        d = delta_pct(a, f)
        if d is not None:
            lines.append(rf"\newcommand{{\{macro}}}{{{d:+.1f}\%}}")
    lines.append("")

    # Backend residual-weight ablation (w_i on vs off), N-matched per VIODE cell.
    ab_off, ab_on = [], []
    ab_counts = {"better": 0, "similar": 0, "worse": 0}
    for sc in VIODE_SCENES:
        off_mu, _, _ = ate(by, sc, "sem_geodf_noweight")
        on_mu, _, _ = ate(by, sc, "sem_geodf")
        if off_mu is None or on_mu is None:
            continue
        ab_off.append(off_mu)
        ab_on.append(on_mu)
        ab_counts[_delta_verdict(delta_pct(off_mu, on_mu))] += 1
    if ab_off:
        off_mean = sum(ab_off) / len(ab_off)
        on_mean = sum(ab_on) / len(ab_on)
        ab_macros = (
            ("AblationNCells", f"{len(ab_off)}"),
            ("AblationMeanWOff", f"{off_mean:.3f}"),
            ("AblationMeanWOn", f"{on_mean:.3f}"),
            ("AblationDeltaMean", f"{delta_pct(off_mean, on_mean):+.1f}\\%"),
            ("AblationWeightBetter", f"{ab_counts['better']}"),
            ("AblationWeightSimilar", f"{ab_counts['similar']}"),
            ("AblationWeightWorse", f"{ab_counts['worse']}"),
        )
    else:
        # Placeholder defaults so the manuscript compiles before the ablation runs.
        ab_macros = (
            ("AblationNCells", "0"),
            ("AblationMeanWOff", "--"),
            ("AblationMeanWOn", "--"),
            ("AblationDeltaMean", "n/a"),
            ("AblationWeightBetter", "0"),
            ("AblationWeightSimilar", "0"),
            ("AblationWeightWorse", "0"),
        )
    for macro, val in ab_macros:
        lines.append(rf"\newcommand{{\{macro}}}{{{val}}}")
    path.write_text("\n".join(lines) + "\n")
    print("wrote", path)


def write_failure_table(data, scenes, path: Path, caption: str, label: str):
    """Success rate, diverged count and failure-penalized ATE per cell.

    Plan P0.3 requires all four numbers to be published together. An ATE mean on its
    own hides that the diverged runs were removed from it.
    """
    lines = [
        r"% Generated by scripts/make_sem_geodf_paper_assets.py -- do not edit.",
        r"\begin{table}[t]",
        r"\centering",
        r"\caption{" + caption + r"}",
        r"\label{" + label + r"}",
        r"\begin{tabular}{l" + "r" * (2 * len(METHODS)) + r"}",
        r"\hline",
        r"Scene & " + " & ".join(
            rf"\multicolumn{{2}}{{c}}{{{latex_escape(METHOD_LABEL[m])}}}" if False
            else f"{latex_escape(METHOD_LABEL[m])} SR & pen. ATE" for m in METHODS) + r" \\",
        r"\hline",
    ]
    for scene in scenes:
        cells = []
        for method in METHODS:
            sr = success_rate(data, scene, method)
            pen = failure_penalized_ate(data, scene, method)
            ok, diverged, failed, total = cell_counts(data, scene, method)
            sr_text = "--" if sr is None else rf"{sr * 100:.0f}\%$_{{{ok}/{total}}}$"
            pen_text = "--" if pen is None else f"{pen:.3f}"
            if diverged or failed:
                pen_text += r"$^{\dagger}$"
            cells.extend([sr_text, pen_text])
        lines.append(f"{latex_escape(SHORT.get(scene, scene))} & " + " & ".join(cells) + r" \\")
    lines += [
        r"\hline",
        r"\end{tabular}",
        r"\\[2pt]",
        r"\footnotesize SR = success rate (successful/attempted trials). "
        r"Penalized ATE charges every diverged or failed trial at "
        rf"{FAILURE_PENALTY_ATE_M:.0f}\,m rather than excluding it. "
        r"$^{\dagger}$ this cell contains at least one non-successful trial.",
        r"\end{table}",
        "",
    ]
    path.write_text("\n".join(lines))
    print("wrote", path)


def require_validated(root: Path, allow_unvalidated: bool,
                      require_receipt: Path | None = None) -> None:
    """Refuse to build publication assets from an unvalidated run tree (plan P0.3/P0.5)."""
    report = root / "validation.json"
    if allow_unvalidated:
        print("[warn] --allow-unvalidated: assets are being built WITHOUT the expected-matrix "
              "gate. Do not use the output in the paper.", file=sys.stderr)
        return
    if not report.is_file():
        raise SystemExit(
            f"[fatal] {report} not found.\n"
            f"        Run the gate first:\n"
            f"          python3 scripts/validate_experiment_matrix.py --root {root}\n"
            f"        Publication assets may not be built from an unvalidated run tree.")
    try:
        payload = json.loads(report.read_text())
    except (OSError, json.JSONDecodeError) as exc:
        raise SystemExit(f"[fatal] cannot read {report}: {exc}")
    if payload.get("result") != "PASS":
        errors = payload.get("errors", [])
        raise SystemExit(
            f"[fatal] {report} reports {payload.get('result')} with {len(errors)} problem(s); "
            f"the first is:\n          {errors[0] if errors else '(none recorded)'}\n"
            f"        Fix the run tree and re-validate before building assets.")

    receipt = require_receipt or (root / "validation_receipt.json")
    if not receipt.is_file():
        raise SystemExit(
            f"[fatal] {receipt} not found.\n"
            f"        Re-run validate_experiment_matrix.py with --write-receipt so paper "
            f"assets can prove which manifests were gated.")
    try:
        receipt_payload = json.loads(receipt.read_text())
    except (OSError, json.JSONDecodeError) as exc:
        raise SystemExit(f"[fatal] cannot read {receipt}: {exc}")
    if receipt_payload.get("result") != "PASS":
        raise SystemExit(f"[fatal] validation receipt is not PASS: {receipt}")
    print(f"[ok] expected-matrix gate passed for {root}")
    print(f"[ok] validation receipt: {receipt}")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--root", type=Path, required=True)
    ap.add_argument("--out", type=Path, default=Path("paper/sem_geodf"))
    ap.add_argument("--allow-unvalidated", action="store_true",
                    help="Skip the expected-matrix gate. For local inspection only; the "
                         "output must not go into the paper.")
    ap.add_argument("--require-receipt", type=Path, default=None,
                    help="Path to validation_receipt.json (default: <root>/validation_receipt.json)")
    args = ap.parse_args()
    root = args.root.resolve()
    out = args.out.resolve()
    require_validated(root, args.allow_unvalidated, args.require_receipt)
    figdir = out / "figures"
    tabdir = out / "tables"
    figdir.mkdir(parents=True, exist_ok=True)
    tabdir.mkdir(parents=True, exist_ok=True)

    by = collect(root)
    write_ate_table(
        by,
        VIODE_SCENES,
        tabdir / "ate_viode.tex",
        r"VIODE ATE RMSE [m] (mean$\pm$std).",
        "tab:ate_viode",
    )
    write_ate_table(
        by,
        EUROC_SCENES,
        tabdir / "ate_euroc.tex",
        r"EuRoC Machine Hall ATE RMSE [m].",
        "tab:ate_euroc",
    )
    write_failure_table(by, VIODE_SCENES, tabdir / "failure_viode.tex",
                        r"VIODE success rate and failure-penalized ATE.",
                        "tab:failure_viode")
    write_failure_table(by, EUROC_SCENES, tabdir / "failure_euroc.tex",
                        r"EuRoC success rate and failure-penalized ATE.",
                        "tab:failure_euroc")
    write_delta_table(by, tabdir / "delta_adaptive.tex")
    write_weight_ablation_table(by, tabdir / "ablation_weight.tex")
    write_meta(by, tabdir / "macros.tex")
    fig_pipeline(figdir)
    fig_ate_delta(by, figdir)
    fig_methods_grouped(by, figdir)


if __name__ == "__main__":
    main()
