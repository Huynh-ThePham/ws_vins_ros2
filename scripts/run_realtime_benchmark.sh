#!/usr/bin/env bash
# Async ROS 2 realtime benchmark: YOLO mask_node || pht_vio_node at multiple bag rates.
set -eo pipefail

WS="${WS:-/home/theph/ws_vins_ros2_sadvins}"
VIODE_BAG="${VIODE_BAG:-/home/theph/ws_vins_ros2/data/viode_ros2/city_day/3_high/ros2_bag}"
VIODE_GT="${VIODE_GT:-${WS}/data/viode_gt/city_day/3_high/gt_odometry.csv}"
EUROC_BAG="${EUROC_BAG:-/media/theph/Data1/Research/raw_datasets/euroc/machine_hall/MH_01_easy/ros2_bag}"
EUROC_GT="${EUROC_GT:-/media/theph/Data1/Research/raw_datasets/euroc/machine_hall/MH_01_easy/mav0/state_groundtruth_estimate0/data.csv}"
EUROC_START="${EUROC_START:-40}"

ROOT="${ROOT:-${WS}/results/realtime_benchmark}"
mkdir -p "$ROOT"

# shellcheck disable=SC1091
source "${WS}/scripts/lib/sad_common.sh"
source_ros2_ws "$WS"

CFG_ROOT="$(ros2 pkg prefix pht_vio_ros)/share/pht_vio_ros/config"
export YOLO_DEVICE="${YOLO_DEVICE:-cuda}"

prepare_viode_cfg() {
    local method="$1" out="$2" mode="$3" tag="$4"
    local template="${CFG_ROOT}/viode/viode_${mode}_config.yaml"
    local cfg_dir="${ROOT}/.run_configs"
    mkdir -p "$cfg_dir"
    local cfg="${cfg_dir}/viode_${mode}_${tag}.yaml"
    cp "$template" "$cfg"
    sed -i "s|output_path: \"~/output/\"|output_path: \"${out}/\"|" "$cfg"
    sed -i "s|pose_graph_save_path: \"~/output/pose_graph/\"|pose_graph_save_path: \"${out}/pose_graph/\"|" "$cfg"
    if [[ "$method" == sgta* ]]; then
        apply_sgta_method_overrides sgta "$cfg"
    fi
    echo "$cfg"
}

prepare_euroc_cfg() {
    local method="$1" out="$2" mode="$3" tag="$4"
    local template="${CFG_ROOT}/euroc/euroc_${mode}_config.yaml"
    local cfg_dir="${ROOT}/.run_configs"
    mkdir -p "$cfg_dir"
    local cfg="${cfg_dir}/euroc_${mode}_${tag}.yaml"
    cp "$template" "$cfg"
    sed -i "s|output_path: \"~/output/\"|output_path: \"${out}/\"|" "$cfg"
    sed -i "s|pose_graph_save_path: \"~/output/pose_graph/\"|pose_graph_save_path: \"${out}/pose_graph/\"|" "$cfg"
    if [[ "$method" == sgta* ]]; then
        apply_sgta_method_overrides sgta "$cfg"
    fi
    echo "$cfg"
}

run_case() {
    local name="$1" method="$2" mode="$3" bag="$4" gt="$5" start="$6" rate="$7" every_n="$8" use_yolo="$9"
    local out="${ROOT}/${name}"
    mkdir -p "$out"

    local cfg
    cfg="$(prepare_viode_cfg "$method" "$out" "$mode" "$name")"

    export SAD_BAG_RATE="$rate"
    export YOLO_PROCESS_EVERY_N="$every_n"

    echo ""
    echo "========== ${name} =========="
    echo "method=${method} bag_rate=${rate} yolo_every_n=${every_n} use_yolo=${use_yolo}"

    killall -9 pht_vio_node mask_node 2>/dev/null || true
    sleep 1

    local t0 t1 wall
    t0=$(date +%s.%N)
    run_sad_vio_benchmark "$cfg" "$out" "$bag" "$start" "$rate" "$use_yolo"
    t1=$(date +%s.%N)
    wall=$(python3 - <<PY
import decimal
print(float(decimal.Decimal("$t1") - decimal.Decimal("$t0")))
PY
)

    if [[ -f "$gt" ]]; then
        python3 "${WS}/scripts/evaluate_trajectory.py" \
            "${out}/vio.csv" "$gt" "${out}/eval" --no-plot --run-name "$name" || true
    fi
    python3 "${WS}/scripts/sem_filter_metrics.py" --run-dir "$out" --json "${out}/sem_filter_metrics.json" \
        2>/dev/null || true

    cat > "${out}/run_config.json" <<EOF
{
  "name": "${name}",
  "method": "${method}",
  "mode": "${mode}",
  "bag": "${bag}",
  "bag_rate": ${rate},
  "process_every_n": ${every_n},
  "use_yolo": ${use_yolo},
  "wall_play_s": ${wall},
  "yolo_device": "${YOLO_DEVICE}"
}
EOF

    python3 "${WS}/scripts/summarize_realtime_benchmark.py" --run-dir "$out"
}

