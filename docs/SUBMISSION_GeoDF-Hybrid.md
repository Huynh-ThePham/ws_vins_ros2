# Paper #2 — GeoDF-Hybrid Submission Notes

Branch: `paper/geodf-imu-dynamic-2026-q4`
Prior work: Paper #1 frozen on `paper/geodf-adaptive-vins-2026-q4` (commit `c646740`).

## Pivot (Paper #2 framing)

**GeoDF-Hybrid** — two-source geometry filter with reliability-gated arbitration (not “P1 + always-on IMU”). Inertial-only path retained as ablation (`geodf_hybrid_enable=0`).

## Status

| Item | State |
|---|---|
| Hybrid arbitration in `rejectGeoDynamic()` | done |
| Params `geodf_hybrid_enable`, `geodf_hybrid_inertial_floor` | done |
| VIODE + EuRoC hybrid configs | done |
| Benchmark scripts (`run_geodf_hybrid*.sh`) | done |
| N=5 VIODE evaluation | pending |
| Manuscript | `docs/MANUSCRIPT_GeoDF-Hybrid.md` |
| Legacy inertial ablation doc | `docs/MANUSCRIPT_GeoDF-IMU.md` |

## Reproduce

```bash
source /opt/ros/humble/setup.bash
cd ws_vins_ros2 && colcon build --packages-select pht_vio pht_vio_ros

# Priority conditions (parking_lot + static safety)
bash scripts/run_geodf_hybrid.sh "2_mid 3_high" "parking_lot" 5
bash scripts/run_geodf_hybrid.sh "0_none" "city_day city_night" 5

# Full VIODE grid N=5
bash scripts/run_geodf_hybrid_n5.sh 5

python3 scripts/summarize_hybrid_n5.py
```

Inertial-only ablation (for comparison table):

```bash
bash scripts/run_geodf_inertial.sh "0_none 3_high" "city_night parking_lot" 5
python3 scripts/summarize_inertial_n5.py
```

EuRoC static safety:

```bash
export EUROC_ROOT=/media/theph/Data1/Research/Datasets/EuRoC
bash scripts/run_geodf_euroc_hybrid.sh 5
```

## Config

- **Hybrid (proposed):** `src/config/viode/viode_stereo_imu_geodf_hybrid_config.yaml`
- **Inertial ablation:** `src/config/viode/viode_stereo_imu_geodf_inertial_config.yaml` (`geodf_hybrid_enable: 0`)

Key hybrid parameters: `geodf_hybrid_enable`, `geodf_hybrid_inertial_floor`
(`floor_on`, default 0.088), `geodf_hybrid_floor_off` (hysteresis return, 0.06)
and `geodf_hybrid_dwell` (anti-chatter dwell, 8 frames). The arbitration signal
is the slow `F_p1`-sensed epipolar-outlier floor, latched on its sustained level.

## Claims for Paper #2

1. Two geometry sources (feature-fit + inertial + derotation) with explicit arbitration.
2. Dynamic-density proxy (`hybrid_signal`: the Paper #1-sensed slow epipolar-outlier floor) gates source selection — not universal IMU-on. Sensing is decoupled from rejection (signal always read from `F_p1`), so the latch cannot self-oscillate.
3. Recovers Paper #1 parking_lot failure without static-scene regression seen in inertial-only runs.
4. Front-end only; Paper #1 back-end machinery unchanged.
