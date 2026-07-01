# Git Branch Naming Policy

International-style branch names for a multi-paper VINS research workspace.  
All names use **lowercase ASCII**, **kebab-case**, and **no spaces**.

## Namespace overview

| Prefix | Purpose | Mutable? | Merge target |
|--------|---------|----------|--------------|
| `main` | Integration / stable ROS 2 port | Yes (reviewed) | — |
| `baseline/` | Frozen reproducible reference | **No** (tags only) | never from `paper/` or `exp/` |
| `paper/` | One branch per manuscript | Yes until acceptance | `main` (optional, after review) |
| `exp/` | Exploratory ideas, failed ablations | Yes | discard or fork to `paper/` |

## 1. Baseline branches

Format:

```text
baseline/<platform>-<sensor-modality>-<system>-<benchmark>-v<N>
```

| Field | Meaning | Examples |
|-------|---------|----------|
| `platform` | Runtime stack | `ros2`, `ros1` |
| `sensor-modality` | Camera + inertial setup | `stereo`, `mono`, `stereo-imu` (prefer `stereo` when IMU is implied by system) |
| `system` | Academic system class | `vi-slam` (Visual-Inertial SLAM), `vio` (odometry-only), `vins` |
| `benchmark` | Primary validation dataset | `euroc`, `kitti`, `viode` |
| `vN` | Major baseline revision | `v1`, `v2` |

**Terminology (international)**

| Token | Full name | When to use |
|-------|-----------|-------------|
| `vi-slam` | **Visual-Inertial SLAM** | Stereo/Mono + IMU + mapping or loop closure (this workspace) |
| `vio` | **Visual-Inertial Odometry** | Pose tracking only, no loop closure in scope |
| `vins` | Visual-Inertial Navigation System | Product-style name; use if paper brands as VINS |

**Rules**

- Branch from `main` (or previous baseline tag), verify metrics, then **freeze**.
- Bugfix-only; no algorithm experiments.
- Tag each freeze: e.g. git tag `baseline-v1.0-ros2-stereo-vi-slam-euroc`.

**Current baseline**

| Branch | Description |
|--------|-------------|
| `baseline/ros2-stereo-vi-slam-euroc-v1` | ROS 2 **stereo Visual-Inertial SLAM** reference verified on EuRoC (VIO + loop closure). Former names: `baseline/euroc-verified`, `baseline/ros2-euroc-stereo-imu-v1`. |

## 2. Paper branches

Format:

```text
paper/<method-slug>-<year>-<venue>
```

| Field | Meaning | Examples |
|-------|---------|----------|
| `method-slug` | Short method name (≤4 words) | `geodf-adaptive-vins`, `sad-vins` |
| `year` | Target submission / publication year | `2026` |
| `venue` | Journal tier or conference | `q4`, `q3`, `q2`, `q1`, `icra2026`, `iros2026`, `ral2026` |

**Rules**

- **One branch = one paper.** Do not mix two manuscripts on the same branch.
- Branch from the current baseline (not from another active `paper/` branch).
- Keep configs, scripts, and evaluation artifacts needed to reproduce tables/figures.
- After acceptance: tag `paper-<method-slug>-<year>-accepted` and optionally merge to `main`.

**Naming examples**

```text
paper/geodf-adaptive-vins-2026-q4      # GeoDF-Adaptive: scene-aware hard rejection
paper/geodf-weighted-vins-2026-q4      # GeoDF-Weighted: backend soft weighting
paper/sad-vins-2026-q1                 # SAD-VINS (semantic-adaptive dynamic)
paper/dyn-robust-vio-2027-icra2027     # future conference paper
```

**Venue codes**

| Code | Meaning |
|------|---------|
| `q1` … `q4` | Scopus journal quartile (target) |
| `ral2026` | IEEE RA-L |
| `tro2026` | IEEE T-RO |
| `icra2026`, `iros2026` | Conference + year |

Use the **shortest unambiguous** venue code.

## 3. Experimental branches

Format:

```text
exp/<topic-slug>
```

Examples: `exp/imu-gated-geodf`, `exp/soft-weight-backend`.

- No publication claim required.
- Safe to delete after merge or rejection.
- Never merge into `baseline/`.

## 4. Workflow (multi-paper lab)

```text
main
 └── baseline/ros2-stereo-vi-slam-euroc-v1     [frozen]
       ├── paper/geodf-adaptive-vins-2026-q4
       ├── paper/geodf-weighted-vins-2026-q4
       ├── paper/sad-vins-2026-q1
       └── exp/<scratch>
```

1. Verify baseline on EuRoC (and other core benchmarks).
2. `git checkout -b paper/<new-paper> baseline/ros2-stereo-vi-slam-euroc-v1`
3. Implement method + benchmarks on the paper branch.
4. Tag baseline/paper milestones; open PR to `main` only when integrating stable code.

## 5. Migration map (legacy names)

| Old branch | New branch |
|------------|------------|
| `baseline/euroc-verified` | `baseline/ros2-stereo-vi-slam-euroc-v1` |
| `baseline/ros2-euroc-stereo-imu-v1` | `baseline/ros2-stereo-vi-slam-euroc-v1` |
| `paper/geodf-vins-hard-q4` | `paper/geodf-adaptive-vins-2026-q4` |
| `paper/geodf-imu-dynamic-2026-q4` | `paper/geodf-weighted-vins-2026-q4` |
| `paper/geodf-weighted-dynamic-2026-q4` | `paper/geodf-weighted-vins-2026-q4` |
| `ws_vins_ros2_paper1_freeze` (worktree) | `ws_vins_ros2_paper1_adaptive` |
| `ws_vins_ros2_paper2_freeze` (worktree) | `ws_vins_ros2_paper2_weight` |
| `paper/sad-vins-q1-research-20260501` | `paper/sad-vins-2026-q1` (recommended) |

After renaming locally, update remote:

```bash
git push -u origin <new-branch-name>
git push origin --delete <old-branch-name>
```

## 6. Protected-branch policy (recommended on GitHub)

| Branch pattern | Protection |
|----------------|------------|
| `main` | Require PR, no force-push |
| `baseline/*` | Require PR + benchmark checklist; no force-push |
| `paper/*` | No force-push to shared remote |
| `exp/*` | Unrestricted |

## 7. Commit messages on paper branches

Prefer Conventional Commits scoped to the method:

```text
feat(geodf): add adaptive EMA gating in feature tracker
eval(geodf): add VIODE city_day ablation scripts
docs(geodf): fix OpenCV epipolar notation in proposal
```

This keeps history readable when many papers share one repository.