# VIODE city_day/3_high — primary dynamic scene (~20 Hz cam, ~66 s)
run_case "viode_3high_baseline_r1.0" baseline stereo_imu "$VIODE_BAG" "$VIODE_GT" 0 1.0 1 0
run_case "viode_3high_sgta_r0.5_n1" sgta stereo_imu_sgta "$VIODE_BAG" "$VIODE_GT" 0 0.5 1 1
run_case "viode_3high_sgta_r1.0_n1" sgta stereo_imu_sgta "$VIODE_BAG" "$VIODE_GT" 0 1.0 1 1
run_case "viode_3high_sgta_r1.0_n2" sgta stereo_imu_sgta "$VIODE_BAG" "$VIODE_GT" 0 1.0 2 1
run_case "viode_3high_sad_sem_r1.0_n1" sad_sem stereo_imu_sem "$VIODE_BAG" "$VIODE_GT" 0 1.0 1 1

# EuRoC MH_01_easy — static safety at full bag rate (~20 Hz)
run_euroc_case() {
    local name="$1" method="$2" mode="$3" rate="$4" every_n="$5" use_yolo="$6"
    local out="${ROOT}/${name}"
    mkdir -p "$out"
    local cfg
    cfg="$(prepare_euroc_cfg "$method" "$out" "$mode" "$name")"
    export SAD_BAG_RATE="$rate"
    export YOLO_PROCESS_EVERY_N="$every_n"
    echo ""
    echo "========== ${name} =========="
    echo "method=${method} bag_rate=${rate} yolo_every_n=${every_n} use_yolo=${use_yolo}"
    local t0 t1 wall
    t0=$(date +%s.%N)
    run_sad_vio_benchmark "$cfg" "$out" "$EUROC_BAG" "$EUROC_START" "$rate" "$use_yolo"
    t1=$(date +%s.%N)
    wall=$(python3 - <<PY
import decimal
print(float(decimal.Decimal("$t1") - decimal.Decimal("$t0")))
PY
)
    if [[ -f "$EUROC_GT" ]]; then
        python3 "${WS}/scripts/evaluate_trajectory.py" \
            "${out}/vio.csv" "$EUROC_GT" "${out}/eval" --no-plot --run-name "$name" || true
    fi
    python3 "${WS}/scripts/sem_filter_metrics.py" --run-dir "$out" --json "${out}/sem_filter_metrics.json" \
        2>/dev/null || true
    cat > "${out}/run_config.json" <<EOF
{
  "name": "${name}",
  "method": "${method}",
  "mode": "${mode}",
  "bag": "${EUROC_BAG}",
  "bag_rate": ${rate},
  "process_every_n": ${every_n},
  "use_yolo": ${use_yolo},
  "wall_play_s": ${wall},
  "yolo_device": "${YOLO_DEVICE}"
}
EOF
    python3 "${WS}/scripts/summarize_realtime_benchmark.py" --run-dir "$out"
}

run_euroc_case "euroc_mh01_baseline_r1.0" baseline stereo_imu 1.0 1 0
run_euroc_case "euroc_mh01_sgta_r1.0_n1" sgta stereo_imu_sgta 1.0 1 1
run_euroc_case "euroc_mh01_sgta_r1.0_n2" sgta stereo_imu_sgta 1.0 2 1

echo ""
echo "========== SUMMARY =========="
python3 "${WS}/scripts/summarize_realtime_benchmark.py" \
    --root "$ROOT" \
    --json-out "${ROOT}/realtime_summary.json" | tee "${ROOT}/realtime_summary.txt"
