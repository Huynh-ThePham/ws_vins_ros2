# PHT SLAM Architecture (Modular)

This workspace separates **algorithm research code** from **ROS 2 integration** so you can modify and test estimators without touching nodes, launch files, or visualization.

## Package Layers

```
┌─────────────────────────────────────────────────────────────┐
│  ROS 2 interface (nodes, launch, RViz)                      │
│  pht_vio_ros · pht_loop_closure_ros · pht_global_fusion_ros │
├─────────────────────────────────────────────────────────────┤
│  Algorithm cores (libraries)                                │
│  pht_vio · pht_loop_closure · pht_global_fusion             │
├─────────────────────────────────────────────────────────────┤
│  Shared foundations                                         │
│  pht_slam_common · pht_slam_ros_common · pht_camera_models  │
└─────────────────────────────────────────────────────────────┘
```

## Package Reference

| Package | Role | Modify when… |
|---------|------|--------------|
| **pht_slam_common** | Math utils (`Utility`), timing (`TicToc`), portable logging | Adding shared math helpers |
| **pht_camera_models** | Camera calibration models (pinhole, MEI, etc.) | Changing camera projection models |
| **pht_vio** | VIO backend: feature tracker, factors, initialization, sliding-window estimator | **Primary VIO algorithm research** |
| **pht_slam_ros_common** | Shared RViz marker helpers (`CameraPoseVisualization`) | Changing debug visualization primitives |
| **pht_vio_ros** | ROS 2 node, publishers, TF, launch files, config install | Wiring topics, bags, demos |
| **pht_loop_closure** | BRIEF/DBoW loop closure, pose graph optimization | **Loop closure algorithm research** |
| **pht_loop_closure_ros** | ROS 2 loop-closure node | Topic names, I/O only |
| **pht_global_fusion** | GPS + VIO global optimization (Ceres) | **GPS fusion algorithm research** |
| **pht_global_fusion_ros** | ROS 2 global fusion node | Topic names, I/O only |
| **pht_slam** | Metapackage (`ros2 pkg install pht_slam`) | — |

## Directory Map (Algorithm Code)

### `pht_vio/src/`

| Directory | Contents |
|-----------|----------|
| `estimator/` | `Estimator`, `FeatureManager`, YAML config loader |
| `featureTracker/` | KLT frontend, feature tracking |
| `factor/` | Ceres factors, IMU preintegration, marginalization |
| `initial/` | SFM bootstrap, visual-IMU alignment, 5-pt pose |

### `pht_loop_closure/src/`

| File / Dir | Contents |
|------------|----------|
| `keyframe.*` | BRIEF descriptors, loop matching, PnP |
| `pose_graph.*` | Pose graph, 4DoF/6DoF optimization |
| `ThirdParty/` | DBoW2, DVision (vendored) |

### `pht_global_fusion/src/`

| File | Contents |
|------|----------|
| `globalOpt.*` | GPS/VIO fusion optimizer |
| `Factors.h` | Ceres cost functions |

## Decoupling: Estimator ↔ ROS

`pht_vio` does **not** publish ROS messages directly. Instead, `Estimator` exposes callbacks:

```cpp
estimator.setFrameOutputCallback([](const Estimator &e, double t) { /* publish odometry */ });
estimator.setPropagatedStateCallback([](double t, const Vector3d &P, ...) { /* IMU propagation */ });
estimator.setTrackImageCallback([](const cv::Mat &img, double t) { /* debug image */ });
estimator.setStatisticsCallback([](const Estimator &e, double ms) { /* timing stats */ });
```

The ROS wiring lives in `pht_vio_ros/src/ros/estimator_ros_adapter.cpp`.

To test algorithms offline, link against `pht_vio` and provide your own callbacks (or none).

## Build Order

Colcon resolves dependencies automatically:

```
pht_slam_common → pht_vio → pht_vio_ros
pht_slam_common + pht_slam_ros_common + pht_camera_models → pht_loop_closure → pht_loop_closure_ros
pht_slam_common → pht_global_fusion → pht_global_fusion_ros
pht_camera_models → pht_vio, pht_loop_closure
```

## Typical Research Workflow

1. **Change a Ceres factor** → edit `pht_vio/src/factor/`
2. **Change initialization** → edit `pht_vio/src/initial/`
3. **Change loop detection** → edit `pht_loop_closure/src/keyframe.cpp` or `pose_graph.cpp`
4. Rebuild only the affected package:
   ```bash
   colcon build --packages-select pht_vio pht_vio_ros --symlink-install
   ```
5. Run as before:
   ```bash
   ros2 launch pht_vio_ros euroc_stereo_imu.launch.py enable_loop:=true
   ```

## Config & Assets

- YAML configs: installed by `pht_vio_ros` → `share/pht_vio_ros/config/`
- Loop vocabulary: `share/pht_loop_closure_ros/support_files/` (auto-downloaded at build)
- ROS 1 reference (not built): `src/VINS-Fusion-ROS1/`

## Future Improvements

- Further decouple `pht_loop_closure` from file I/O in `PoseGraph::savePoseGraph`
- Add offline loop-closure evaluation binary

## Offline VIO (no ROS)

Run VIO directly on EuRoC `mav0/` folders:

