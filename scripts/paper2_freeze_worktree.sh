#!/usr/bin/env bash
# Prepare a clean Paper #2 (GeoDF-Weighted / RA-L) worktree so Paper #3 or
# ongoing edits in the current checkout cannot contaminate the rebuild/submission build.
#
# Default action is safe: create/check a detached worktree at the frozen Paper #2
# commit and print its status. Pass --build / --benchmark explicitly for heavier actions.
#
# Usage:
#   bash scripts/paper2_freeze_worktree.sh [--build] [--benchmark N] [--euroc]
#
# Environment:
#   PAPER2_REF       frozen ref/commit (default: see PAPER2_FREEZE below)
#   PAPER2_WORKTREE  output worktree path (default: ../ws_vins_ros2_paper2_freeze)
#   FORCE            forwarded to benchmark scripts (default: 0)
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
# Update this hash after each Paper #2 freeze commit (same pattern as Paper #1).
PAPER2_FREEZE="${PAPER2_FREEZE:-PLACEHOLDER_UPDATE_AFTER_FREEZE_COMMIT}"
if [ "$PAPER2_FREEZE" = "PLACEHOLDER_UPDATE_AFTER_FREEZE_COMMIT" ]; then
    PAPER2_REF="${PAPER2_REF:-paper/geodf-weighted-dynamic-2026-q4}"
else
    PAPER2_REF="${PAPER2_REF:-$PAPER2_FREEZE}"
fi
WORKTREE="${PAPER2_WORKTREE:-$(dirname "$ROOT")/ws_vins_ros2_paper2_freeze}"
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
        echo "ERROR: Paper #2 freeze worktree is dirty: $WORKTREE" >&2
        echo "Commit/stash its changes or choose PAPER2_WORKTREE=/path/to/new/worktree." >&2
        exit 1
    fi
    git -C "$WORKTREE" checkout --detach "$RESOLVED_REF"
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
    echo "ERROR: freeze worktree at $ACTUAL, expected $RESOLVED_REF" >&2
    exit 1
fi

echo "[paper2-freeze] worktree: $WORKTREE"
echo "[paper2-freeze] commit:   $ACTUAL"
echo "[paper2-freeze] subject:  $(git -C "$WORKTREE" log -1 --format=%s)"
echo "[paper2-freeze] status:   clean"

if [ "$DO_BUILD" = "1" ]; then
    echo "[paper2-freeze] building dependencies up to pht_vio_ros from frozen worktree"
    (cd "$WORKTREE" && set +u && source /opt/ros/humble/setup.bash && set -u && \
        colcon build --packages-up-to pht_vio_ros --cmake-args -DCMAKE_BUILD_TYPE=Release)
fi

if [ -n "$BENCHMARK_N" ]; then
    echo "[paper2-freeze] running Paper #2 VIODE N=$BENCHMARK_N benchmark (FORCE=${FORCE:-0})"
    (cd "$WORKTREE" && FORCE="${FORCE:-0}" bash scripts/run_geodf_weighted_n5.sh "$BENCHMARK_N")
    if [ "$DO_EUROC" = "1" ]; then
        echo "[paper2-freeze] running Paper #2 EuRoC N=$BENCHMARK_N benchmark (FORCE=${FORCE:-0})"
        (cd "$WORKTREE" && FORCE="${FORCE:-0}" bash scripts/run_geodf_euroc_weighted.sh "$BENCHMARK_N")
    fi
fi
