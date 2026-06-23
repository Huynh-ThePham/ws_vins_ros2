#!/usr/bin/env bash
# Source only this workspace (avoids stale CMAKE_PREFIX_PATH from old package names).
set -eo pipefail
_SCRIPT="${BASH_SOURCE[0]:-$0}"
WS="$(cd "$(dirname "$_SCRIPT")/.." && pwd)"
ROS_DISTRO="${ROS_DISTRO:-humble}"
source "/opt/ros/${ROS_DISTRO}/setup.bash"
# Drop legacy overlay paths from renamed packages (vins, loop_fusion, …).
if [ -n "${CMAKE_PREFIX_PATH:-}" ]; then
  _clean=()
  IFS=':' read -ra _parts <<< "${CMAKE_PREFIX_PATH}"
  for _p in "${_parts[@]}"; do
    case "$_p" in
      *"/install/vins"|*"/install/loop_fusion"|*"/install/vins_core"|*"/install/vins_common"|*"/install/camera_models"|*"/install/global_fusion"|*"/install/vins_ros_common"|*"/install/loop_fusion_core"|*"/install/global_fusion_core") continue ;;
    esac
    _clean+=("$_p")
  done
  export CMAKE_PREFIX_PATH="$(IFS=:; echo "${_clean[*]}")"
fi
source "${WS}/install/setup.bash"
echo "[pht_slam] sourced ${WS} (ROS ${ROS_DISTRO})"
