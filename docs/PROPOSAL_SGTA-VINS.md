# SGTA-VINS: Semantic–Geometric–Temporal Adaptive VIO

**Branch:** `paper/sad-vins-2026` (freeze: [SGTA-VINS-FREEZE.md](SGTA-VINS-FREEZE.md))  
**Baseline:** SAD-VINS semantic hard-mask (`sad_sem`) + GeoDF-Adaptive geometry (`geodf_adaptive`)

## Goal

Move beyond YOLO hard-mask VINS by estimating **per-feature dynamic uncertainty** and using it in front-end gating and optional visual residual soft-weighting, with **scene-adaptive Policy-2** and **ROS 2 async** semantic inference.

## Pipeline

```text
/cam0 ──► mask_node (YOLOv11n-seg, GPU, async) ──► /dynamic_mask
/cam0,/cam1,/imu ──► pht_vio_node
                       └─ FeatureTracker: SGTA + GeoDF adaptive
                       └─ optional soft-weight projection factors
                       └─ VINS-Fusion backend (stereo + IMU)
```

## Method

### 1. Three observations (per feature / frame)

| Signal | Source |
|--------|--------|
| Semantic prior | YOLO mask on cam0 |
| Geometric inconsistency | F temporal + Sampson (+ RANSAC gate) |
| Temporal persistence | EMA `p_dynamic`, vote frames, warmup |

### 2. Scene activation (GeoDF adaptive)

- Auto-ρ_on from static outlier floor + hysteresis  
- Ratio guard (`geodf_reject_ratio_max`)  
- Hard reject only when scene active and vote satisfied  

### 3. Policy-2 dual mode

```text
policy_signal = EMA( sqrt(dynamic_pixel_ratio × scene_observation) )

static-safe (default):
  auto-ρ, vote=2, warmup=30, sem_mask_gated=1, prob_th=0.55

aggressive (policy_signal ≥ 0.012, hold 45 frames):
  ρ_on=0.05, vote=1, warmup=0, mask ungated, prob_th=0.45
```

### 4. Soft backend (optional)

`visual residual weight = max(w_min, (1 - p_dynamic)^power)`

### 5. ROS 2 async integration

- `mask_node` ∥ `pht_vio_node`  
- Latest-mask sync, `sem_mask_max_age_ms: 150`  
- Default YOLO device: **CUDA**  

## Implementation

| Area | File |
|------|------|
| Parameters + CSV logs | `src/pht_vio/src/estimator/parameters.*` |
| SGTA / GeoDF gate | `src/pht_vio/src/featureTracker/feature_tracker.*` |
| Dynamic prob → backend | `src/pht_vio/src/estimator/feature_manager.*` |
| Soft projection factors | `src/pht_vio/src/factor/projection*Factor.*` |
| Mask ROS sync | `src/pht_vio_ros/src/rosNodeTest.cpp` |
| YOLO node | `src/yolo_dynamic_mask/` |
| Benchmark | `scripts/lib/sad_common.sh`, `scripts/run_realtime_benchmark.sh` |

## Key config (`viode_stereo_imu_sgta_config.yaml`)

```yaml
sem_enable: 1
sem_mask_max_age_ms: 150.0
sem_use_latest_mask: 1
sem_mask_gated: 1
geodf_enable: 1
geodf_adaptive: 1
geodf_auto_rho: 1
geodf_vote_frames: 2
geodf_warmup_frames: 30
geodf_dynamic_prob_th: 0.55
sgta_policy_enable: 1
sgta_aggressive_sem_on: 0.012
sgta_aggressive_sem_off: 0.008
sgta_aggressive_hold_frames: 45
sgta_soft_weight_enable: 1
sgta_imu_gate_enable: 0   # ablation only
```

## Run

```bash
source install/setup.bash
./scripts/run_sad_viode.sh "0_none 1_low 2_mid 3_high" "baseline sad_sem sgta"
./scripts/run_sad_euroc.sh MH_03_medium sgta --eval
./scripts/run_realtime_benchmark.sh
```

## Evaluation snapshot

### VIODE `city_day` (representative)

| Level | baseline | sad_sem | sgta (Policy-2) |
|-------|----------|---------|-----------------|
| 0_none | 0.109 m | 0.138 m | ~0.114 m |
| 3_high | 0.346 m | 0.203 m | **0.162 m** (smoke) |

### Realtime @ 1.0× bag, GPU

| | sgta `3_high` |
|--|---------------|
| Pose coverage | 98.6% (~10 Hz) |
| ATE | 0.176 m (≈ 0.5× eval 0.170 m) |
| YOLO | ~27 ms |
| Mask lag p95 | 100 ms |
| GeoDF | ~1.2 ms |

See [REALTIME_BENCHMARK.md](REALTIME_BENCHMARK.md).

## Paper contributions

1. **Uncertainty-aware SGTA gating** — semantic + geometry + temporal `p_dynamic`, not hard mask  
2. **Policy-2 scene switching** — static-safe vs aggressive tuned params  
3. **Modular ROS 2 deployment** — decoupled YOLO + latency-bounded mask fusion  
4. **Realtime validation** — 1.0× sensor playback on GPU with measured latency  

## Ablations

| Method | Role |
|--------|------|
| baseline | VINS-Fusion ROS 2 |
| sad_sem | Frozen SAD-VINS |
| geodf / geodf_adaptive | Geometry-only |
| sgta | Full method |
| sgta @ 0.5× vs 1.0× | Accuracy vs realtime |
| process_every_n=2 | YOLO skip (not recommended) |

## Deferred

Structural-anchor VINS: [FUTURE_STRUCTURAL_ANCHOR_VINS.md](FUTURE_STRUCTURAL_ANCHOR_VINS.md)
