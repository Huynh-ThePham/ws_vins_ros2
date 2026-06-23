# SAD-VINS: Semantic-Adaptive Dynamic Visual-Inertial Navigation

**Branch:** `paper/sad-vins-2026-q1`  
**Baseline:** `baseline/ros2-stereo-vi-slam-euroc-v1` (frozen EuRoC reference)  
**Sensor setup:** **stereo camera + IMU** (same as baseline; not mono-only)

## Sensor setup (stereo + IMU)

SAD-VINS keeps the baseline **stereo Visual-Inertial SLAM** stack unchanged in the backend. Only the **front-end** adds semantic masking on the left camera (`cam0`).

| Sensor | EuRoC topic | Role |
|--------|-------------|------|
| IMU | `/imu0` | Preintegration + bias estimation |
| Left camera | `/cam0/image_raw` | KLT tracking + YOLO input |
| Right camera | `/cam1/image_raw` | Stereo depth / right-track validation |

YAML flags: `imu: 1`, `num_of_cam: 2` — identical modality to `euroc_stereo_imu_config.yaml`.

## Motivation

Dynamic objects (people, vehicles) corrupt VIO feature tracks. **SAD-VINS** adds a **semantic front-end** that rejects features on known dynamic COCO classes before they enter the VINS optimizer.

This design follows the YOLO segmentation pipeline from the Khang ORB-SLAM3 reference (`yolo_dynamic_mask`), ported to **VINS-Fusion ROS 2** without ORB-SLAM3 dependencies.

## Pipeline

```text
/imu0 ─────────────────────────────────────────► VINS backend (IMU preintegration)
/cam1/image_raw ───────────────────────────────► stereo right track
/cam0/image_raw ──► YOLOv11n-seg ──► /dynamic_mask (mono8: 255=static, 0=dynamic)
        │                                      │
        └──────────────► VINS FeatureTracker ◄─┘  (cam0 + cam1, stereo KLT)
                              │
                         reject + mask gating (cam0 only)
                              │
                    stereo + IMU optimizer (unchanged)
```

### Dynamic COCO classes (default)

| ID | Class |
|----|-------|
| 0 | person |
| 2 | car |
| 3 | motorcycle |
| 5 | bus |
| 7 | truck |

## Integration points

| Component | Change |
|-----------|--------|
| `yolo_dynamic_mask` | New ROS 2 Python node (`mask_node`) |
| `parameters.h/cpp` | `sem_enable`, `sem_mask_topic`, `sem_static_value` |
| `feature_tracker.cpp` | `rejectSemanticDynamic()`, mask AND in `setMask()` |
| `rosNodeTest.cpp` | Subscribe + timestamp-sync mask with cam0 |
| `estimator.cpp` | Pass mask into `trackImage()` |

## Configuration

```yaml
sem_enable: 1
sem_mask_topic: "/dynamic_mask"
sem_static_value: 255
```

Config file: `src/config/euroc/euroc_stereo_imu_sem_config.yaml`

## Run (EuRoC bag)

```bash
# Terminal 1: install YOLO deps (once)
pip install -r requirements-yolo.txt

# Terminal 2: build workspace
colcon build --packages-select pht_vio pht_vio_ros yolo_dynamic_mask
source install/setup.bash

# Terminal 3: play bag + launch SAD-VINS
ros2 bag play /path/to/euroc/ros2_bag --clock
ros2 launch pht_vio_ros euroc_stereo_imu_sem.launch.py yolo_device:=cuda
```

Disable YOLO node (mask from external source):

```bash
ros2 launch pht_vio_ros euroc_stereo_imu_sem.launch.py enable_yolo:=false
```

## Differences from Khang reference

| Khang (ORB-SLAM3) | SAD-VINS (this repo) |
|-------------------|----------------------|
| FAST keypoint filter in ORB extractor | KLT track cull + `goodFeaturesToTrack` mask |
| Hardcoded venv path | Standard pip / venv |
| Fixed 640×480 mask size | Mask resized to image size |
| RGB-D RealSense topic | Configurable `image_topic` (EuRoC `/cam0/image_raw`) |

## Relation to GeoDF

| Method | Signal | Branch |
|--------|--------|--------|
| **SAD-VINS** | Semantic segmentation (YOLO) | `paper/sad-vins-2026-q1` |
| **GeoDF-Adaptive** | Epipolar geometry (Sampson) | `paper/geodf-adaptive-vins-2026-q4` |

These are **orthogonal** and can be combined in a future hybrid branch if needed.

## Evaluation (2026-06-23, stereo + IMU)

Protocol mirrors GeoDF branch: EuRoC 5×MH (baseline vs sad_sem), VIODE city_day 4 levels.

Report: `results/sad_evaluation/EVALUATION_REPORT.md`

| Dataset | Result |
|---------|--------|
| EuRoC | **5/5 PASS** (|Δ ATE| ≤ 20%); filter active, low reject on static scenes |
| VIODE 2_mid | **−17.0%** ATE (0.166 → 0.138 m) |
| VIODE 3_high | **−31.0%** ATE (0.346 → 0.239 m) |
| VIODE 0_none/1_low | Regressed (YOLO false positives + 0.5× bag rate for YOLO runs) |

**Verdict:** Filter **works** (mask 100%, reject scales with dynamic pixel %). **Useful** on high-dynamic VIODE; needs tuning for low-dynamic scenes.

```bash
./scripts/run_sad_full_evaluation.sh   # EuRoC + VIODE + report
./scripts/run_sad_euroc.sh MH_03_medium sad_sem --eval
./scripts/run_sad_viode.sh "2_mid 3_high" "baseline sad_sem"
```

## References

- Khang repo: YOLOv11-seg + ORB-SLAM3 dynamic masking (local reference)
- Ultralytics YOLO11 segmentation
- VINS-Fusion (HKUST aerial robotics)
