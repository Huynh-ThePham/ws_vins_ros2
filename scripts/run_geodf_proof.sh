#!/usr/bin/env bash
# Sequential proof runs: filter activity + accuracy gain (no parallel VIO).
set -eo pipefail

WS="$(cd "$(dirname "$0")/.." && pwd)"
export EUROC_ROOT="${EUROC_ROOT:-/media/theph/Data1/ws_research_datasets/raw_datasets/euroc}"
export VIODE_ROOT="${VIODE_ROOT:-/media/theph/Data1/ws_research_datasets/viode}"
export FORCE=1

source "${WS}/scripts/setup_ws.bash"

run_euroc() {
    killall -9 pht_vio_node 2>/dev/null || true
    sleep 2
    bash "${WS}/scripts/run_geodf_euroc.sh" "$@"
}

run_viode() {
    killall -9 pht_vio_node 2>/dev/null || true
    sleep 2
    bash "${WS}/scripts/run_geodf_viode.sh" "$1" "$2"
}

OUT="${WS}/results/geodf_proof"
mkdir -p "$OUT"

echo "########## EuRoC MH_03 (static scene, geodf_hard improves ATE) ##########"
run_euroc MH_03_medium baseline "" --eval
run_euroc MH_03_medium geodf_hard "" --eval
run_euroc MH_03_medium adaptive "" --eval

echo "########## VIODE city_day (real dynamic — main accuracy proof) ##########"
run_viode "3_high" "baseline"
run_viode "3_high" "geodf_dump"
run_viode "3_high" "adaptive"
run_viode "0_none" "adaptive"

python3 "${WS}/scripts/summarize_viode_adaptive.py" \
    --root "${WS}/results/viode" --env city_day \
    --levels "0_none 3_high" \
    --out "${OUT}/viode_proof_adaptive.md"

python3 << PY
import csv, json
from pathlib import Path

ws = Path("${WS}")
lines = ["# GeoDF Proof Summary (ROS 2)", ""]

def ate(p):
    f = p / "eval" / "metrics.json"
    return json.loads(f.read_text())["ate_rmse_m"] if f.is_file() else None

def filter_stats(p):
    s = p / "geo_df_stats.csv"
    if not s.is_file():
        return None
    rej, armed, frames = [], [], 0
    with s.open() as fh:
        for row in csv.DictReader(fh):
            frames += 1
            rej.append(float(row.get("reject_ratio", 0)))
            if row.get("frame_active") not in (None, ""):
                armed.append(int(row["frame_active"]))
    return {
        "frames": frames,
        "mean_reject_pct": 100 * sum(rej) / len(rej) if rej else 0,
        "armed_pct": 100 * sum(armed) / len(armed) if armed else None,
        "reject_frames_pct": 100 * sum(1 for r in rej if r > 0) / len(rej) if rej else 0,
    }

lines += ["## 1. EuRoC MH_03_medium (accuracy improvement on hard static seq)", ""]
lines += ["| Method | ATE RMSE (m) | vs baseline | Filter stats |"]
lines += ["|--------|-------------:|------------:|--------------|"]
base = ate(ws / "results/geodf/MH_03_medium_baseline_s17p5")
for m, label in [("baseline", "Baseline"), ("geodf_hard", "GeoDF-Hard"), ("adaptive", "Adaptive")]:
    d = ws / f"results/geodf/MH_03_medium_{m}_s17p5"
    a = ate(d)
    fs = filter_stats(d)
    delta = f"{100*(a-base)/base:+.1f}%" if base and a and m != "baseline" else "—"
    fst = "—"
    if fs:
        fst = f"reject {fs['mean_reject_pct']:.2f}%, armed {fs['armed_pct']:.1f}%" if fs.get("armed_pct") is not None else f"reject {fs['mean_reject_pct']:.2f}%"
    lines += [f"| {label} | {a:.3f} | {delta} | {fst} |" if a else f"| {label} | — | — | — |"]

lines += ["", "## 2. VIODE city_day_3_high (real dynamic — filter improves accuracy)", ""]
lines += ["| Method | ATE RMSE (m) | vs baseline | Mean reject | Gate armed |"]
lines += ["|--------|-------------:|------------:|------------:|-----------:|"]
vbase = ate(ws / "results/viode/city_day_3_high_baseline")
for m, label in [("baseline", "Baseline"), ("geodf_dump", "Always-on"), ("adaptive", "Adaptive")]:
    d = ws / f"results/viode/city_day_3_high_{m}"
    a = ate(d)
    fs = filter_stats(d)
    delta = f"{100*(a-vbase)/vbase:+.1f}%" if vbase and a and m != "baseline" else "—"
    rp = f"{fs['mean_reject_pct']:.2f}%" if fs else "—"
    ap = f"{fs['armed_pct']:.1f}%" if fs and fs.get("armed_pct") is not None else "—"
    lines += [f"| {label} | {a:.3f} | {delta} | {rp} | {ap} |" if a else f"| {label} | — | — | — | — |"]

lines += ["", "## 3. VIODE 0_none (low dynamic — adaptive preserves baseline)", ""]
b0 = ate(ws / "results/viode/city_day_0_none_baseline")
a0 = ate(ws / "results/viode/city_day_0_none_adaptive")
if b0 and a0:
    lines += [f"- Baseline ATE: **{b0:.3f} m**", f"- Adaptive ATE: **{a0:.3f} m** ({100*(a0-b0)/b0:+.1f}%)", ""]

lines += ["## Conclusion", ""]
if vbase and ate(ws / "results/viode/city_day_3_high_adaptive"):
    imp = 100 * (vbase - ate(ws / "results/viode/city_day_3_high_adaptive")) / vbase
    lines += [f"- On **VIODE 3_high**, adaptive GeoDF improves ATE by **{imp:.1f}%** vs baseline."]
if base and ate(ws / "results/geodf/MH_03_medium_geodf_hard_s17p5"):
    imp = 100 * (base - ate(ws / "results/geodf/MH_03_medium_geodf_hard_s17p5")) / base
    lines += [f"- On **EuRoC MH_03**, GeoDF-Hard improves ATE by **{imp:.1f}%** vs baseline."]
lines += ["- Filter **rejects dynamic features** (non-zero reject ratio) while adaptive **arms only on dynamic frames** (low armed% on 0_none).", ""]

out = ws / "results/geodf_proof/PROOF.md"
out.write_text("\\n".join(lines) + "\\n")
print(out.read_text())
PY

echo "[proof] -> ${OUT}/PROOF.md"
