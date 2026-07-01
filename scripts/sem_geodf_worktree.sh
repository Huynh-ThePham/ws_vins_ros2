#!/usr/bin/env bash
# Prepare a clean Sem-GeoDF worktree isolated from other method branches in the
# shared repository.
#
# Default action is safe: create/check a worktree at the Sem-GeoDF branch tip and
# print its status. Pass --build / --benchmark explicitly for heavier actions.
#
# Usage:
#   bash scripts/sem_geodf_worktree.sh [--build] [--benchmark quick|full]
#
# Environment:
#   SEM_GEODF_REF       branch or commit (default: paper/sem-geodf-vins-2026)
#   SEM_GEODF_WORKTREE  output worktree path (default: ../ws_vins_ros2_sem_geodf)
#   FORCE               forwarded to benchmark scripts (default: 0)
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
SEM_GEODF_REF="${SEM_GEODF_REF:-paper/sem-geodf-vins-2026}"
WORKTREE="${SEM_GEODF_WORKTREE:-$(dirname "$ROOT")/ws_vins_ros2_sem_geodf}"
DO_BUILD=0
BENCHMARK_MODE=""

while [ "$#" -gt 0 ]; do
    case "$1" in
        --build)
            DO_BUILD=1
            shift
            ;;
        --benchmark)
            BENCHMARK_MODE="${2:?missing mode after --benchmark (quick|full)}"
            shift 2
            ;;
        -h|--help)
            sed -n '1,24p' "$0"
            exit 0
            ;;
        *)
            echo "Unknown argument: $1" >&2
            exit 2
            ;;
    esac
done

RESOLVED_REF="$(git -C "$ROOT" rev-parse "$SEM_GEODF_REF")"

if [ -e "$WORKTREE" ] && git -C "$WORKTREE" rev-parse --is-inside-work-tree >/dev/null 2>&1; then
    if [ -n "$(git -C "$WORKTREE" status --porcelain)" ]; then
        echo "ERROR: Sem-GeoDF worktree is dirty: $WORKTREE" >&2
        echo "Commit/stash its changes or choose SEM_GEODF_WORKTREE=/path/to/new/worktree." >&2
        exit 1
    fi
    if git -C "$ROOT" show-ref --verify --quiet "refs/heads/${SEM_GEODF_REF#origin/}"; then
        git -C "$WORKTREE" checkout "$SEM_GEODF_REF"
    else
        git -C "$WORKTREE" checkout --detach "$RESOLVED_REF"
    fi
elif [ ! -e "$WORKTREE" ]; then
    mkdir -p "$(dirname "$WORKTREE")"
    git -C "$ROOT" worktree add --detach "$WORKTREE" "$RESOLVED_REF"
else
    echo "ERROR: path exists but is not a git worktree: $WORKTREE" >&2
    echo "Choose a different SEM_GEODF_WORKTREE or remove the path manually." >&2
    exit 1
fi

ACTUAL="$(git -C "$WORKTREE" rev-parse HEAD)"
if [ "$ACTUAL" != "$RESOLVED_REF" ]; then
    echo "ERROR: Sem-GeoDF worktree at $ACTUAL, expected $RESOLVED_REF" >&2
    exit 1
fi

echo "[sem-geodf] worktree: $WORKTREE"
echo "[sem-geodf] commit:   $ACTUAL"
echo "[sem-geodf] subject:  $(git -C "$WORKTREE" log -1 --format=%s)"
echo "[sem-geodf] status:   clean"

if [ "$DO_BUILD" = "1" ]; then
    echo "[sem-geodf] building dependencies up to pht_vio_ros + yolo_dynamic_mask"
    (cd "$WORKTREE" && set +u && source /opt/ros/humble/setup.bash && set -u && \
        colcon build --symlink-install --packages-up-to pht_vio_ros yolo_dynamic_mask \
            --cmake-args -DCMAKE_BUILD_TYPE=Release)
fi

if [ -n "$BENCHMARK_MODE" ]; then
    echo "[sem-geodf] running ablation ($BENCHMARK_MODE, FORCE=${FORCE:-0})"
    (cd "$WORKTREE" && FORCE="${FORCE:-0}" bash scripts/run_sem_geodf_ablation.sh "$BENCHMARK_MODE")
fi
