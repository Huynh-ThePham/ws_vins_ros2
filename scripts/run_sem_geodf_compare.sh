#!/usr/bin/env bash
# Quick A/B: GeoDF-Adaptive vs Semantic–GeoDF fusion.
#
# Usage: ./scripts/run_sem_geodf_compare.sh [euroc|viode] [SEQ_OR_LEVEL ...]
#   euroc default: MH_03_medium
#   viode default:  2_mid 3_high
set -eo pipefail

WS="$(cd "$(dirname "$0")/.." && pwd)"
# shellcheck source=scripts/lib/geodf_common.sh
source "${WS}/scripts/lib/geodf_common.sh"
# shellcheck source=scripts/lib/sad_common.sh
source "${WS}/scripts/lib/sad_common.sh"

TARGET="${1:-viode}"
shift || true

source_ros2_ws "$WS"
export YOLO_DEVICE="${YOLO_DEVICE:-cuda}"
export SAD_YOLO_BAG_RATE="${SAD_YOLO_BAG_RATE:-0.5}"

link_euroc_ros2_bag() {
    local seq="$1"
    local dst
    dst="$(resolve_euroc_ros2_bag "$seq" "$WS")"
    mkdir -p "$(dirname "$dst")"
    if [ -f "${dst}/metadata.yaml" ]; then
        return 0
    fi
    local src="/media/theph/Data1/Research/raw_datasets/euroc/machine_hall/${seq}/ros2_bag"
    if [ -f "${src}/metadata.yaml" ]; then
        ln -sfn "$src" "$dst"
        return 0
    fi
    bash "${WS}/scripts/euroc_prepare.sh" "$seq"
}

link_viode_ros2_bag() {
    local level="$1"
    local bag_ros1="${VIODE}/${VIODE_ENV}/${level}.bag"
    local dst
    dst="$(resolve_viode_ros2_bag "$bag_ros1" "$WS")"
    mkdir -p "$(dirname "$dst")"
    if [ -f "${dst}/metadata.yaml" ]; then
        return 0
    fi
    local alt="/home/theph/ws_vins_ros2/data/viode_ros2/${VIODE_ENV}/${level}/ros2_bag"
    if [ -f "${alt}/metadata.yaml" ]; then
        ln -sfn "$alt" "$dst"
        return 0
    fi
    bash "${WS}/scripts/viode_prepare_ros2_bag.sh" "$bag_ros1" "$dst"
}

run_euroc_pair() {
    local seq="$1"
    link_euroc_ros2_bag "$seq"
    local group gt bag start tag
    group="$(euroc_group_for_seq "$seq")"
    gt="${EUROC}/${group}/${seq}/${seq}/mav0/state_groundtruth_estimate0/data.csv"
    bag="$(resolve_euroc_ros2_bag "$seq" "$WS")"
    start="$(euroc_bag_start_s "$seq")"
    tag="$(start_tag "$start")"
    [ -f "$gt" ] || { echo "Missing GT: $gt" >&2; return 1; }

    for method in adaptive sem_geodf; do
        local mode out run_cfg use_yolo=0
        mode="$(geodf_method_to_mode "$method")"
        out="${WS}/results/sem_geodf_compare/euroc/${seq}_${method}_s${tag}"
        [ "$method" = "sem_geodf" ] && use_yolo=1

        if [ "${FORCE:-0}" != "1" ] && [ -f "${out}/eval/metrics.json" ]; then
            echo "[have] euroc $seq $method"
            continue
        fi

        EUROC_CFG="$(ros2 pkg prefix pht_vio_ros)/share/pht_vio_ros/config/euroc"
        run_cfg="${EUROC_CFG}/euroc_${mode}_config_run_${seq}.yaml"
        mkdir -p "$out"
        cp "${EUROC_CFG}/euroc_${mode}_config.yaml" "$run_cfg"
        sed -i "s|output_path: \"~/output/\"|output_path: \"${out}/\"|" "$run_cfg"
        sed -i "s|pose_graph_save_path: \"~/output/pose_graph/\"|pose_graph_save_path: \"${out}/pose_graph/\"|" "$run_cfg"

        echo "=== EuRoC $seq $method (yolo=$use_yolo) ==="
        killall -9 pht_vio_node mask_node 2>/dev/null || true
        sleep 1
        if [ "$use_yolo" = "1" ]; then
            run_sad_vio_benchmark "$run_cfg" "$out" "$bag" "$start" 1.0 1
        else
            run_pht_vio_benchmark "$run_cfg" "$out" "$bag" "$start" 1.0
        fi
        python3 "${WS}/scripts/evaluate_trajectory.py" \
            "${out}/vio.csv" "$gt" "${out}/eval" \
            --no-plot --run-name "${seq}_${method}"
    done
}