```bash
ros2 run pht_vio pht_vio_offline \
  $(ros2 pkg prefix pht_vio_ros)/share/pht_vio_ros/config/euroc/euroc_stereo_imu_config.yaml \
  /path/to/MH_01_easy/MH_01_easy/mav0 \
  40
```

Arguments: `config.yaml`, `mav0/`, optional `start_sec`, optional `duration_sec`.

Output trajectory: `output_path/vio.csv` from the YAML config.

## Configuration API

VIO parameters are loaded into `VinsConfig` via `vinsConfig().loadFromYaml(path)` (see `pht_vio/src/estimator/parameters.h`). Loop-closure node settings use `LoopClosureConfig` in `pht_loop_closure/src/loop_closure_config.hpp`.

## Git branches (baseline vs papers)

Full policy: **[docs/BRANCHING.md](docs/BRANCHING.md)** — international naming for multi-paper research.

| Branch | Purpose |
|--------|---------|
| `main` | Stable ROS 2 integration (default for contributors) |
| `baseline/ros2-stereo-vi-slam-euroc-v1` | Frozen **ROS 2 stereo Visual-Inertial SLAM** reference (EuRoC verified). **No algorithm experiments.** |
| `paper/<method>-<year>-<venue>` | One branch per manuscript (e.g. `paper/geodf-adaptive-vins-2026-q4`) |
| `exp/<topic>` | Exploratory work; not publication-bound |

```text
main
 └── baseline/ros2-stereo-vi-slam-euroc-v1
       ├── paper/geodf-adaptive-vins-2026-q4
       ├── paper/geodf-weighted-vins-2026-q4
       ├── paper/sad-vins-2026-q1          (planned)
       └── exp/<scratch>
```

Tag baseline freezes: `baseline-v1.0-ros2-stereo-vi-slam-euroc`.

### GeoDF-Adaptive VINS (`paper/geodf-adaptive-vins-2026-q4`)

Q4 paper branch — *GeoDF-VINS-Hard: A Lightweight Geometry-Based Dynamic Feature Rejection Method for Stereo-Inertial VINS-Fusion* (scene-aware adaptive self-gating). See [docs/PROPOSAL_GeoDF-VINS-Hard.md](docs/PROPOSAL_GeoDF-VINS-Hard.md).

| Method | Config | Use case |
|--------|--------|----------|
| `baseline` | `euroc_stereo_imu_config.yaml` | No filter |
| `geodf_hard` | `euroc_stereo_imu_geodf_config.yaml` | Always-on + ratio guard (EuRoC static sanity) |
| `alwayson` / `geodf_dump` | `euroc_stereo_imu_geodf_dump_config.yaml` | Always-on + per-feature dump (detection eval) |
| `adaptive` | `euroc_stereo_imu_geodf_adaptive_config.yaml` | **Recommended** — scene-aware self-gating |
| `geodf_noguard` | `euroc_stereo_imu_geodf_noguard_config.yaml` | Ablation (no ratio guard) |

VIODE configs under `src/config/viode/` (max_cnt=120, pinhole calib).

**Datasets:** EuRoC (`EUROC_ROOT`) + VIODE (`VIODE_ROOT`, default `city_day`).

```bash
# EuRoC single run
./scripts/run_geodf_euroc.sh MH_01_easy adaptive --eval

# EuRoC static ablation (baseline / always-on / adaptive)
./scripts/run_euroc_static_ablation.sh

# Full EuRoC study + filter impact
./scripts/run_geodf_full_benchmark.sh all

# VIODE dynamic (converts ROS1 bag → ros2_bag on first run)
export VIODE_ROOT=/path/to/viode
./scripts/run_geodf_viode.sh "0_none 1_low 2_mid 3_high" "baseline geodf_dump adaptive"

# Full pipeline (EuRoC + VIODE)
./scripts/run_geodf_benchmark_all.sh all
```

Summaries: `results/geodf_study/geodf_summary.md`, `results/geodf/euroc_static_ablation.md`, `results/viode/viode_city_day_adaptive.md`.

### GeoDF-Weighted VINS (`paper/geodf-weighted-vins-2026-q4`)

Paper #2 branch — *GeoDF-Weighted: Uncertainty-Normalized Inertial Residual Weighting for Dynamic Feature Robustness in Stereo-Inertial VINS*. See [docs/PROPOSAL_GeoDF-Weighted.md](docs/PROPOSAL_GeoDF-Weighted.md).

**Worktree:** `../ws_vins_ros2_paper2_weight` — `bash scripts/paper2_weight_worktree.sh [--build] [--benchmark N]`

| Method | Config | Use case |
|--------|--------|----------|
| `baseline` | `euroc_stereo_imu_config.yaml` | No filter |
| `weighted` | `euroc_stereo_imu_geodf_weighted_config.yaml` | **Recommended** — IMU geometry scoring + backend soft weights |

```bash
# VIODE full eval (N=5)
FORCE=1 bash scripts/run_geodf_weighted_n5.sh 5

# EuRoC static regression
FORCE=1 bash scripts/run_geodf_euroc_weighted.sh 5
```

Re-run EuRoC checks before merging research into baseline:

```bash
./scripts/run_euroc_benchmark_all.sh
./scripts/run_euroc_benchmark_loop_all.sh
./scripts/regenerate_benchmark_summaries.sh
```

Tag releases on the baseline branch, e.g. `baseline-v1.0-ros2-stereo-vi-slam-euroc`.
