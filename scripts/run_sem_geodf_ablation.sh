#!/usr/bin/env bash
# Full Semantic–GeoDF ablation (6 modes, EuRoC + VIODE, optional N repeats).
#
# Usage:
#   ./scripts/run_sem_geodf_ablation.sh [quick|full]
#
# Env:
#   N=3                  repeat trials (default 1 for quick, 3 for full)
#   METHODS="..."         override method list
#   EUROC_SEQS="..."      default quick: MH_03_medium; full: 5×MH
#   VIODE_LEVELS="..."    default quick: 2_mid 3_high; full: 0_none..3_high
#   VIODE_ENV=city_day|city_night|parking_lot
#   FAIR_BAG_RATE=1       published default: same bag rate for all methods (see SAD_BAG_RATE)
#   SAD_BAG_RATE=1.0      bag rate when FAIR_BAG_RATE=1 (default 1.0)
#   SEM_POLICY_PARAMS_FILE=...  apply train-selected semantic policy params to sem_geodf only
#   SEM_POLICY_VIODE_LEVEL_OVERRIDE=1  oracle only; requires ORACLE_ABLATION=1
#   ORACLE_ABLATION=1     allow labelled VIODE policy override runs
#   PROTOCOL_TAG=fair1p0  optional suffix for versioned result dirs
#   FORCE=1               re-run existing
set -eo pipefail

WS="$(cd "$(dirname "$0")/.." && pwd)"
# shellcheck source=scripts/lib/geodf_common.sh
source "${WS}/scripts/lib/geodf_common.sh"
# shellcheck source=scripts/lib/sad_common.sh
source "${WS}/scripts/lib/sad_common.sh"

SCOPE="${1:-quick}"
mkdir -p "${WS}/logs" "${WS}/results/sem_geodf_ablation"

if [ "$SCOPE" = "full" ]; then
    N="${N:-3}"
    METHODS="${METHODS:-baseline adaptive sad_sem sequential sem_geodf sem_geodf_mask_gated}"
    EUROC_SEQS="${EUROC_SEQS:-MH_01_easy MH_02_easy MH_03_medium MH_04_difficult MH_05_difficult}"
    VIODE_LEVELS="${VIODE_LEVELS:-0_none 1_low 2_mid 3_high}"
else
    N="${N:-1}"
    METHODS="${METHODS:-baseline adaptive sad_sem sequential sem_geodf}"
    EUROC_SEQS="${EUROC_SEQS:-MH_03_medium}"
    VIODE_LEVELS="${VIODE_LEVELS:-2_mid 3_high}"
fi

VIODE_ENV="${VIODE_ENV:-city_day}"
export YOLO_DEVICE="${YOLO_DEVICE:-cuda}"
if [ "${EUROC_SEQS:-}" = "__none__" ]; then
    EUROC_SEQS=""
fi
if [ "${VIODE_LEVELS:-}" = "__none__" ]; then
    VIODE_LEVELS=""
fi
if [ "${SKIP_EUROC:-0}" = "1" ]; then
    EUROC_SEQS=""
fi

# Published protocol: equal bag rate for YOLO and non-YOLO paths unless explicitly disabled.
if [ "${FAIR_BAG_RATE:-1}" = "1" ]; then
    export SAD_YOLO_BAG_RATE="${SAD_BAG_RATE:-1.0}"
    BAG_RATE_NON_YOLO="${SAD_YOLO_BAG_RATE}"
    PROTOCOL_FAIR=1
else
    export SAD_YOLO_BAG_RATE="${SAD_YOLO_BAG_RATE:-0.5}"
    BAG_RATE_NON_YOLO="1.0"
    PROTOCOL_FAIR=0
fi
PROTOCOL_TAG="${PROTOCOL_TAG:-fair${SAD_YOLO_BAG_RATE//./p}}"

if [ "${SEM_POLICY_VIODE_LEVEL_OVERRIDE:-0}" = "1" ] && [ "${ORACLE_ABLATION:-0}" != "1" ]; then
    echo "[error] SEM_POLICY_VIODE_LEVEL_OVERRIDE=1 requires ORACLE_ABLATION=1" >&2
    exit 1
fi
mkdir -p "${WS}/results/sem_geodf_ablation/${PROTOCOL_TAG}/euroc" \
         "${WS}/results/sem_geodf_ablation/${PROTOCOL_TAG}/viode"

source_ros2_ws "$WS"
EUROC=""
if [ "${SKIP_EUROC:-0}" != "1" ] && [ -n "${EUROC_SEQS:-}" ]; then
    EUROC="$(resolve_euroc_root)" || EUROC=""
