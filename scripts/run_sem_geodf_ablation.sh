#!/usr/bin/env bash
# Semantic–GeoDF publication ablation on a FIXED common backbone (plan P0.1–P0.5).
#
# Every method is produced as: dataset common base + method overlay -> resolved_config.yaml
# The estimator is given THAT file. Legacy per-method YAMLs under src/config/{viode,euroc}/
# are never read on the publication path.
#
# Usage:
#   ./scripts/run_sem_geodf_ablation.sh [quick|full]
#
# Env:
#   N=3                  repeat trials (default 1 for quick, 3 for full)
#   METHODS="..."        default full: baseline geodf semantic union_noweight union_weight
#   EUROC_SEQS / VIODE_LEVELS / VIODE_ENV
#   PUBLICATION_MODE=1   fail-closed (default for full); disables ALLOW_MISSING_DATA etc.
#   SKIP_EUROC=1 / SKIP_VIODE=1   explicit dataset opt-out only
#   PROTOCOL_TAG=sem-geodf-fair-v2
#   FORCE=1              re-run even if reusable
#   CLAIM_MATRIX=fixed_backbone_main|adaptive_extension
set -euo pipefail

WS="$(cd "$(dirname "$0")/.." && pwd)"
# shellcheck source=scripts/lib/geodf_common.sh
source "${WS}/scripts/lib/geodf_common.sh"
# shellcheck source=scripts/lib/sad_common.sh
source "${WS}/scripts/lib/sad_common.sh"

SCOPE="${1:-quick}"
mkdir -p "${WS}/logs" "${WS}/results/sem_geodf_ablation"

PAPER_CFG="${WS}/src/config/paper"
OVERLAY_DIR="${PAPER_CFG}/overlays"
PROTOCOL_VERSION="${PROTOCOL_VERSION:-sem-geodf-fair-v2}"
CLAIM_MATRIX="${CLAIM_MATRIX:-fixed_backbone_main}"

# Phase 3.2: factor adaptation is either off for every method, or on for every
# method. Never on for "ours" alone (see audit_method_config_diff.py).
#   ADAPTATION_MODE=off      — paper default (visual_adaptive_quality=0)
#   ADAPTATION_MODE=visual   — visual_adaptive_quality=1 for every method
#   ADAPTATION_MODE=full     — visual + imu adaptation for every method
ADAPTATION_MODE="${ADAPTATION_MODE:-off}"
ADAPTATION_SETS=()
case "$ADAPTATION_MODE" in
    off) ;;
    visual)
        ADAPTATION_SETS+=(--set "visual_adaptive_quality=1")
        ;;
    full)
        ADAPTATION_SETS+=(--set "visual_adaptive_quality=1")
        ADAPTATION_SETS+=(--set "imu_adaptive_covariance=1")
        ;;
    *)
        echo "[fatal] ADAPTATION_MODE must be off|visual|full, got '$ADAPTATION_MODE'" >&2
        exit 2
        ;;
esac

if [ "$SCOPE" = "full" ]; then
    N="${N:-3}"
    METHODS="${METHODS:-baseline geodf semantic union_noweight union_weight}"
    EUROC_SEQS="${EUROC_SEQS:-MH_01_easy MH_02_easy MH_03_medium MH_04_difficult MH_05_difficult}"
    VIODE_LEVELS="${VIODE_LEVELS:-0_none 1_low 2_mid 3_high}"
    PUBLICATION_MODE="${PUBLICATION_MODE:-1}"
else
    N="${N:-1}"
    METHODS="${METHODS:-baseline geodf semantic union_noweight union_weight}"
    EUROC_SEQS="${EUROC_SEQS:-MH_03_medium}"
    VIODE_LEVELS="${VIODE_LEVELS:-2_mid 3_high}"
    PUBLICATION_MODE="${PUBLICATION_MODE:-0}"
fi

