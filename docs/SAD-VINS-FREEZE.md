# SAD-VINS freeze (ablation baseline)

Semantic-only dynamic masking: YOLOv11-seg hard reject on cam0 + VINS-Fusion stereo+IMU.

**Role in current branch:** frozen **ablation** (`sad_sem` method). Main contribution is **SGTA-VINS** — see [SGTA-VINS-FREEZE.md](SGTA-VINS-FREEZE.md).

**Tag (historical):** `paper-sad-vins-2026-q1-freeze`  
**Worktree:** `/home/theph/ws_vins_ros2_sadvins`

## Reproduce SAD-VINS only

```bash
git checkout paper/sad-vins-2026
colcon build --symlink-install --packages-select pht_vio pht_vio_ros yolo_dynamic_mask
source install/setup.bash
./scripts/run_sad_euroc.sh MH_01_easy sad_sem --eval
./scripts/run_sad_viode.sh "3_high" "baseline sad_sem"
```

Config: `src/config/euroc/euroc_stereo_imu_sem_config.yaml`

Proposal: [PROPOSAL_SAD-VINS.md](PROPOSAL_SAD-VINS.md)
