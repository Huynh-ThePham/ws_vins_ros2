#!/usr/bin/env bash
# Deprecated: use scripts/paper2_weight_worktree.sh (ws_vins_ros2_paper2_weight).
echo "NOTE: paper2_freeze_worktree.sh is deprecated; use paper2_weight_worktree.sh" >&2
exec "$(dirname "$0")/paper2_weight_worktree.sh" "$@"
