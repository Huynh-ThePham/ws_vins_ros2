# PHT SLAM (ROS 2)

[![ROS 2](https://img.shields.io/badge/ROS_2-Humble|Iron|Jazzy-blue)](https://docs.ros.org/en/humble/)
[![License: GPL v3](https://img.shields.io/badge/License-GPLv3-blue.svg)](LICENSE)

Official ROS 2 port of [VINS-Fusion](https://github.com/HKUST-Aerial-Robotics/VINS-Fusion) — an optimization-based multi-sensor state estimator for visual-inertial odometry, loop closure, and GPS fusion.

**Supported ROS 2 distros:** Humble, Iron, Jazzy (tested on Humble)

## Clone & build

```bash
git clone git@github.com:Huynh-ThePham/ws_vins_ros2.git
cd ws_vins_ros2
git checkout main   # or a research branch — see below
source /opt/ros/${ROS_DISTRO}/setup.bash
colcon build --symlink-install
source install/setup.bash
source scripts/setup_ws.bash
```

## Research branches

Multi-paper workflow documented in **[docs/BRANCHING.md](docs/BRANCHING.md)** · [CONTRIBUTING.md](CONTRIBUTING.md)

| Branch | Role |
|--------|------|
| **`main`** | Stable integration (start here) |
| `baseline/ros2-stereo-vi-slam-euroc-v1` | Frozen stereo **Visual-Inertial SLAM** reference (EuRoC verified) |
| [`paper/geodf-adaptive-vins-2026`](https://github.com/Huynh-ThePham/ws_vins_ros2/tree/paper/geodf-adaptive-vins-2026) | GeoDF-Adaptive — scene-aware hard rejection (AECE) |
| [`paper/geodf-weighted-vins-2026`](https://github.com/Huynh-ThePham/ws_vins_ros2/tree/paper/geodf-weighted-vins-2026) | GeoDF-Weighted — backend soft weighting |

New paper branch:

```bash
git fetch origin
git checkout -b paper/my-method-2026 origin/baseline/ros2-stereo-vi-slam-euroc-v1
```

## Packages

| Package | Description |
|---------|-------------|
| `pht_slam_common` | Shared math utilities, timing, logging |
| `pht_slam_ros_common` | Shared RViz visualization helpers |
| `pht_camera_models` | Camera calibration models (pinhole, MEI, fisheye, …) |
| `pht_vio` | VIO algorithm library (estimator, factors, initialization) |
| `pht_vio_ros` | VIO ROS 2 nodes, visualization, launch files |
| `pht_loop_closure` | Loop closure algorithm library (BRIEF/DBoW, pose graph) |
| `pht_loop_closure_ros` | Loop closure ROS 2 node |
| `pht_global_fusion` | GPS global optimization library |
| `pht_global_fusion_ros` | GPS fusion ROS 2 node |
| `pht_slam` | Metapackage (depends on all of the above) |

See [ARCHITECTURE.md](ARCHITECTURE.md) for module boundaries.

### Workspace setup

```bash
source scripts/setup_ws.bash   # clean overlay; avoids stale renamed-package paths
```

## Prerequisites

### System

- Ubuntu 22.04 (recommended for ROS 2 Humble)
- [ROS 2 Humble](https://docs.ros.org/en/humble/Installation.html) (or compatible distro)

### Dependencies

```bash
sudo apt update
sudo apt install -y \
  ros-${ROS_DISTRO}-cv-bridge \
  ros-${ROS_DISTRO}-image-transport \
  ros-${ROS_DISTRO}-tf2 \
  ros-${ROS_DISTRO}-tf2-ros \
  ros-${ROS_DISTRO}-rviz2 \
  libceres-dev \
  libeigen3-dev \
  libopencv-dev \
  libboost-filesystem-dev \
  libboost-program-options-dev \
  libboost-system-dev
```

## Build

```bash
cd ~/ws_vins_ros2
source /opt/ros/${ROS_DISTRO}/setup.bash
colcon build --symlink-install
source install/setup.bash
```

The loop-closure vocabulary (`brief_k10L6.bin`, ~58 MB) is downloaded automatically on first build if not present in `src/support_files/`.

## Quick Start — EuRoC Dataset

Download the [EuRoC MAV Dataset](https://projects.asl.ethz.ch/datasets/doku.php?id=kmavvisualinertialdatasets) (e.g. `MH_01_easy`).

### Monocular + IMU

```bash
# Terminal 1: VIO + RViz
ros2 launch pht_vio_ros euroc_mono_imu.launch.py

# Terminal 2: play bag (publish /clock for sim time)
ros2 bag play MH_01_easy --clock
```

### Stereo + IMU (with loop closure)

```bash
ros2 launch pht_vio_ros euroc_stereo_imu.launch.py enable_loop:=true
ros2 bag play MH_01_easy --clock
```

### Stereo only

```bash
ros2 launch pht_vio_ros euroc_stereo.launch.py
ros2 bag play MH_01_easy --clock
```

Green path = VIO odometry; red path = odometry with loop closure.

## Offline VIO (EuRoC, no ROS)

```bash
ros2 run pht_vio pht_vio_offline \
  $(ros2 pkg prefix pht_vio_ros)/share/pht_vio_ros/config/euroc/euroc_stereo_imu_config.yaml \
  /path/to/MH_01_easy/MH_01_easy/mav0 \
  40
```

Reads `cam0/`, `cam1/`, `imu0/` CSV streams directly and writes `vio.csv` to the YAML `output_path`.

Requires EuRoC `mav0/` with `cam0/data.csv` + PNG images (CRLF in CSV is supported). Use the installed config so camera YAML resolves correctly:

```bash
bash scripts/run_euroc_offline_benchmark.sh MH_04_difficult 15
```

Or manually:

```bash
ros2 run pht_vio pht_vio_offline \
  $(ros2 pkg prefix pht_vio_ros)/share/pht_vio_ros/config/euroc/euroc_stereo_imu_config.yaml \
  /path/to/MH_04_difficult/mav0 15
```

Set `output_path` in the YAML (or copy config + `cam*.yaml` into a writable folder).

## Benchmarks (EuRoC Machine Hall)

```bash
source scripts/setup_ws.bash
bash scripts/run_euroc_benchmark.sh MH_01_easy stereo_imu 40 0   # VIO only
bash scripts/run_euroc_benchmark.sh MH_01_easy stereo_imu 40 1   # with loop
bash scripts/run_euroc_benchmark_all.sh                          # all MH VIO
bash scripts/run_euroc_benchmark_loop_all.sh                     # all MH loop
bash scripts/run_euroc_offline_benchmark.sh MH_04_difficult 15   # offline mav0
bash scripts/regenerate_benchmark_summaries.sh                   # refresh JSON from results/
```

Summaries: `results/euroc_post_refactor_summary.json`, `results/euroc_loop_post_refactor_summary.json`.

## Manual Node Launch

```bash
# VIO estimator
ros2 run pht_vio_ros pht_vio_node \
  $(ros2 pkg prefix pht_vio_ros)/share/pht_vio_ros/config/euroc/euroc_stereo_imu_config.yaml

# Loop closure (optional)
ros2 run pht_loop_closure_ros pht_loop_closure_node \
  $(ros2 pkg prefix pht_vio_ros)/share/pht_vio_ros/config/euroc/euroc_stereo_imu_config.yaml

# RViz
ros2 launch pht_vio_ros pht_vio_rviz.launch.py
```

## KITTI Examples

### Odometry (stereo)

```bash
ros2 launch pht_vio_ros pht_vio_rviz.launch.py
ros2 run pht_vio_ros kitti_odom_test \
  $(ros2 pkg prefix pht_vio_ros)/share/pht_vio_ros/config/kitti_odom/kitti_config00-02.yaml \
  /path/to/kitti/odometry/sequences/00/
```

### GPS Fusion

```bash
ros2 launch pht_global_fusion_ros kitti_gps_fusion.launch.py \
  data_path:=/path/to/2011_10_03_drive_0027_sync/
```

## Available Launch Files

| Launch file | Description |
|-------------|-------------|
| `pht_vio_ros/pht_vio_rviz.launch.py` | RViz2 only |
| `pht_vio_ros/euroc_mono_imu.launch.py` | EuRoC mono + IMU |
| `pht_vio_ros/euroc_stereo_imu.launch.py` | EuRoC stereo + IMU |
| `pht_vio_ros/euroc_stereo.launch.py` | EuRoC stereo only |
| `pht_vio_ros/vi_car.launch.py` | Car demonstration config |
| `pht_global_fusion_ros/kitti_gps_fusion.launch.py` | KITTI VIO + GPS fusion |
| `pht_global_fusion_ros/global_fusion.launch.py` | Global fusion node only |

All EuRoC/vi_car launch files accept:
- `config:=/path/to/config.yaml` — override config file
- `enable_loop:=true` — enable loop closure
- `rviz:=false` — disable RViz
- `use_sim_time:=true` — default; sync to `/clock` when using `ros2 bag play --clock`. Set `false` for live sensors.

EuRoC benchmark scripts live in `scripts/` (`run_euroc_benchmark.sh`, `evaluate_trajectory.py`).

## ROS 1 Bag Compatibility

EuRoC and other ROS 1 bags must be converted before playback:

```bash
ros2 bag convert --input MH_01_easy.bag --output MH_01_easy_ros2
ros2 bag play MH_01_easy_ros2 --clock
```

## Configuration

Config files are installed to `share/pht_vio_ros/config/` and include presets for:

- EuRoC (`euroc/`)
- KITTI odometry (`kitti_odom/`)
- KITTI raw + GPS (`kitti_raw/`)
- RealSense D435i (`realsense_d435i/`)
- Mynteye (`mynteye/`)
- VI-Car demo (`vi_car/`)

Edit the YAML config to match your camera/IMU topics and calibration.

## Migration Notes (ROS 1 → ROS 2)

| ROS 1 | ROS 2 |
|-------|-------|
| `rosrun vins vins_node` | `ros2 run pht_vio_ros pht_vio_node` |
| `rosrun loop_fusion loop_fusion_node` | `ros2 run pht_loop_closure_ros pht_loop_closure_node` |
| `rosrun global_fusion global_fusion_node` | `ros2 run pht_global_fusion_ros pht_global_fusion_node` |
| `roslaunch vins vins_rviz.launch` | `ros2 launch pht_vio_ros pht_vio_rviz.launch.py` |
| `rosbag play` | `ros2 bag play` |

Default ROS 2 node names: `pht_vio_estimator`, `pht_loop_closure_ros`, `pht_global_fusion`.

The original ROS 1 source is preserved in `src/VINS-Fusion-ROS1/` (excluded from build via `COLCON_IGNORE`).

## License

GPL-3.0 — see [LICENSE](LICENSE) (same as upstream VINS-Fusion).

## Citation

If you use VINS-Fusion in academic research, please cite the original papers. See [support_files/paper_bib.txt](src/support_files/paper_bib.txt).

When using research branches (e.g. GeoDF-Adaptive), cite that work separately once published.

## Authors

Original VINS-Fusion: [HKUST Aerial Robotics Group](http://uav.ust.hk/)

ROS 2 port and research extensions: [Huynh-ThePham/ws_vins_ros2](https://github.com/Huynh-ThePham/ws_vins_ros2) contributors — see [CONTRIBUTING.md](CONTRIBUTING.md).