VIODE_ENV="${VIODE_ENV:-city_day}"
export YOLO_DEVICE="${YOLO_DEVICE:-cuda}"
export YOLO_MODEL="${YOLO_MODEL:-${WS}/models/$(python3 -c "import json; print(json.load(open('${WS}/models/model_manifest.json'))['file'])" 2>/dev/null || echo yolo11n-seg.pt)}"
export YOLO_MODEL_MANIFEST="${YOLO_MODEL_MANIFEST:-${WS}/models/model_manifest.json}"

if [ "${EUROC_SEQS:-}" = "__none__" ]; then
    EUROC_SEQS=""
fi
if [ "${VIODE_LEVELS:-}" = "__none__" ]; then
    VIODE_LEVELS=""
fi
if [ "${SKIP_EUROC:-0}" = "1" ]; then
    EUROC_SEQS=""
fi
if [ "${SKIP_VIODE:-0}" = "1" ]; then
    VIODE_LEVELS=""
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
PROTOCOL_TAG="${PROTOCOL_TAG:-${PROTOCOL_VERSION}}"

if [ "${PUBLICATION_MODE}" = "1" ]; then
    # Fail-closed: no partial-run escapes.
    ALLOW_MISSING_DATA=0
    ORACLE_ABLATION=0
    SEM_POLICY_VIODE_LEVEL_OVERRIDE=0
    export ALLOW_MISSING_DATA ORACLE_ABLATION SEM_POLICY_VIODE_LEVEL_OVERRIDE
    export REQUIRE_VERIFIED_MODEL=1
    if [ "$CLAIM_MATRIX" = "fixed_backbone_main" ]; then
        for m in $METHODS; do
            if [ "$m" = "full_adaptive" ]; then
                echo "[fatal] full_adaptive belongs in the adaptive-extension matrix, not the fixed-backbone main claim. Use scripts/run_sem_geodf_adaptive_extension.sh" >&2
                exit 2
            fi
        done
        for required in baseline geodf semantic union_noweight union_weight; do
            found=0
            for m in $METHODS; do
                [ "$m" = "$required" ] && found=1 && break
            done
            if [ "$found" != "1" ]; then
                echo "[fatal] fixed-backbone main matrix is missing method '$required'" >&2
                exit 2
            fi
        done
    fi
fi

if [ "${SEM_POLICY_VIODE_LEVEL_OVERRIDE:-0}" = "1" ] && [ "${ORACLE_ABLATION:-0}" != "1" ]; then
    echo "[error] SEM_POLICY_VIODE_LEVEL_OVERRIDE=1 requires ORACLE_ABLATION=1" >&2
    exit 1
fi

mkdir -p "${WS}/results/sem_geodf_ablation/${PROTOCOL_TAG}/euroc" \
         "${WS}/results/sem_geodf_ablation/${PROTOCOL_TAG}/viode"

source_ros2_ws "$WS"

# --- dataset roots: fatal when required (P0.3) ---------------------------------
EUROC=""
if [ -n "${EUROC_SEQS:-}" ]; then
    if ! EUROC="$(resolve_euroc_root)"; then
        echo "[fatal] EuRoC root unresolved but EUROC_SEQS is set: ${EUROC_SEQS}" >&2
        echo "        Set EUROC_ROOT, or SKIP_EUROC=1 to opt out explicitly." >&2
        exit 21
    fi
fi
VIODE=""
if [ -n "${VIODE_LEVELS:-}" ]; then
    if ! VIODE="$(resolve_viode_root)"; then
        echo "[fatal] VIODE root unresolved but VIODE_LEVELS is set: ${VIODE_LEVELS}" >&2
        echo "        Set VIODE_ROOT, or SKIP_VIODE=1 to opt out explicitly." >&2
        echo "        Stub fallback is disabled in publication mode." >&2
        exit 22
    fi
fi