fi
VIODE=""
if [ -n "${VIODE_LEVELS:-}" ]; then
    VIODE="$(resolve_viode_root)" || VIODE=""
    if [ -z "$VIODE" ] && [ -d "${WS}/data/viode_stub/${VIODE_ENV}" ]; then
        VIODE="${WS}/data/viode_stub"
    fi
fi

method_needs_yolo() {
    case "$1" in
        sad_sem|sequential|sem_geodf|sem_geodf_mask_gated) return 0 ;;
        *) return 1 ;;
    esac
}

resolve_mode() {
    local method="$1"
    case "$method" in
        baseline) echo stereo_imu ;;
        adaptive) geodf_method_to_mode adaptive ;;
        sad_sem) sad_method_to_mode sad_sem ;;
        sequential) geodf_method_to_mode sequential ;;
        sem_geodf) geodf_method_to_mode sem_geodf ;;
        sem_geodf_mask_gated) geodf_method_to_mode sem_geodf_mask_gated ;;
        *) echo "Unknown method: $method" >&2; return 1 ;;
    esac
}

viode_sem_policy_level() {
    case "$1" in
        0_none|1_low) echo 0 ;;
        2_mid) echo 1 ;;
        3_high) echo 2 ;;
        *) echo -1 ;;
    esac
}

link_euroc_ros2_bag() {
    local seq="$1"
    local dst
    dst="$(resolve_euroc_ros2_bag "$seq" "$WS")"
    mkdir -p "$(dirname "$dst")"
    [ -f "${dst}/metadata.yaml" ] && return 0
    local src="/media/theph/Data1/Research/raw_datasets/euroc/machine_hall/${seq}/ros2_bag"
    if [ -f "${src}/metadata.yaml" ]; then
        ln -sfn "$src" "$dst"
        return 0
    fi
    bash "${WS}/scripts/euroc_prepare.sh" "$seq"
}

viode_ros2_bag_path() {
    local level="$1"
    local bag_ros1="${VIODE}/${VIODE_ENV}/${level}.bag"
    local dst
    dst="$(resolve_viode_ros2_bag "$bag_ros1" "$WS")"
    mkdir -p "$(dirname "$dst")"
    if [ -f "${dst}/metadata.yaml" ]; then
        echo "$dst"
        return 0
    fi
    local alt="/home/theph/ws_vins_ros2/data/viode_ros2/${VIODE_ENV}/${level}/ros2_bag"
    if [ -f "${alt}/metadata.yaml" ]; then
        ln -sfn "$alt" "$dst"
        echo "$dst"
        return 0
    fi
    if [ -f "$bag_ros1" ]; then
        bash "${WS}/scripts/viode_prepare_ros2_bag.sh" "$bag_ros1" "$dst"
        echo "$dst"
        return 0
    fi
    return 1
}

link_viode_ros2_bag() {
    viode_ros2_bag_path "$1" >/dev/null
}

viode_gt_path() {
    local level="$1"
    local bag_ros1="${VIODE}/${VIODE_ENV}/${level}.bag"
    local cached="${WS}/data/viode_gt/${VIODE_ENV}/${level}/gt_odometry.csv"
    if [ -f "$cached" ]; then
        echo "$cached"
        return 0
    fi
    if [ -f "$bag_ros1" ]; then
        mkdir -p "$(dirname "$cached")"
        python3 "${WS}/scripts/viode_dump_gt.py" --bag "$bag_ros1" --out "$cached"
        echo "$cached"
        return 0
    fi
    return 1
}

write_run_manifest() {
    local out="$1" dataset="$2" scene="$3" method="$4" trial="$5" rate="$6" yolo="$7" status="$8" cfg="$9" bag="${10}"
    local policy_level="${11:--1}" oracle="${12:-0}"
    python3 "${WS}/scripts/write_run_manifest.py" \
        --out-dir "$out" --dataset "$dataset" --scene "$scene" --method "$method" \
        --trial "$trial" --bag-rate "$rate" --yolo "$yolo" --status "$status" \
        --config "$cfg" --bag "$bag" --ws "$WS" \
        --protocol-fair "$PROTOCOL_FAIR" --oracle-ablation "$oracle" \
        --sem-policy-dynamic-level "$policy_level" \
        --sem-policy-params-file "${SEM_POLICY_PARAMS_FILE:-}"
}

apply_sem_policy_params_if_needed() {
    local method="$1" cfg="$2"
    if [ -z "${SEM_POLICY_PARAMS_FILE:-}" ]; then
        return 0
    fi
    if [ "$method" != "sem_geodf" ]; then
        return 0
    fi
    if [ ! -f "$SEM_POLICY_PARAMS_FILE" ]; then
        echo "[error] SEM_POLICY_PARAMS_FILE not found: $SEM_POLICY_PARAMS_FILE" >&2
        exit 1
    fi
    python3 "${WS}/scripts/apply_sem_policy_params.py" \
        --config "$cfg" \
        --params "$SEM_POLICY_PARAMS_FILE"
}

