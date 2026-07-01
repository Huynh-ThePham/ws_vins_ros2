#!/usr/bin/env bash
# Shared SAD-VINS benchmark helpers (ROS 2 native). Source, do not execute.

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

sad_method_to_mode() {
    case "$1" in
        baseline) echo stereo_imu ;;
        sad_sem|semantic|sad) echo stereo_imu_sem ;;
        sequential|sem_geodf_seq) echo stereo_imu_sem_geodf_sequential ;;
        sem_geodf|fusion) echo stereo_imu_sem_geodf ;;
        sem_geodf_mask_gated|mask_gated) echo stereo_imu_sem_geodf_mask_gated ;;
        *)
            echo "Unknown SAD method: $1 (baseline|sad_sem|sem_geodf|sequential|sem_geodf_mask_gated)" >&2
            return 1
            ;;
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

yaml_image0_topic() {
    local cfg="$1"
    python3 - "$cfg" <<'PY'
import re, sys
text = open(sys.argv[1]).read()
m = re.search(r'image0_topic:\s*"([^"]+)"', text)
print(m.group(1) if m else "/cam0/image_raw")
PY
}

source_ros2_ws() {
    local ws="${1:?}"
    # shellcheck disable=SC1091
    source /opt/ros/humble/setup.bash
    # shellcheck disable=SC1091
    source "${ws}/install/setup.bash"
}

run_sad_vio_benchmark() {
    local cfg="$1" out="$2" bag="$3" start="${4:-0}" rate="${5:-1.0}" use_yolo="${6:-0}"
    local image_topic yolo_device="${YOLO_DEVICE:-cuda}"
    image_topic="$(yaml_image0_topic "$cfg")"

    if [ -n "${SAD_BAG_RATE:-}" ]; then
        rate="${SAD_BAG_RATE}"
    elif [ "$use_yolo" = "1" ]; then
        # YOLO segmentation (~15 ms/frame) cannot keep up with 1.0x on long VIODE bags.
        rate="${SAD_YOLO_BAG_RATE:-0.5}"
    fi

    mkdir -p "$out"
    rm -f "${out}/vio.csv" "${out}/sem_stats.csv" \
          "${out}/pht_vio_node.log" "${out}/yolo_mask_node.log"

    local yolo_pid=""
    if [ "$use_yolo" = "1" ]; then
        ros2 run yolo_dynamic_mask mask_node --ros-args \
            -p use_sim_time:=true \
            -p image_topic:="${image_topic}" \
            -p mask_topic:=/dynamic_mask \
            -p device:="${yolo_device}" \
            > "${out}/yolo_mask_node.log" 2>&1 &
        yolo_pid=$!
        sleep 15
        if ! kill -0 "$yolo_pid" 2>/dev/null; then
            echo "yolo mask_node failed:" >&2
            tail -40 "${out}/yolo_mask_node.log" >&2
            return 1
        fi
    fi

    ros2 run pht_vio_ros pht_vio_node "$cfg" --ros-args -p use_sim_time:=true \
        > "${out}/pht_vio_node.log" 2>&1 &
    local pid=$!
    sleep 5
    if ! kill -0 "$pid" 2>/dev/null; then
        echo "pht_vio_node failed:" >&2
        tail -30 "${out}/pht_vio_node.log" >&2
        [ -n "$yolo_pid" ] && kill "$yolo_pid" 2>/dev/null || true
        return 1
    fi

    local play_args=(--clock --rate "$rate" --disable-keyboard-controls)
    if [ "$start" != "0" ] && [ -n "$start" ]; then
        play_args+=(--start-offset "$start")
    fi
    ros2 bag play "$bag" "${play_args[@]}"

    sleep 8
    if [ "$use_yolo" = "1" ]; then
        # Drain remaining mask/image pairs after bag ends.
        sleep 20
    fi
    kill "$pid" 2>/dev/null || true
    [ -n "$yolo_pid" ] && kill "$yolo_pid" 2>/dev/null || true
    wait "$pid" 2>/dev/null || true
    [ -n "$yolo_pid" ] && wait "$yolo_pid" 2>/dev/null || true

    if [ ! -s "${out}/vio.csv" ]; then
        echo "ERROR: empty trajectory ${out}/vio.csv" >&2
        tail -20 "${out}/pht_vio_node.log" >&2
        [ -f "${out}/yolo_mask_node.log" ] && tail -20 "${out}/yolo_mask_node.log" >&2
        return 1
    fi
}
