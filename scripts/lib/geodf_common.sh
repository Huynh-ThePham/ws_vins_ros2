#!/usr/bin/env bash
# Shared GeoDF benchmark helpers (ROS 2 native). Source, do not execute.

resolve_euroc_root() {
    if [ -n "${EUROC_ROOT:-}" ] && [ -d "${EUROC_ROOT}/machine_hall" ]; then
        echo "$EUROC_ROOT"
        return 0
    fi
    local candidate
    for candidate in \
        "/media/theph/Data1/Research/Datasets/EuRoC" \
        "/media/theph/Data1/ws_research_datasets/raw_datasets/euroc" \
        "/media/theph/Data/ws_research_datasets/raw_datasets/euroc" \
        "/media/theph/Data/ws_research_datasets/euroc"; do
        if [ -d "${candidate}/machine_hall" ]; then
            echo "$candidate"
            return 0
        fi
    done
    return 1
}

resolve_viode_root() {
    if [ -n "${VIODE_ROOT:-}" ] && [ -d "${VIODE_ROOT}/${VIODE_ENV:-city_day}" ]; then
        echo "$VIODE_ROOT"
        return 0
    fi
    local candidate env="${VIODE_ENV:-city_day}"
    for candidate in \
        "/media/theph/Data1/Research/Datasets/Viode" \
        "/media/theph/Data1/ws_research_datasets/viode" \
        "/media/theph/Data/ws_research_datasets/viode"; do
        if [ -d "${candidate}/${env}" ]; then
            echo "$candidate"
            return 0
        fi
    done
    return 1
}

euroc_group_for_seq() {
    case "$1" in
        MH_*) echo machine_hall ;;
        V1_*) echo vicon_room1 ;;
        V2_*) echo vicon_room2 ;;
        *) return 1 ;;
    esac
}

euroc_bag_start_s() {
    case "$1" in
        MH_01_easy) echo 40 ;;
        MH_02_easy) echo 35 ;;
        MH_03_medium) echo 17.5 ;;
        MH_04_difficult|MH_05_difficult) echo 15 ;;
        *) echo 0 ;;
    esac
}

geodf_method_to_mode() {
    case "$1" in
        baseline) echo stereo_imu ;;
        geodf|geodf_hard) echo stereo_imu_geodf ;;
        geodf_dump|alwayson) echo stereo_imu_geodf_dump ;;
        # PROPOSED (Hướng A): scene-aware + auto-ρ_on (B), stereo OFF
        adaptive|proposed|geodf_adaptive) echo stereo_imu_geodf_adaptive ;;
        # PROPOSED (Paper #2): IMU/geometry residual -> backend visual weight, no hard delete
        weighted|geodf_weighted|soft_weight|proposed2|proposed2_weighted) echo stereo_imu_geodf_weighted ;;
        # Ablation: fixed ρ_on=0.12 (oracle / dataset-tuned)
        adaptive_fixed|adaptive_v1|fixed_rho) echo stereo_imu_geodf_adaptive_fixed ;;
        adaptive_v2) echo stereo_imu_geodf_adaptive_v2 ;;
        adaptiveB|adaptive_b) echo stereo_imu_geodf_adaptive ;;  # same as proposed
        geodf_dump_v2|alwayson_v2) echo stereo_imu_geodf_dump_v2 ;;
        geodf_noguard) echo stereo_imu_geodf_noguard ;;
        *)
            echo "Unknown GeoDF method: $1 (baseline|geodf_hard|alwayson|adaptive|adaptive_fixed|weighted|geodf_noguard)" >&2
            return 1
            ;;
    esac
}

geodf_config_subdir() {
    case "$1" in
        viode|VIODE) echo viode ;;
        *) echo euroc ;;
    esac
}

start_tag() {
    local start="$1"
    if [[ "$start" == *.* ]]; then
        echo "${start//./p}"
    else
        echo "$start"
    fi
}

resolve_viode_ros2_bag() {
    local bag_ros1="$1"
    local ws="${2:?}"
    local level
    level="$(basename "$(dirname "$bag_ros1")")/$(basename "$bag_ros1" .bag)"
    echo "${ws}/data/viode_ros2/${level}/ros2_bag"
}

resolve_euroc_ros2_bag() {
    local seq="$1"
    local ws="${2:?}"
    echo "${ws}/data/euroc_ros2/${seq}/ros2_bag"
}

source_ros2_ws() {
    local ws="${1:?}"
    # shellcheck disable=SC1091
    source /opt/ros/humble/setup.bash
    # shellcheck disable=SC1091
    source "${ws}/install/setup.bash"
}

run_pht_vio_benchmark() {
    local cfg="$1" out="$2" bag="$3" start="${4:-0}" rate="${5:-1.0}"
    mkdir -p "$out"
    rm -f "${out}/vio.csv" "${out}/geo_df_stats.csv" "${out}/geo_df_features.csv" \
          "${out}/pht_vio_node.log"

    ros2 run pht_vio_ros pht_vio_node "$cfg" --ros-args -p use_sim_time:=true \
        > "${out}/pht_vio_node.log" 2>&1 &
    local pid=$!
    sleep 5
    if ! kill -0 "$pid" 2>/dev/null; then
        echo "pht_vio_node failed:" >&2
        tail -30 "${out}/pht_vio_node.log" >&2
        return 1
    fi

    local play_args=(--clock --rate "$rate" --disable-keyboard-controls)
    if [ "$start" != "0" ] && [ -n "$start" ]; then
        play_args+=(--start-offset "$start")
    fi
    ros2 bag play "$bag" "${play_args[@]}"

    sleep 8
    kill "$pid" 2>/dev/null || true
    wait "$pid" 2>/dev/null || true

    if [ ! -s "${out}/vio.csv" ]; then
        echo "ERROR: empty trajectory ${out}/vio.csv" >&2
        tail -20 "${out}/pht_vio_node.log" >&2
        return 1
    fi
}
