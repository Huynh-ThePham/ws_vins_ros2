# SGTA-VINS freeze (`paper/sad-vins-2026-q1`)

Frozen reference for **SGTA-VINS**: Semantic–Geometric–Temporal Adaptive stereo-inertial VIO with uncertainty-aware gating, Policy-2 scene switching, and modular ROS 2 async semantic inference.

**Recommended tag:** `paper-sgta-vins-2026-freeze`  
**Worktree:** `/home/theph/ws_vins_ros2_sadvins`  
**Supersedes:** semantic-only [SAD-VINS](SAD-VINS-FREEZE.md) on the same branch (SAD remains the `sad_sem` ablation).

## What is frozen

| Layer | Status |
|-------|--------|
| **SGTA-VINS** (main contribution) | Frozen — Policy-2, GeoDF adaptive, soft weight, ROS 2 mask sync |
| **SAD-VINS** (`sad_sem`) | Frozen baseline / ablation |
| **GeoDF** (`geodf`, `geodf_adaptive`) | Config + code for ablation (geometry-only) |
| **Realtime benchmark** | Frozen scripts + protocol (`docs/REALTIME_BENCHMARK.md`) |

Not in this freeze: TISA, GeoDF-Hybrid IMU, sem-geodf fusion worktree (`exp/sem-geodf-gated`).

## Reproduce

```bash
cd /home/theph/ws_vins_ros2_sadvins
git checkout paper/sad-vins-2026-q1
pip install -r requirements-yolo.txt
# YOLO weights: yolo11n-seg.pt in workspace root (or symlink)
colcon build --symlink-install --packages-select pht_vio pht_vio_ros yolo_dynamic_mask
source install/setup.bash

# Accuracy — VIODE 4 levels
./scripts/run_sad_viode.sh "0_none 1_low 2_mid 3_high" "baseline sad_sem geodf geodf_adaptive sgta"

# Accuracy — EuRoC spot check
export EUROC_ROOT=/path/to/euroc
./scripts/run_sad_euroc.sh MH_03_medium sgta --eval

# Realtime @ 1.0× bag (GPU default)
./scripts/run_realtime_benchmark.sh
python3 scripts/summarize_realtime_benchmark.py --root results/realtime_benchmark
```

## Primary configs

| Method | EuRoC | VIODE |
|--------|-------|-------|
| baseline | `euroc_stereo_imu_config.yaml` | `viode_stereo_imu_config.yaml` |
| sad_sem | `euroc_stereo_imu_sem_config.yaml` | `viode_stereo_imu_sem_config.yaml` |
| geodf | `euroc_stereo_imu_geodf_config.yaml` | `viode_stereo_imu_geodf_config.yaml` |
| geodf_adaptive | `euroc_stereo_imu_geodf_adaptive_config.yaml` | `viode_stereo_imu_geodf_adaptive_config.yaml` |
| **sgta** | `euroc_stereo_imu_sgta_config.yaml` | `viode_stereo_imu_sgta_config.yaml` |

## Key SGTA parameters (Policy-2)

```yaml
sem_mask_max_age_ms: 150.0
sem_use_latest_mask: 1
sem_mask_gated: 1
geodf_adaptive: 1
geodf_auto_rho: 1
sgta_policy_enable: 1
sgta_aggressive_sem_on: 0.012
sgta_aggressive_sem_off: 0.008
sgta_aggressive_hold_frames: 45
sgta_soft_weight_enable: 1
```

Policy signal: `EMA(sqrt(dynamic_pixel_ratio × scene_observation))`.

## Evidence snapshot (smoke + realtime)

| Scenario | baseline | sad_sem | sgta |
|----------|----------|---------|------|
| VIODE `3_high` ATE | 0.346 m | 0.203 m | **0.162–0.176 m** |
| VIODE `0_none` ATE | 0.109 m | 0.138 m | **~0.114 m** |
| Realtime @ 1.0× (`3_high`) | 98.6% pose cov. | yes | yes, ATE 0.176 m |
| YOLO latency (GPU) | — | ~24 ms | ~27 ms |
| Mask lag p95 | — | ~100 ms | ~100 ms (< 150 ms gate) |

Full proposal: [PROPOSAL_SGTA-VINS.md](PROPOSAL_SGTA-VINS.md)  
Realtime protocol: [REALTIME_BENCHMARK.md](REALTIME_BENCHMARK.md)

## Paper claim (safe)

> SGTA-VINS fuses semantic priors, adaptive epipolar geometry, and temporal dynamic probability for uncertainty-aware front-end gating and optional soft visual weighting, deployed as a decoupled ROS 2 pipeline that maintains ~10 Hz odometry at 1.0× sensor playback on GPU.

Do **not** claim: first YOLO-VINS, TISA track arbitration, or GeoDF-Hybrid IMU.

## Diagnostics

- `sem_stats.csv` — semantic mask / reject summary  
- `geo_df_stats.csv` — SGTA geometry, Policy-2 signal, `sgta_aggressive`  
- `results/realtime_benchmark/realtime_summary.json` — latency / throughput  
