#!/usr/bin/env bash
# Prepare a clean GeoDF-Adaptive (AECE) worktree isolated from other method
# branches in the shared repository.
#
# Default action is safe: create/check a worktree at the adaptive branch tip and
# print its status. Pass --build / --benchmark / --docx explicitly for heavier
# actions.
#
# Usage:
#   bash scripts/paper1_adaptive_worktree.sh [--build] [--benchmark N] [--docx]
#
# Environment:
#   PAPER1_REF       branch or commit (default: paper/geodf-adaptive-vins-2026)
#   PAPER1_WORKTREE  output worktree path (default: ../ws_vins_ros2_paper1_adaptive)
#   FORCE            forwarded to benchmark scripts (default: 0)
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
PAPER1_REF="${PAPER1_REF:-paper/geodf-adaptive-vins-2026}"
WORKTREE="${PAPER1_WORKTREE:-$(dirname "$ROOT")/ws_vins_ros2_paper1_adaptive}"
DO_BUILD=0
DO_DOCX=0
BENCHMARK_N=""

while [ "$#" -gt 0 ]; do
    case "$1" in
        --build)
            DO_BUILD=1
            shift
            ;;
        --docx)
            DO_DOCX=1
            shift
            ;;
        --benchmark)
            BENCHMARK_N="${2:?missing N after --benchmark}"
            shift 2
            ;;
        -h|--help)
            sed -n '1,28p' "$0"
            exit 0
            ;;
        *)
            echo "Unknown argument: $1" >&2
            exit 2
            ;;
    esac
done

RESOLVED_REF="$(git -C "$ROOT" rev-parse "$PAPER1_REF")"

if [ -e "$WORKTREE" ] && git -C "$WORKTREE" rev-parse --is-inside-work-tree >/dev/null 2>&1; then
    if [ -n "$(git -C "$WORKTREE" status --porcelain)" ]; then
        echo "ERROR: GeoDF-Adaptive worktree is dirty: $WORKTREE" >&2
        echo "Commit/stash its changes or choose PAPER1_WORKTREE=/path/to/new/worktree." >&2
        exit 1
    fi
    if git -C "$ROOT" show-ref --verify --quiet "refs/heads/${PAPER1_REF#origin/}"; then
        git -C "$WORKTREE" checkout "$PAPER1_REF"
    else
        git -C "$WORKTREE" checkout --detach "$RESOLVED_REF"
    fi
elif [ ! -e "$WORKTREE" ]; then
    mkdir -p "$(dirname "$WORKTREE")"
    git -C "$ROOT" worktree add --detach "$WORKTREE" "$RESOLVED_REF"
else
    echo "ERROR: path exists but is not a git worktree: $WORKTREE" >&2
    echo "Choose a different PAPER1_WORKTREE or remove the path manually." >&2
    exit 1
fi

ACTUAL="$(git -C "$WORKTREE" rev-parse HEAD)"
if [ "$ACTUAL" != "$RESOLVED_REF" ]; then
    echo "ERROR: adaptive worktree at $ACTUAL, expected $RESOLVED_REF" >&2
    exit 1
fi

echo "[paper1-adaptive] worktree: $WORKTREE"
echo "[paper1-adaptive] commit:   $ACTUAL"
echo "[paper1-adaptive] subject:  $(git -C "$WORKTREE" log -1 --format=%s)"
echo "[paper1-adaptive] status:   clean"

if [ "$DO_BUILD" = "1" ]; then
    echo "[paper1-adaptive] building dependencies up to pht_vio_ros from adaptive worktree"
    (cd "$WORKTREE" && set +u && source /opt/ros/humble/setup.bash && set -u && \
        colcon build --packages-up-to pht_vio_ros --cmake-args -DCMAKE_BUILD_TYPE=Release)
fi

if [ -n "$BENCHMARK_N" ]; then
    echo "[paper1-adaptive] running GeoDF-Adaptive N=$BENCHMARK_N benchmark (FORCE=${FORCE:-0})"
    (cd "$WORKTREE" && FORCE="${FORCE:-0}" bash scripts/run_geodf_n5_final.sh "$BENCHMARK_N")
fi

if [ "$DO_DOCX" = "1" ]; then
    echo "[paper1-adaptive] building AECE docx from adaptive worktree"
    (cd "$WORKTREE" && bash scripts/build_manuscript_docx.sh)
fi
