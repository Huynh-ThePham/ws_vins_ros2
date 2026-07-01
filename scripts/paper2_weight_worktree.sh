#!/usr/bin/env bash
# Prepare a clean GeoDF-Weighted worktree isolated from other method branches
# in the shared repository.
#
# Default action is safe: create/check a worktree at the weighted branch tip and
# print its status. Pass --build / --benchmark explicitly for heavier actions.
#
# Usage:
#   bash scripts/paper2_weight_worktree.sh [--build] [--benchmark N] [--euroc]
#
# Environment:
#   PAPER2_REF       branch or commit (default: paper/geodf-weighted-vins-2026-q4)
#   PAPER2_WORKTREE  output worktree path (default: ../ws_vins_ros2_paper2_weight)
#   FORCE            forwarded to benchmark scripts (default: 0)
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
PAPER2_REF="${PAPER2_REF:-paper/geodf-weighted-vins-2026-q4}"
WORKTREE="${PAPER2_WORKTREE:-$(dirname "$ROOT")/ws_vins_ros2_paper2_weight}"
DO_BUILD=0
DO_EUROC=0
BENCHMARK_N=""

while [ "$#" -gt 0 ]; do
    case "$1" in
        --build)
            DO_BUILD=1
            shift
            ;;
        --benchmark)
            BENCHMARK_N="${2:?missing N after --benchmark}"
            shift 2
            ;;
        --euroc)
            DO_EUROC=1
            shift
            ;;
        -h|--help)
            sed -n '1,32p' "$0"
            exit 0
            ;;
        *)
            echo "Unknown argument: $1" >&2
            exit 2
            ;;
    esac
done

RESOLVED_REF="$(git -C "$ROOT" rev-parse "$PAPER2_REF")"

if [ -e "$WORKTREE" ] && git -C "$WORKTREE" rev-parse --is-inside-work-tree >/dev/null 2>&1; then
    if [ -n "$(git -C "$WORKTREE" status --porcelain)" ]; then
        echo "ERROR: GeoDF-Weighted worktree is dirty: $WORKTREE" >&2
        echo "Commit/stash its changes or choose PAPER2_WORKTREE=/path/to/new/worktree." >&2
        exit 1
    fi
    if git -C "$ROOT" show-ref --verify --quiet "refs/heads/${PAPER2_REF#origin/}"; then
        git -C "$WORKTREE" checkout "$PAPER2_REF"
    else
        git -C "$WORKTREE" checkout --detach "$RESOLVED_REF"
    fi
elif [ ! -e "$WORKTREE" ]; then
    mkdir -p "$(dirname "$WORKTREE")"
    git -C "$ROOT" worktree add --detach "$WORKTREE" "$RESOLVED_REF"
else
    echo "ERROR: path exists but is not a git worktree: $WORKTREE" >&2
    echo "Choose a different PAPER2_WORKTREE or remove the path manually." >&2
    exit 1
fi

ACTUAL="$(git -C "$WORKTREE" rev-parse HEAD)"
if [ "$ACTUAL" != "$RESOLVED_REF" ]; then
    echo "ERROR: weight worktree at $ACTUAL, expected $RESOLVED_REF" >&2
    exit 1
fi

echo "[paper2-weight] worktree: $WORKTREE"
echo "[paper2-weight] commit:   $ACTUAL"
echo "[paper2-weight] subject:  $(git -C "$WORKTREE" log -1 --format=%s)"
echo "[paper2-weight] status:   clean"

if [ "$DO_BUILD" = "1" ]; then
    echo "[paper2-weight] building dependencies up to pht_vio_ros from weight worktree"
    (cd "$WORKTREE" && set +u && source /opt/ros/humble/setup.bash && set -u && \
        colcon build --packages-up-to pht_vio_ros --cmake-args -DCMAKE_BUILD_TYPE=Release)
fi

if [ -n "$BENCHMARK_N" ]; then
    echo "[paper2-weight] running GeoDF-Weighted VIODE N=$BENCHMARK_N benchmark (FORCE=${FORCE:-0})"
    (cd "$WORKTREE" && FORCE="${FORCE:-0}" bash scripts/run_geodf_weighted_n5.sh "$BENCHMARK_N")
    if [ "$DO_EUROC" = "1" ]; then
        echo "[paper2-weight] running GeoDF-Weighted EuRoC N=$BENCHMARK_N benchmark (FORCE=${FORCE:-0})"
        (cd "$WORKTREE" && FORCE="${FORCE:-0}" bash scripts/run_geodf_euroc_weighted.sh "$BENCHMARK_N")
    fi
fi
