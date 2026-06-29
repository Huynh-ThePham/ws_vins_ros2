# SAD-VINS freeze (`paper/sad-vins-2026-q1`)

Frozen reference for semantic-only dynamic masking (YOLOv11-seg + VINS-Fusion stereo+IMU).

**Tag:** `paper-sad-vins-2026-q1-freeze`  
**Worktree:** `/home/theph/ws_vins_ros2_sadvins` (keep on this branch; do not implement fusion here)

## Reproduce

```bash
cd /home/theph/ws_vins_ros2_sadvins
git checkout paper/sad-vins-2026-q1
pip install -r requirements-yolo.txt
colcon build --packages-select pht_vio pht_vio_ros yolo_dynamic_mask
source install/setup.bash
./scripts/run_sad_full_evaluation.sh
```

Config: `src/config/euroc/euroc_stereo_imu_sem_config.yaml`

## Next work

Semantic–geometry fusion: branch `exp/sem-geodf-gated`, worktree `/home/theph/ws_vins_ros2_sem_geodf`.