# Map publication method names (and a few legacy aliases) onto paper overlays.
normalize_method() {
    case "$1" in
        baseline) echo baseline ;;
        geodf|adaptive|geodf_adaptive) echo geodf ;;
        semantic|sad_sem) echo semantic ;;
        union_noweight|sem_geodf_noweight) echo union_noweight ;;
        union_weight|sem_geodf) echo union_weight ;;
        full_adaptive) echo full_adaptive ;;
        adaptive_arbitration) echo adaptive_arbitration ;;
        *) echo "Unknown publication method: $1" >&2; return 1 ;;
    esac
}

method_needs_yolo() {
    case "$(normalize_method "$1")" in
        semantic|union_noweight|union_weight|full_adaptive|adaptive_arbitration) return 0 ;;
        *) return 1 ;;
    esac
}

paper_base_for_dataset() {
    case "$1" in
        euroc) echo "${PAPER_CFG}/euroc_common.yaml" ;;
        viode) echo "${PAPER_CFG}/viode_common.yaml" ;;
        *) return 1 ;;
    esac
}

overlay_for_method() {
    local method
    method="$(normalize_method "$1")"
    local path="${OVERLAY_DIR}/${method}.yaml"
    if [ ! -f "$path" ]; then
        echo "[fatal] missing overlay $path" >&2
        return 1
    fi
    echo "$path"
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
    local dst src
    dst="$(resolve_euroc_ros2_bag "$seq" "$WS")"
    mkdir -p "$(dirname "$dst")"
    [ -f "${dst}/metadata.yaml" ] && return 0
    # Search env-driven and workspace-relative locations only (no personal hardcoding
    # as the primary protocol path).
    for src in \
        "${EUROC_ROS2_ROOT:-}/${seq}/ros2_bag" \
        "${WS}/data/euroc_ros2/${seq}/ros2_bag"; do
        if [ -n "${src}" ] && [ -f "${src}/metadata.yaml" ]; then
            ln -sfn "$src" "$dst"
            return 0
        fi
    done
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
    local alt="${WS}/data/viode_ros2/${VIODE_ENV}/${level}/ros2_bag"
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

paired_seed() {
    echo $(( 1000 + $1 ))
}

write_run_manifest() {
    local out="$1" dataset="$2" scene="$3" method="$4" trial="$5" rate="$6" yolo="$7" status="$8" cfg="$9" bag="${10}"
    local policy_level="${11:--1}" oracle="${12:-0}" gt="${13:-}"
    local manifest_status="$status"
    local reason="${failure_reason:-}"
    case "$status" in
        ok) manifest_status=ok ;;
        *)  manifest_status=failed
            reason="${reason:-$status}" ;;
    esac
    local extra=()
    if [ -n "$gt" ]; then
        extra+=(--gt "$gt")
    fi
    python3 "${WS}/scripts/write_run_manifest.py" \
        --out-dir "$out" --dataset "$dataset" --scene "$scene" --method "$method" \
        --trial "$trial" --bag-rate "$rate" --yolo "$yolo" --status "$manifest_status" \
        --failure-reason "$reason" \
        --config "$cfg" --bag "$bag" --ws "$WS" \
        --seed "$(paired_seed "$trial")" \
        --model "${YOLO_MODEL:-}" \
        --model-manifest "${YOLO_MODEL_MANIFEST}" \
        --protocol-fair "$PROTOCOL_FAIR" --oracle-ablation "$oracle" \
        --sem-policy-dynamic-level "$policy_level" \
        --sem-policy-params-file "${SEM_POLICY_PARAMS_FILE:-}" \
        --protocol-tag "$PROTOCOL_TAG" \
        --protocol-version "$PROTOCOL_VERSION" \
        "${extra[@]}"
}

