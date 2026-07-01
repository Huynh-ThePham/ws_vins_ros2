# GeoDF-Weighted evaluation — PENDING

This directory holds the paper evaluation artifacts for the **GeoDF-Weighted**
method (branch `paper/geodf-weighted-vins-2026`).

**Status: no weighted results generated yet.** The previous contents were
GeoDF-Adaptive (paper #1 / AECE) artifacts inherited from the fork and have been
removed so this branch only carries its own method's data. No numbers are
reported here until the weighted benchmark has actually been run.

Do not hand-write result numbers into this folder or the docs — every table and
figure below must be produced by the scripts from real runs.

## How to generate the weighted-specific artifacts

```bash
# 0. Build the weighted binaries
source /opt/ros/${ROS_DISTRO}/setup.bash
colcon build --symlink-install
source install/setup.bash

# 1. VIODE real-dynamic, N=5 (baseline vs weighted, all 12 conditions)
export VIODE_ROOT=/path/to/viode
FORCE=1 bash scripts/run_geodf_weighted_n5.sh 5

# 2. EuRoC static-safety regression (baseline vs weighted)
FORCE=1 bash scripts/run_geodf_euroc_weighted.sh 5

# 3. (optional) VIODE mask-level detection for weighted predictions
python3 scripts/eval_viode_detection.py \
  --root results/viode --mask-root results/viode/masks \
  --env parking_lot --levels "3_high" \
  --method weighted --prediction weight --weight-threshold 0.999
```

Per-trial outputs land under `results/viode_repeat/`. Once real runs exist,
summarize them into this folder and only then update the numbers in
`README.md` and `docs/PROPOSAL_GeoDF-Weighted.md`.
