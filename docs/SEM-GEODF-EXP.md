# Semantic–GeoDF fusion experiment (`exp/sem-geodf-gated`)

**Worktree:** `/home/theph/ws_vins_ros2_sem_geodf`  
**Base:** `paper/geodf-adaptive-vins-2026-q4` + cherry-picked SAD-VINS semantic front-end  
**Frozen SAD reference:** tag `paper-sad-vins-2026-q1-freeze` on `/home/theph/ws_vins_ros2_sadvins`

## Current state

Both subsystems are present but **not yet fused**:

- `geodf_*` — GeoDF-Adaptive geometry filter (Paper #1)
- `sem_*` + `yolo_dynamic_mask` — SAD-VINS semantic mask

When both are enabled in YAML, filters run sequentially (geo then sem). Next step: implement `rejectSemGeoFused()` with scene gating.

## Build

```bash
cd /home/theph/ws_vins_ros2_sem_geodf
pip install -r requirements-yolo.txt
colcon build --packages-select pht_vio pht_vio_ros yolo_dynamic_mask
source install/setup.bash
```

## Ablation configs (planned)

| Mode | geodf_enable | sem_enable | Notes |
|------|-------------|------------|-------|
| baseline | 0 | 0 | VINS |
| geodf | 1 | 0 | Paper #1 |
| sad_sem | 0 | 1 | Frozen SAD |
| sem_geodf | 1 | 1 | Fusion (TODO) |