read_sem_policy_level() {
    local cfg="$1"
    if [ ! -f "$cfg" ]; then
        echo "-1"
        return
    fi
    local v
    v="$(grep -E '^sem_policy_dynamic_level:' "$cfg" | awk '{print $2}')"
    echo "${v:--1}"
}

euroc_bag_ready() {
    local seq="$1" bag
    link_euroc_ros2_bag "$seq" || return 1
    bag="$(resolve_euroc_ros2_bag "$seq" "$WS")"
    [ -f "${bag}/metadata.yaml" ]
}

run_one() {
    local dataset="$1" scene="$2" method="$3" trial="$4"
    local mode out run_cfg rate use_yolo=0
    mode="$(resolve_mode "$method")"
    method_needs_yolo "$method" && use_yolo=1
    rate="$BAG_RATE_NON_YOLO"
    [ "$use_yolo" = "1" ] && rate="${SAD_YOLO_BAG_RATE}"

    if [ "$dataset" = "euroc" ]; then
        local seq="$scene" group gt bag start tag
        link_euroc_ros2_bag "$seq"
        group="$(euroc_group_for_seq "$seq")"
        gt="${EUROC}/${group}/${seq}/${seq}/mav0/state_groundtruth_estimate0/data.csv"
        bag="$(resolve_euroc_ros2_bag "$seq" "$WS")"
        start="$(euroc_bag_start_s "$seq")"
        tag="$(start_tag "$start")"
        out="${WS}/results/sem_geodf_ablation/${PROTOCOL_TAG}/euroc/${seq}_${method}_t${trial}_s${tag}"
        EUROC_CFG="$(ros2 pkg prefix pht_vio_ros)/share/pht_vio_ros/config/euroc"
        run_cfg="${EUROC_CFG}/euroc_${mode}_config_run_${seq}_t${trial}.yaml"
        mkdir -p "$out"
        cp "${EUROC_CFG}/euroc_${mode}_config.yaml" "$run_cfg"
        sed -i "s|output_path: \"~/output/\"|output_path: \"${out}/\"|" "$run_cfg"
        sed -i "s|pose_graph_save_path: \"~/output/pose_graph/\"|pose_graph_save_path: \"${out}/pose_graph/\"|" "$run_cfg"
        apply_sem_policy_params_if_needed "$method" "$run_cfg"
        echo "=== EuRoC $seq $method trial=$trial rate=$rate yolo=$use_yolo ==="
        killall -9 pht_vio_node mask_node 2>/dev/null || true
        sleep 1
        if [ "$use_yolo" = "1" ]; then
            run_sad_vio_benchmark "$run_cfg" "$out" "$bag" "$start" "$rate" 1
        else
            run_pht_vio_benchmark "$run_cfg" "$out" "$bag" "$start" "$rate"
        fi
        python3 "${WS}/scripts/evaluate_trajectory.py" \
            "${out}/vio.csv" "$gt" "${out}/eval" \
            --no-plot --run-name "${seq}_${method}_t${trial}"
        write_run_manifest "$out" "euroc" "$seq" "$method" "$trial" "$rate" "$use_yolo" "ok" "$run_cfg" "$bag" \
            "$(read_sem_policy_level "$run_cfg")" "0"
    else
        local level="$scene"
        local bag_ros1="${VIODE}/${VIODE_ENV}/${level}.bag"
        local bag_ros2 gt_path run_name status=ok
        bag_ros2="$(viode_ros2_bag_path "$level")" || {
            echo "[skip] no VIODE ros2 bag for ${VIODE_ENV}/${level}"
            return 0
        }
        run_name="${VIODE_ENV}_${level}_${method}_t${trial}"
        out="${WS}/results/sem_geodf_ablation/${PROTOCOL_TAG}/viode/${run_name}"
        local oracle_flag=0
        local policy_level=-1
        VIODE_CFG="$(ros2 pkg prefix pht_vio_ros)/share/pht_vio_ros/config/viode"
        run_cfg="${VIODE_CFG}/viode_${mode}_config_run_${level}_t${trial}.yaml"
        mkdir -p "$out"
        cp "${VIODE_CFG}/viode_${mode}_config.yaml" "$run_cfg"
        sed -i "s|output_path: \"~/output/\"|output_path: \"${out}/\"|" "$run_cfg"
        sed -i "s|pose_graph_save_path: \"~/output/pose_graph/\"|pose_graph_save_path: \"${out}/pose_graph/\"|" "$run_cfg"
        apply_sem_policy_params_if_needed "$method" "$run_cfg"
        if [ "$method" = "sem_geodf" ] && [ "${SEM_POLICY_VIODE_LEVEL_OVERRIDE:-0}" = "1" ]; then
            policy_level="$(viode_sem_policy_level "$level")"
            sed -i "s|^sem_policy_dynamic_level:.*|sem_policy_dynamic_level: ${policy_level}|" "$run_cfg"
            oracle_flag=1
        fi
        echo "=== VIODE $run_name rate=$rate yolo=$use_yolo bag=$bag_ros2 ==="
        killall -9 pht_vio_node mask_node 2>/dev/null || true
        sleep 1
        if [ "$use_yolo" = "1" ]; then
            run_sad_vio_benchmark "$run_cfg" "$out" "$bag_ros2" 0 "$rate" 1 || status=benchmark_failed
        else
            run_pht_vio_benchmark "$run_cfg" "$out" "$bag_ros2" 0 "$rate" || status=benchmark_failed
        fi
        if gt_path="$(viode_gt_path "$level")"; then
            cp -a "$gt_path" "${out}/gt_odometry.csv"
            if [ "$status" = "ok" ]; then
                python3 "${WS}/scripts/evaluate_trajectory.py" \
                    "${out}/vio.csv" "${out}/gt_odometry.csv" "${out}/eval" \
                    --no-plot --run-name "$run_name" || status=eval_failed
            fi
        else
            echo "[warn] no GT for ${VIODE_ENV}/${level}; trajectory+stats saved, skip ATE" >&2
            status=no_gt
        fi
        write_run_manifest "$out" "viode" "${VIODE_ENV}_${level}" "$method" "$trial" "$rate" "$use_yolo" "$status" "$run_cfg" "$bag_ros2" \
            "$policy_level" "$oracle_flag"
    fi
}

