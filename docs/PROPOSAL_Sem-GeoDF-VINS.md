# Sem-GeoDF VINS — Semantic–GeoDF Adaptive Gated Union (`paper/sem-geodf-vins-2026`)

**Worktree:** `/home/theph/ws_vins_ros2_sem_geodf`  
**Base:** `paper/geodf-adaptive-vins-2026` + semantic front-end from `paper/sad-vins-2026`  
**Branch:** `paper/sem-geodf-vins-2026` · **Worktree helper:** `bash scripts/sem_geodf_worktree.sh [--build] [--benchmark quick|full]`

## Related manuscripts

- **GeoDF-Adaptive (AECE):** geometry-only method — `docs/MANUSCRIPT_GeoDF-VINS-AECE.md`
- **SGTA-VINS / SAD:** semantic baseline — `paper/sad-vins-2026` worktree
- **This branch:** semantic YOLO + GeoDF-Adaptive OR fusion with online adaptive semantic policy

## Method name (experimental)

**Adaptive gated union (OR) dynamic rejection** — not AND fusion. Each branch has its own scene gate; candidates are unioned under one shared ratio guard, while the semantic soft-mask path is adapted to estimated dynamic level.

Semantic–geo overlap uses **raw GeoDF candidates** (`geo.raw_candidates`, falling back to `geo.confirmed` only when raw is empty) and no longer requires the GeoDF scene gate (`geo.frame_active`). Overlap is **bidirectional**: `sem_geo_overlap_last = max(geo→sem, sem→geo)`, where `geo→sem` is the fraction of the geo pool that is semantic-dynamic and `sem→geo` is the fraction of semantic-raw tracks inside the geo pool. This keeps the overlap trigger alive when GeoDF and YOLO fire on different-sized track sets. The candidate-count guard passes when either the geo pool or the semantic-raw count reaches `sem_policy_min_geo_candidates`.

## Fusion mode (`rejectSemGeoFused`)

When `sem_geodf_fusion: 1` and both `sem_enable` and `geodf_enable` are set:

```text
cam0 + YOLO mask
     │
     ├─► semantic scene gate (EMA of dynamic pixel ratio)
     │        └─► adaptive policy: static-safe / dynamic-assist / strong-dynamic
     │
prev tracks ──► GeoDF-Adaptive gate (EMA epipolar outlier ratio)
     │        └─► hard reject if geo.confirmed (dual-gate + vote)
     │
     └─► gated union (OR) ──► applyTrackRejection (shared ratio guard)
              └─► adaptive consensus residual weights for suspicious survivors
```

| Branch | Signal | Arms when | Hard reject |
|--------|--------|-----------|-------------|
| Semantic | YOLO dynamic pixel ratio + bidirectional semantic-geo overlap on **geo.raw_candidates** | Adaptive policy state > static-safe **and** `sem_scene_active` | Dynamic mask pixel + `sem_vote_frames` (default 2) |
| GeoDF | Epipolar outlier ratio | GeoDF-Adaptive auto-ρ + hysteresis | RANSAC∧Sampson + `geodf_vote_frames` |

All semantic paths (`sad_sem`, `sequential`, `sem_geodf`) share the same **vote-based** semantic candidate confirmation (`confirmSemanticCandidates`).

### Soft mask vs hard reject

| Mechanism | Adaptive policy (`sem_adaptive_policy: 1`) | Ablation (`sem_mask_gated: 1`) |
|-----------|--------------------------------------------|----------------------------------|
| **Hard cull** (existing tracks) | Off in static-safe; from dynamic-assist onward (state ≥ 1) vote-confirmed semantic tracks enter the shared reject set when `sem_scene_active`; any suspicious tracks that survive the ratio/min-feature guard are down-weighted in the backend | Gated by `sem_scene_active` |
| **Soft mask** (`setMask`, new features) | Static-safe: `sem_scene_active`; dynamic-assist/strong: held after burst/overlap/strong EMA | Only when `sem_scene_active` |

### Adaptive backend weighting (online)

When `sem_geodf_backend_weight: 1`, each surviving feature observation carries a residual weight \(w_i \in [w_\min, 1]\) into the visual factors. The Ceres residual and Jacobian are multiplied by \(\sqrt{w_i}\), so the optimizer receives a proper weighted least-squares residual rather than an untracked post-hoc score.

The target weight is computed only from online signals available at the current frame:

- semantic raw/vote-confirmed mask hit;
- GeoDF raw/confirmed candidate state;
- normalized Sampson excess from the current epipolar estimate;
- semantic/GeoDF scene confidence from EMA+hysteresis;
- semantic-GeoDF overlap EMA.