run_viode_pair() {
    local level="$1"
    local bag_ros1="${VIODE}/${VIODE_ENV}/${level}.bag"
    local bag_ros2
    [ -f "$bag_ros1" ] || { echo "[skip] missing $bag_ros1"; return 0; }
    link_viode_ros2_bag "$level"
    bag_ros2="$(resolve_viode_ros2_bag "$bag_ros1" "$WS")"

    for method in adaptive sem_geodf; do
        local mode out run_cfg use_yolo=0 run_name
        mode="$(geodf_method_to_mode "$method")"
        run_name="${VIODE_ENV}_${level}_${method}"
        out="${WS}/results/sem_geodf_compare/viode/${run_name}"
        [ "$method" = "sem_geodf" ] && use_yolo=1

        if [ "${FORCE:-0}" != "1" ] && [ -f "${out}/eval/metrics.json" ]; then
            echo "[have] viode $run_name"
            continue
        fi

        VIODE_CFG="$(ros2 pkg prefix pht_vio_ros)/share/pht_vio_ros/config/viode"
        run_cfg="${VIODE_CFG}/viode_${mode}_config_run_${level}.yaml"
        mkdir -p "$out"
        cp "${VIODE_CFG}/viode_${mode}_config.yaml" "$run_cfg"
        sed -i "s|output_path: \"~/output/\"|output_path: \"${out}/\"|" "$run_cfg"
        sed -i "s|pose_graph_save_path: \"~/output/pose_graph/\"|pose_graph_save_path: \"${out}/pose_graph/\"|" "$run_cfg"

        echo "=== VIODE $run_name (yolo=$use_yolo) ==="
        killall -9 pht_vio_node mask_node 2>/dev/null || true
        sleep 1
        if [ "$use_yolo" = "1" ]; then
            run_sad_vio_benchmark "$run_cfg" "$out" "$bag_ros2" 0 "$SAD_YOLO_BAG_RATE" 1
        else
            run_pht_vio_benchmark "$run_cfg" "$out" "$bag_ros2" 0 1.0
        fi
        python3 "${WS}/scripts/viode_dump_gt.py" --bag "$bag_ros1" --out "${out}/gt_odometry.csv"
        python3 "${WS}/scripts/evaluate_trajectory.py" \
            "${out}/vio.csv" "${out}/gt_odometry.csv" "${out}/eval" \
            --no-plot --run-name "$run_name"
    done
}

EUROC="$(resolve_euroc_root)" || true
VIODE="$(resolve_viode_root)" || true
VIODE_ENV="${VIODE_ENV:-city_day}"
mkdir -p "${WS}/results/sem_geodf_compare"

case "$TARGET" in
    euroc)
        SEQS=("$@")
        [ ${#SEQS[@]} -eq 0 ] && SEQS=(MH_03_medium)
        for seq in "${SEQS[@]}"; do
            run_euroc_pair "$seq"
        done
        ;;
    viode)
        LEVELS=("$@")
        [ ${#LEVELS[@]} -eq 0 ] && LEVELS=(2_mid 3_high)
        for level in "${LEVELS[@]}"; do
            run_viode_pair "$level"
        done
        ;;
    *)
        echo "Usage: $0 [euroc|viode] [sequences or levels...]" >&2
        exit 1
        ;;
esac

python3 - <<'PY' "${WS}/results/sem_geodf_compare"
import json, sys
from pathlib import Path

root = Path(sys.argv[1])
rows = []
for metrics in sorted(root.rglob("eval/metrics.json")):
    run = metrics.parent.parent.name
    m = json.loads(metrics.read_text())
    ate = m.get("ate_rmse_m")
    if ate is None:
        continue
    method = "sem_geodf" if "sem_geodf" in run else "adaptive"
    rows.append((run, method, ate))

print("\n=== ATE RMSE (m) — lower is better ===")
print(f"{'run':<40} {'method':<12} {'ATE':>8}")
for run, method, ate in rows:
    print(f"{run:<40} {method:<12} {ate:8.4f}")

groups = {}
for run, method, ate in rows:
    key = run.replace("_sem_geodf", "").replace("_adaptive", "")
    groups.setdefault(key, {})[method] = ate

print("\n=== Delta fusion vs adaptive ===")
for key, vals in sorted(groups.items()):
    if "adaptive" not in vals or "sem_geodf" not in vals:
        continue
    b, f = vals["adaptive"], vals["sem_geodf"]
    pct = 100.0 * (f - b) / b if b > 0 else 0.0
    verdict = "BETTER" if f < b else ("WORSE" if f > b else "TIE")
    print(f"{key:<36} adaptive={b:.4f} fusion={f:.4f}  Δ={pct:+.1f}%  {verdict}")
PY
