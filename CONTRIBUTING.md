# Contributing to PHT SLAM (ROS 2)

Thank you for your interest in this project. This repository hosts a **ROS 2 port of VINS-Fusion** and supports **multi-paper visual-inertial SLAM research**.

## Quick links

| Document | Purpose |
|----------|---------|
| [docs/BRANCHING.md](docs/BRANCHING.md) | Branch naming policy (`main`, `baseline/`, `paper/`, `exp/`) |
| [ARCHITECTURE.md](ARCHITECTURE.md) | Package layout and research workflow |
| [README.md](README.md) | Build, run, and benchmark instructions |

## How to contribute

### 1. Bug reports and small fixes → `main`

1. Fork the repository.
2. Create a branch from `main`: `fix/<short-description>`.
3. Build and test on at least one EuRoC sequence if your change touches VIO/loop closure.
4. Open a Pull Request to `main` with:
   - What changed and why
   - How you tested (command + sequence)

### 2. Algorithm research → `paper/` or `exp/`

Do **not** open feature PRs directly to `baseline/*`.

1. Read [docs/BRANCHING.md](docs/BRANCHING.md).
2. Branch from the current baseline:
   ```bash
   git fetch origin
   git checkout -b paper/<method>-<year>-<venue> origin/baseline/ros2-stereo-vi-slam-euroc-v1
   ```
3. Keep **one branch per manuscript**.
4. Include reproducible scripts/configs for your evaluation.
5. Prefer Conventional Commits: `feat(geodf): …`, `eval(geodf): …`, `docs: …`.

### 3. Baseline updates → `baseline/*`

Baseline branches are **frozen references**. Changes require:

- EuRoC benchmark regression on MH_01–MH_05 (VIO ± loop)
- Maintainer review
- Version bump (`v1` → `v2`) and git tag

## Development setup

```bash
git clone git@github.com:Huynh-ThePham/ws_vins_ros2.git
cd ws_vins_ros2
source /opt/ros/${ROS_DISTRO}/setup.bash
colcon build --symlink-install
source install/setup.bash
source scripts/setup_ws.bash
```

## Benchmark checklist (before merging estimator changes)

```bash
bash scripts/run_euroc_benchmark.sh MH_01_easy stereo_imu 40 0 --eval
bash scripts/run_euroc_benchmark.sh MH_01_easy stereo_imu 40 1 --eval
```

For research branches, follow the evaluation scripts documented on that branch (e.g. GeoDF on `paper/geodf-adaptive-vins-2026-q4`).

## Code style

- Match surrounding C++ / Python style in the package you edit.
- Minimize scope: one logical change per commit when possible.
- Do not commit `build/`, `install/`, `log/`, `results/`, or converted dataset bags.

## License

By contributing, you agree that your contributions will be licensed under the [GNU GPL v3](LICENSE), consistent with VINS-Fusion.
