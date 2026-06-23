# SAD-VINS: Semantic-Adaptive Dynamic Visual-Inertial Navigation

**Branch:** `paper/sad-vins-2026-q1`  
**Baseline:** `baseline/ros2-stereo-vi-slam-euroc-v1` (frozen EuRoC reference)

## Motivation

Dynamic objects (people, vehicles) corrupt VIO feature tracks. **SAD-VINS** adds a **semantic front-end** that rejects features on known dynamic COCO classes before they enter the VINS optimizer.

This design follows the YOLO segmentation pipeline from the Khang ORB-SLAM3 reference (`yolo_dynamic_mask`), ported to **VINS-Fusion ROS 2** without ORB-SLAM3 dependencies.

## Pipeline

```text
/cam0/image_raw ──► YOLOv11n-seg ──► /dynamic_mask (mono8: 255=static, 0=dynamic)
        │                                      │
        └──────────────► VINS FeatureTracker ◄─┘
                              │
                         reject + mask gating
                              │
                         VIO backend (unchanged)
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

## Evaluation (TODO)

- EuRoC sequences with dynamic content (if available) or VIODE dynamic scenes
- Compare baseline vs SAD-VINS: ATE, track survival, dynamic pixel rejection rate
- Latency budget: YOLO avg ms + VINS front-end

## References

- Khang repo: YOLOv11-seg + ORB-SLAM3 dynamic masking (local reference)
- Ultralytics YOLO11 segmentation
- VINS-Fusion (HKUST aerial robotics)