Recovery is per feature id (`sem_geodf_backend_recovery`) so a track that stops looking dynamic returns gradually to weight 1. The estimator does **not** read VIODE level, GT, ATE/RPE, hold-out metrics, or run summary files. Audit columns are appended to `sem_geodf_stats.csv`: `weighted_tracks`, `mean_backend_weight`, `geo_valid`, `geo_raw_candidates`, `geo_overlap_pool`, `min_backend_weight`, `mean_backend_target`.

### Adaptive dynamic-level policy (online)

| State | Evidence | Behavior |
|-------|----------|----------|
| `0 static-safe` | No burst / weak overlap / weak strong EMA | Soft mask only when `sem_scene_active`; semantic hard reject off |
| `1 dynamic-assist` | Burst, bidirectional overlap on raw GeoDF candidates, or hold timer | Soft mask held for `sem_policy_hold_frames`; vote-confirmed semantic hard-culled when `sem_scene_active` |
| `2 strong-dynamic` | Strong semantic EMA, semantic-geo agreement, or assist + usable raw geo evidence | Full OR fusion armed when `sem_scene_active` |

Policy trigger reasons are logged per frame: `sem_policy_trigger_burst`, `sem_policy_trigger_strong`, `sem_policy_trigger_overlap`.

### Oracle / labelled VIODE override (diagnostic only)

```bash
ORACLE_ABLATION=1 SEM_POLICY_VIODE_LEVEL_OVERRIDE=1 ./scripts/run_sem_geodf_ablation.sh quick
```

Maps `0_none/1_low→0`, `2_mid→1`, `3_high→2`. **Excluded** from default summaries unless `--include-oracle` is passed. Requires `ORACLE_ABLATION=1`.

### vs sequential (`sem_geodf_fusion: 0`)

GeoDF hard reject → semantic hard reject (two passes). Semantic branch uses the same vote logic but no shared ratio guard with GeoDF.

## Parameters (default fusion config)

```yaml
sem_geodf_fusion: 1
sem_activate_ratio: 0.015
sem_activate_ema: 0.15
sem_deactivate_frac: 0.6
sem_mask_gated: 0
sem_vote_frames: 2
sem_adaptive_policy: 1
sem_policy_dynamic_level: -1   # online only for main results
sem_policy_burst_ratio: 0.18
sem_policy_strong_ratio: 0.20
sem_policy_hold_frames: 120
sem_policy_overlap_ratio: 0.35
sem_policy_overlap_ema: 0.20
sem_policy_min_geo_candidates: 2
sem_geodf_backend_weight: 1
sem_geodf_backend_min_weight: 0.25
sem_geodf_backend_semantic_weight: 0.55
sem_geodf_backend_geo_weight: 0.75
sem_geodf_backend_agree_weight: 0.25
sem_geodf_backend_recovery: 0.20

geodf_adaptive: 1
geodf_auto_rho: 1
geodf_vote_frames: 2
geodf_max_reject_ratio: 0.40
```

## Ablation modes (6)

| Mode | Config suffix | YOLO |
|------|---------------|------|
| baseline | `stereo_imu` | no |
| adaptive | `stereo_imu_geodf_adaptive` | no |
| sad_sem | `stereo_imu_sem` | yes |
| sequential | `stereo_imu_sem_geodf_sequential` | yes, fusion off |
| sem_geodf | `stereo_imu_sem_geodf` | yes, adaptive gated union |
| sem_geodf_mask_gated | `stereo_imu_sem_geodf_mask_gated` | yes, soft mask gated |

## Build & run

```bash
cd /home/theph/ws_vins_ros2_sem_geodf
colcon build --packages-select pht_vio pht_vio_ros yolo_dynamic_mask
source install/setup.bash

ros2 launch pht_vio_ros euroc_stereo_imu_sem_geodf.launch.py yolo_device:=cuda
```

## Evaluation protocol (published default)

| Setting | Value |
|---------|-------|
| `FAIR_BAG_RATE` | `1` (default) |
| `SAD_BAG_RATE` | `1.0` (same rate for YOLO and non-YOLO) |
| `sem_policy_dynamic_level` | `-1` (online policy) |
| Trials | `N=3` for full matrix |
| Result root | `results/sem_geodf_ablation/<PROTOCOL_TAG>/` (default `fair1p0`) |

Each run writes `run_manifest.json` with `git_sha`, `config_hash`, `bag_rate`, `protocol_fair`, `oracle_ablation`, and `sem_policy_dynamic_level`.