apply_sem_policy_params_if_needed() {
    local method="$1" cfg="$2"
    method="$(normalize_method "$method")"
    if [ -z "${SEM_POLICY_PARAMS_FILE:-}" ]; then
        return 0
    fi
    case "$method" in
        union_noweight|union_weight|full_adaptive|adaptive_arbitration) ;;
        *) return 0 ;;
    esac
    if [ ! -f "$SEM_POLICY_PARAMS_FILE" ]; then
        echo "[error] SEM_POLICY_PARAMS_FILE not found: $SEM_POLICY_PARAMS_FILE" >&2
        exit 1
    fi
    local audit_extra=()
    if [ "${ALLOW_INCOMPLETE_TRAIN:-0}" = "1" ] && [ "${PUBLICATION_MODE}" != "1" ]; then
        audit_extra+=(--allow-draft-selected)
    fi
    python3 "${WS}/scripts/audit_sem_geodf_protocol.py" \
        --quiet \
        --configs "$cfg" \
        --selected-params "$SEM_POLICY_PARAMS_FILE" \
        "${audit_extra[@]}"
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

# Generate + audit the exact config the node will receive (P0.1).
prepare_resolved_config() {
    local dataset="$1" method="$2" out="$3"
    local base overlay resolved cam_src
    method="$(normalize_method "$method")"
    base="$(paper_base_for_dataset "$dataset")"
    overlay="$(overlay_for_method "$method")"
    resolved="${out}/resolved_config.yaml"
    mkdir -p "$out"

    python3 "${WS}/scripts/generate_paper_config.py" \
        --base "$base" \
        --overlay "$overlay" \
        --out "$resolved" \
        --set "output_path=\"${out}/\"" \
        --set "pose_graph_save_path=\"${out}/pose_graph/\"" \
        "${ADAPTATION_SETS[@]}" \
        >&2

    # Camera calib paths in the YAML are relative to the config file directory
    # (see parameters.cpp: configPath + "/" + cam0_calib). Copy the dataset
    # intrinsics next to resolved_config.yaml so the node can load them.
    case "$dataset" in
        euroc) cam_src="${WS}/src/config/euroc" ;;
        viode) cam_src="${WS}/src/config/viode" ;;
        *) echo "[fatal] unknown dataset for camera calib copy: $dataset" >&2; exit 3 ;;
    esac
    local cam0 cam1
    cam0="$(grep -E '^cam0_calib:' "$resolved" | head -1 | sed -E 's/^cam0_calib:[[:space:]]*"?([^"]+)"?.*/\1/')"
    cam1="$(grep -E '^cam1_calib:' "$resolved" | head -1 | sed -E 's/^cam1_calib:[[:space:]]*"?([^"]+)"?.*/\1/')"
    if [ -z "$cam0" ] || [ -z "$cam1" ]; then
        echo "[fatal] resolved config missing cam0_calib/cam1_calib" >&2
        exit 3
    fi
    if [ ! -f "${cam_src}/${cam0}" ] || [ ! -f "${cam_src}/${cam1}" ]; then
        echo "[fatal] camera calib missing under ${cam_src}: ${cam0} ${cam1}" >&2
        exit 3
    fi
    cp -a "${cam_src}/${cam0}" "${out}/${cam0}"
    cp -a "${cam_src}/${cam1}" "${out}/${cam1}"

    apply_sem_policy_params_if_needed "$method" "$resolved"

    local protocol_extra=()
    if [ "${ORACLE_ABLATION:-0}" = "1" ]; then
        protocol_extra+=(--allow-oracle)
    fi
    python3 "${WS}/scripts/audit_sem_geodf_protocol.py" --quiet --configs "$resolved" \
        "${protocol_extra[@]}" >&2

    # Reject legacy frame-hold as the sole publication policy timer.
    if grep -qE '^sem_policy_assist_hold_s:\s*0(\.0+)?\s*$' "$resolved" && \
       grep -qE '^sem_policy_hold_frames:' "$resolved"; then
        echo "[fatal] publication config has assist_hold_s=0; frame-based hold is forbidden" >&2
        exit 3
    fi

    echo "$resolved"
}

euroc_bag_ready() {
    local seq="$1" bag
    link_euroc_ros2_bag "$seq" || return 1
    bag="$(resolve_euroc_ros2_bag "$seq" "$WS")"
    [ -f "${bag}/metadata.yaml" ]
}

may_reuse_run() {
    local out="$1" dataset="$2" scene="$3" method="$4" trial="$5"
    local cfg_sha="" yolo_args=() tmp_resolved
    method="$(normalize_method "$method")"
    if [ "${FORCE:-0}" = "1" ]; then
        return 1
    fi
    if [ ! -f "${out}/eval/metrics.json" ] || [ ! -f "${out}/run_manifest.json" ]; then
        return 1
    fi
    # Expected config identity = current base+overlay (not whatever happened to be on disk).
    tmp_resolved="$(mktemp --suffix=.yaml)"
    python3 "${WS}/scripts/generate_paper_config.py" \
        --base "$(paper_base_for_dataset "$dataset")" \
        --overlay "$(overlay_for_method "$method")" \
        --out "$tmp_resolved" \
        --set "output_path=\"${out}/\"" \
        --set "pose_graph_save_path=\"${out}/pose_graph/\"" \
        "${ADAPTATION_SETS[@]}" \
        >/dev/null
    cfg_sha="$(python3 -c "import hashlib,sys; print(hashlib.sha256(open(sys.argv[1],'rb').read()).hexdigest())" "$tmp_resolved")"
    rm -f "$tmp_resolved" "${tmp_resolved}.provenance.json"
    if method_needs_yolo "$method"; then
        yolo_args+=(--require-semantic-model)
        if [ -f "${YOLO_MODEL_MANIFEST}" ]; then
            local msha
            msha="$(python3 -c "import hashlib,sys; print(hashlib.sha256(open(sys.argv[1],'rb').read()).hexdigest())" "${YOLO_MODEL_MANIFEST}")"
            yolo_args+=(--expected-model-manifest-sha256 "$msha")
        fi
    fi
    python3 "${WS}/scripts/validate_reusable_run.py" \
        --run-dir "$out" \
        --ws "$WS" \
        --expected-method "$method" \
        --expected-dataset "$dataset" \
        --expected-scene "$scene" \
        --expected-trial "$trial" \
        --expected-seed "$(paired_seed "$trial")" \
        --expected-config-sha256 "$cfg_sha" \
        --expected-protocol-tag "$PROTOCOL_TAG" \
        --expected-protocol-version "$PROTOCOL_VERSION" \
        "${yolo_args[@]}"
}

run_one() {
    local dataset="$1" scene="$2" method_raw="$3" trial="$4"
    local method out run_cfg rate use_yolo=0 status=ok
    method="$(normalize_method "$method_raw")"
    method_needs_yolo "$method" && use_yolo=1
    rate="$BAG_RATE_NON_YOLO"
    [ "$use_yolo" = "1" ] && rate="${SAD_YOLO_BAG_RATE}"
    failure_reason=""

    if [ "$dataset" = "euroc" ]; then
        local seq="$scene" group gt bag start tag
        if ! link_euroc_ros2_bag "$seq"; then
            echo "[fatal] required EuRoC ros2 bag missing for ${seq}" >&2
            exit 20
        fi
        group="$(euroc_group_for_seq "$seq")"
        gt="${EUROC}/${group}/${seq}/${seq}/mav0/state_groundtruth_estimate0/data.csv"
        bag="$(resolve_euroc_ros2_bag "$seq" "$WS")"
        start="$(euroc_bag_start_s "$seq")"
        tag="$(start_tag "$start")"
        out="${WS}/results/sem_geodf_ablation/${PROTOCOL_TAG}/euroc/${seq}_${method}_t${trial}_s${tag}"

        if may_reuse_run "$out" euroc "$seq" "$method" "$trial"; then
            echo "[reuse] euroc ${seq} ${method} t${trial}"
            return 0
        fi

        run_cfg="$(prepare_resolved_config euroc "$method" "$out")"
        echo "=== EuRoC $seq $method trial=$trial rate=$rate yolo=$use_yolo cfg=$run_cfg ==="
        killall -9 pht_vio_node mask_node 2>/dev/null || true
        sleep 1
        if [ ! -f "$gt" ]; then
            echo "[fatal] missing EuRoC GT: $gt" >&2
            status=failed
            failure_reason=missing_ground_truth
            write_run_manifest "$out" "euroc" "$seq" "$method" "$trial" "$rate" "$use_yolo" "$status" "$run_cfg" "$bag" \
                "$(read_sem_policy_level "$run_cfg")" "0" "$gt"
            [ "${PUBLICATION_MODE}" = "1" ] && exit 23
            return 23
        fi
        if [ "$use_yolo" = "1" ]; then
            run_sad_vio_benchmark "$run_cfg" "$out" "$bag" "$start" "$rate" 1 || status=benchmark_failed
        else
            run_pht_vio_benchmark "$run_cfg" "$out" "$bag" "$start" "$rate" || status=benchmark_failed
        fi
        if [ "$status" = "ok" ]; then
            python3 "${WS}/scripts/evaluate_trajectory.py" \
                "${out}/vio.csv" "$gt" "${out}/eval" \
                --no-plot --run-name "${seq}_${method}_t${trial}" || status=eval_failed
        fi
        write_run_manifest "$out" "euroc" "$seq" "$method" "$trial" "$rate" "$use_yolo" "$status" "$run_cfg" "$bag" \
            "$(read_sem_policy_level "$run_cfg")" "0" "$gt"
        if [ "$status" != "ok" ] && [ "${PUBLICATION_MODE}" = "1" ]; then
            echo "[fatal] EuRoC cell failed: ${seq}/${method}/t${trial} status=$status" >&2
            exit 24
        fi
    else
        local level="$scene"
        local bag_ros2 gt_path run_name
        local oracle_flag=0
        local policy_level=-1
        bag_ros2="$(viode_ros2_bag_path "$level")" || {
            echo "[fatal] required VIODE ros2 bag missing for ${VIODE_ENV}/${level}" >&2
            exit 20
        }
        run_name="${VIODE_ENV}_${level}_${method}_t${trial}"
        out="${WS}/results/sem_geodf_ablation/${PROTOCOL_TAG}/viode/${run_name}"

        if may_reuse_run "$out" viode "${VIODE_ENV}_${level}" "$method" "$trial"; then
            echo "[reuse] viode ${run_name}"
            return 0
        fi

        run_cfg="$(prepare_resolved_config viode "$method" "$out")"
        if [ "$method" = "union_weight" ] && [ "${SEM_POLICY_VIODE_LEVEL_OVERRIDE:-0}" = "1" ]; then
            policy_level="$(viode_sem_policy_level "$level")"
            # Oracle only outside publication mode; PUBLICATION_MODE already forces override off.
            sed -i "s|^sem_policy_dynamic_level:.*|sem_policy_dynamic_level: ${policy_level}|" "$run_cfg"
            oracle_flag=1
            python3 "${WS}/scripts/audit_sem_geodf_protocol.py" --quiet --configs "$run_cfg" --allow-oracle
        fi
        echo "=== VIODE $run_name rate=$rate yolo=$use_yolo cfg=$run_cfg bag=$bag_ros2 ==="
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
            echo "[fatal] no ground truth for ${VIODE_ENV}/${level}; ATE cannot be computed" >&2
            status=failed
            failure_reason=missing_ground_truth
        fi
        write_run_manifest "$out" "viode" "${VIODE_ENV}_${level}" "$method" "$trial" "$rate" "$use_yolo" "$status" "$run_cfg" "$bag_ros2" \
            "$policy_level" "$oracle_flag" "${gt_path:-}"
        if [ "$status" != "ok" ] && [ "${PUBLICATION_MODE}" = "1" ]; then
            echo "[fatal] VIODE cell failed: ${run_name} status=$status" >&2
            exit 24
        fi
    fi
}

echo "[ablation] scope=$SCOPE N=$N methods=$METHODS publication_mode=$PUBLICATION_MODE"
echo "[ablation] EUROC_SEQS=$EUROC_SEQS VIODE_LEVELS=$VIODE_LEVELS VIODE_ENV=$VIODE_ENV"
echo "[ablation] protocol_tag=$PROTOCOL_TAG protocol_version=$PROTOCOL_VERSION claim=$CLAIM_MATRIX"
echo "[ablation] adaptation_mode=$ADAPTATION_MODE"
echo "[ablation] bag_rate=$BAG_RATE_NON_YOLO fair=$PROTOCOL_FAIR"

# Preflight: fair overlays must audit clean before any cell runs.
python3 "${WS}/scripts/audit_method_config_diff.py" --base "${PAPER_CFG}/viode_common.yaml"
python3 "${WS}/scripts/audit_method_config_diff.py" --base "${PAPER_CFG}/euroc_common.yaml"

for trial in $(seq 1 "$N"); do
    if [ -n "$EUROC" ]; then
        for seq in $EUROC_SEQS; do
            if ! euroc_bag_ready "$seq"; then
                echo "[fatal] required EuRoC ros2 bag missing for ${seq}" >&2
                echo "        mount the dataset or run scripts/euroc_prepare.sh ${seq}" >&2
                exit 20
            fi
            for method in $METHODS; do
                start="$(euroc_bag_start_s "$seq")"
                tag="$(start_tag "$start")"
                method_n="$(normalize_method "$method")"
                out="${WS}/results/sem_geodf_ablation/${PROTOCOL_TAG}/euroc/${seq}_${method_n}_t${trial}_s${tag}"
                if may_reuse_run "$out" euroc "$seq" "$method_n" "$trial"; then
                    echo "[have] euroc ${seq} ${method_n} t${trial}"
                    continue
                fi
                run_one euroc "$seq" "$method" "$trial"
            done
        done
    fi
    if [ -n "$VIODE" ]; then
        for level in $VIODE_LEVELS; do
            for method in $METHODS; do
                method_n="$(normalize_method "$method")"
                out="${WS}/results/sem_geodf_ablation/${PROTOCOL_TAG}/viode/${VIODE_ENV}_${level}_${method_n}_t${trial}"
                if may_reuse_run "$out" viode "${VIODE_ENV}_${level}" "$method_n" "$trial"; then
                    echo "[have] viode ${VIODE_ENV}_${level} ${method_n} t${trial}"
                    continue
                fi
                run_one viode "$level" "$method" "$trial"
            done
        done
    fi
done

ABLATION_ROOT="${WS}/results/sem_geodf_ablation/${PROTOCOL_TAG}"
mkdir -p "$ABLATION_ROOT"

# Summaries are diagnostic only here; publication assets require validation receipt.
python3 "${WS}/scripts/summarize_sem_geodf_ablation.py" \
    --root "$ABLATION_ROOT" \
    --out "${ABLATION_ROOT}/ABLATION_SUMMARY.md"

python3 "${WS}/scripts/export_ablation_analysis.py" \
    --root "$ABLATION_ROOT" \
    --out-csv "${ABLATION_ROOT}/ABLATION_ANALYSIS.csv" \
    --out-md "${ABLATION_ROOT}/ABLATION_ANALYSIS.md"

if [ "${PUBLICATION_MODE}" = "1" ]; then
    echo "[ablation] publication mode: summaries written; full-rerun must validate before [done]"
else
    echo "[done] ${ABLATION_ROOT}/ABLATION_SUMMARY.md"
    echo "[done] ${ABLATION_ROOT}/ABLATION_ANALYSIS.csv"
fi
