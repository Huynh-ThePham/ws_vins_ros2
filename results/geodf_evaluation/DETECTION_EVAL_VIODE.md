# GeoDF Detection Eval — VIODE GT Segmentation (Reproduced)

**Date:** 2026-06-24  
**Branch:** `paper/geodf-adaptive-vins-2026`  
**Dataset:** `/media/theph/Data1/Research/Datasets/Viode`  
**Pipeline:** `scripts/run_geodf_proposal_evaluation.sh` (Phase 3)  
**Summary:** `results/viode/viode_city_day_detection.md` + `.json`  
**Multi-env:** `results/geodf_evaluation/MULTIENV_REPORT.md`

## Setup

| Item | Value |
|------|-------|
| VIODE bags | `/media/theph/Data1/Research/Datasets/Viode/{city_day,city_night,parking_lot}/*.bag` |
| GT dynamic | `/cam0/segmentation` → ids `vehicle_dynamic_0..10` (241–251) |
| Feature dump | `geo_df_features.csv` from **always-on** GeoDF (`geodf_dump`, no adaptive) |
| Prediction | `rejected=1` iff feature hard-deleted by dual gate |
| Timestamp match | nearest mask within **30 ms** (100% matched) |

## Results — city_day (paper main table)

| Level | GT dyn base-rate | Precision | **Lift** | Recall | Static FPR | RANSAC-out dyn / stat |
|-------|----------------:|----------:|---------:|-------:|-----------:|:---------------------:|
| 0_none | **0.00%** | — | — | — | **0.59%** | — / 3.4% |
| 1_low | 0.08% | 2.5% | **31.72×** | 20.4% | 0.59% | 38.1% / 3.4% |
| 2_mid | 1.24% | 14.7% | **12.14×** | 8.7% | 0.59% | 21.2% / 3.6% |
| 3_high | 4.10% | 33.9% | **8.33×** | 8.9% | 0.71% | 21.2% / 3.8% |

## Multi-environment (city_night + parking_lot)

| Env | Level | GT base-rate | Precision | **Lift** | Static FPR | RANSAC dyn/stat |
|-----|-------|-------------:|----------:|---------:|-----------:|:---------------:|
| city_night | 2_mid | 2.0% | 6.2% | **3.02×** | 0.8% | 13.6 / 5.1% |
| city_night | 3_high | 4.9% | 14.3% | **2.89×** | 0.7% | 11.8 / 5.1% |
| parking_lot | 2_mid | 10.7% | 15.9% | 1.48× | 2.8% | 13.1 / 9.9% |
| parking_lot | 3_high | 14.0% | 19.8% | 1.42× | 3.0% | 14.6 / 10.6% |

## Interpretation for paper

1. **Lift > 1 at all dynamic levels (city_day):** rejections are **8.3–31.7× more likely** to hit a moving vehicle than random sampling.
2. **RANSAC gate separates dynamic/static ~6×** on city_day (21–38% vs 3–4%).
3. **Static FPR ~0.6–0.7%** on city_day — lower than prior runs; adaptive gate ARM 1.7–11% keeps most frames pass-through.
4. **parking_lot** confirms limitation #1: high dynamic density collapses lift to ~1.4×.

## Reproduce

```bash
export EUROC_ROOT=/media/theph/Data1/Research/Datasets/EuRoC
export VIODE_ROOT=/media/theph/Data1/Research/Datasets/Viode
export FORCE=1
bash scripts/run_geodf_proposal_evaluation.sh
```

Per-level JSON: `results/viode/{env}_{level}_geodf_dump/detection_eval.json`