```bash
# Quick smoke (1 trial)
./scripts/run_sem_geodf_ablation.sh quick

# Full paper matrix (N=3, fair 1.0×, 3 VIODE envs)
./scripts/run_sem_geodf_full_rerun.sh

# Oracle diagnostic only
ORACLE_ABLATION=1 SEM_POLICY_VIODE_LEVEL_OVERRIDE=1 \
  ./scripts/run_sem_geodf_ablation.sh quick
```

Summaries (QC-filtered, oracle excluded by default):

```bash
python3 scripts/audit_sem_geodf_protocol.py \
  --configs src/config/euroc/euroc_stereo_imu_sem_geodf_config.yaml \
            src/config/viode/viode_stereo_imu_sem_geodf_config.yaml

python3 scripts/summarize_sem_geodf_ablation.py \
  --root results/sem_geodf_ablation/fair1p0 \
  --out results/sem_geodf_ablation/fair1p0/ABLATION_SUMMARY.md
```

Legacy runs under `results/sem_geodf_ablation/euroc/` and `viode/` predate protocol versioning; regenerate under `fair1p0/` for paper tables.

## Semantic policy tuning (train / hold-out)

Thresholds are **not** tuned on the same sequences used for final ATE claims.

| Split | VIODE env | Levels | Role |
|-------|-----------|--------|------|
| **Train** | `city_day` | `0_none`–`3_high` | Grid-search `(burst, strong, hold)`; overlap fixed at `0.35` |
| **Hold-out** | `city_night`, `parking_lot` | `0_none`–`3_high` | Report only — never used for selection |
| **Generalization** | EuRoC `MH_01`–`MH_05` | — | Report only — static-safe policy behaviour |

Offline replay reads `sem_geodf_stats.csv` from `city_day` `sem_geodf` fusion runs. Selection maximizes a weighted per-level policy objective (static `0_none` vs dynamic `3_high` trade-off), not ATE. The tuning scripts fail by default if any `city_day` train level is missing; use `ALLOW_INCOMPLETE_TRAIN=1` only for draft/debug tables.

```bash
# 1) Generate the train split only. This does not run city_night,
# parking_lot, or EuRoC.
./scripts/run_sem_policy_protocol.sh train

# 2) Select thresholds from city_day only and write selected_params.yaml.
./scripts/run_sem_policy_protocol.sh tune

# 3) Final report runs. sem_geodf receives selected_params.yaml;
# baselines and ablations keep their own configs.
./scripts/run_sem_policy_protocol.sh holdout

# 4) Regenerate hold-out summary + one-page sensitivity report.
./scripts/run_sem_policy_protocol.sh report

# Outputs:
#   results/sem_policy_tuning/selected_params.yaml   (train-selected)
#   results/sem_policy_tuning/tuning_report.json
#   results/sem_policy_tuning/SENSITIVITY_TABLE.md   (burst × strong × hold, 1 page)
#   results/sem_policy_tuning/sensitivity_grid.csv
#   results/sem_geodf_ablation/sem_policy_holdout_fair1p0/HOLDOUT_SUMMARY.md
```

**Paper rule:** fix thresholds from `selected_params.yaml` (or keep default YAML if train rank ≈ 1) **before** inspecting hold-out ATE. If the train split is incomplete, `selected_params.yaml` is marked `status=draft_incomplete_train` and `run_sem_policy_protocol.sh holdout` refuses to use it unless `ALLOW_INCOMPLETE_TRAIN=1` is set for debug only. Sensitivity table shows conclusions are stable across a local grid; overlap appendix sweeps `0.30 / 0.35 / 0.40` at default burst/strong/hold.

Single-run replay / verify against logged policy:

```bash
python3 scripts/simulate_sem_policy.py RUN_DIR --verify
python3 scripts/simulate_sem_policy.py STATS.csv --sweep
```

## Real-time robustness

| Mechanism | Param | Behavior |
|-----------|-------|----------|
| Non-blocking sync | `sem_block_on_mask: 0` | VINS never waits for YOLO |
| Geo-only fallback | mask empty/stale | `sem_mask_trusted=0` → skip semantic branch |
| Latest mask within age | `sem_mask_max_age_ms: 150` | Use lagging mask if fresh enough |

## Known limitations (report honestly)

- EuRoC easy sequences may regress vs GeoDF-Adaptive when YOLO adds overhead/FP.
- Gains are strongest on VIODE `3_high`; not universal.
- `parking_lot` high-dynamic density remains a stress case (untested for fusion until full rerun completes).
- Preliminary single-trial numbers in older logs are **superseded** by `fair1p0/` N≥3 matrix.