echo "[ablation] scope=$SCOPE N=$N methods=$METHODS"
echo "[ablation] EUROC_SEQS=$EUROC_SEQS VIODE_LEVELS=$VIODE_LEVELS VIODE_ENV=$VIODE_ENV"
echo "[ablation] protocol_tag=$PROTOCOL_TAG bag_rate=$BAG_RATE_NON_YOLO fair=$PROTOCOL_FAIR oracle_override=${SEM_POLICY_VIODE_LEVEL_OVERRIDE:-0}"

for trial in $(seq 1 "$N"); do
    if [ -n "$EUROC" ]; then
        for seq in $EUROC_SEQS; do
            if ! euroc_bag_ready "$seq"; then
                echo "[skip] EuRoC ${seq}: ros2 bag unavailable (mount dataset or run euroc_prepare.sh)"
                continue
            fi
            for method in $METHODS; do
                out_glob="${WS}/results/sem_geodf_ablation/${PROTOCOL_TAG}/euroc/${seq}_${method}_t${trial}_*"
                if [ "${FORCE:-0}" != "1" ] && compgen -G "${out_glob}/eval/metrics.json" > /dev/null; then
                    echo "[have] euroc ${seq} ${method} t${trial}"
                    continue
                fi
                run_one euroc "$seq" "$method" "$trial"
            done
        done
    fi
    if [ -n "$VIODE" ]; then
        for level in $VIODE_LEVELS; do
            for method in $METHODS; do
                out="${WS}/results/sem_geodf_ablation/${PROTOCOL_TAG}/viode/${VIODE_ENV}_${level}_${method}_t${trial}"
                if [ "${FORCE:-0}" != "1" ] && [ -f "${out}/eval/metrics.json" ]; then
                    echo "[have] viode ${VIODE_ENV}_${level} ${method} t${trial}"
                    continue
                fi
                run_one viode "$level" "$method" "$trial"
            done
        done
    fi
done

ABLATION_ROOT="${WS}/results/sem_geodf_ablation/${PROTOCOL_TAG}"
mkdir -p "$ABLATION_ROOT"

python3 "${WS}/scripts/summarize_sem_geodf_ablation.py" \
    --root "$ABLATION_ROOT" \
    --out "${ABLATION_ROOT}/ABLATION_SUMMARY.md"

python3 "${WS}/scripts/export_ablation_analysis.py" \
    --root "$ABLATION_ROOT" \
    --out-csv "${ABLATION_ROOT}/ABLATION_ANALYSIS.csv" \
    --out-md "${ABLATION_ROOT}/ABLATION_ANALYSIS.md"

echo "[done] ${ABLATION_ROOT}/ABLATION_SUMMARY.md"
echo "[done] ${ABLATION_ROOT}/ABLATION_ANALYSIS.csv"
