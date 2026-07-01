#!/usr/bin/env bash
# Deprecated: use scripts/paper1_adaptive_worktree.sh (ws_vins_ros2_paper1_adaptive).
echo "NOTE: paper1_freeze_worktree.sh is deprecated; use paper1_adaptive_worktree.sh" >&2
exec "$(dirname "$0")/paper1_adaptive_worktree.sh" "$@"
