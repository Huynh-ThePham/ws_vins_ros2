# GeoDF-Adaptive — Multi-Environment Evaluation (VIODE)

**Source:** `/home/theph/ws_vins_ros2/results/viode` · 1 deterministic run per config.

## 1. ATE-RMSE (m) — adaptive Δ vs baseline (positive = improvement)

| Env | Level | baseline | always-on | adaptive | adapt Δ | armed% |
|---|---|---:|---:|---:|---:|---:|
| city_day | 0_none | 0.155 | 0.108 | 0.113 | +27.2% | 1.7 |
| city_day | 1_low | 0.138 | 0.144 | 0.138 | +0.2% | 2.0 |
| city_day | 2_mid | 0.166 | 0.168 | 0.148 | +10.7% | 3.2 |
| city_day | 3_high | 0.456 | 0.366 | 0.225 | +50.6% | 11.0 |
| city_night | 0_none | 0.422 | 0.266 | 0.377 | +10.6% | 6.6 |
| city_night | 1_low | 0.500 | 0.557 | 0.537 | -7.5% | 10.4 |
| city_night | 2_mid | 0.497 | 0.551 | 0.444 | +10.7% | 7.4 |
| city_night | 3_high | 0.884 | 0.923 | 0.891 | -0.7% | 11.2 |
| parking_lot | 0_none | 0.167 | 0.126 | 0.128 | +23.2% | 34.2 |
| parking_lot | 1_low | 0.118 | 0.093 | 0.101 | +13.7% | 35.3 |
| parking_lot | 2_mid | 0.144 | 0.571 | 0.117 | +19.1% | 58.2 |
| parking_lot | 3_high | 0.119 | 0.149 | 0.217 | -81.8% | 78.5 |

## 2. Detection vs GT segmentation — precision lift

| Env | Level | GT base-rate | Precision | Lift | Recall | Static FPR | RANSAC dyn/stat |
|---|---|---:|---:|---:|---:|---:|---|
| city_day | 0_none | 0.0% | 0.0% | — | — | 0.6% | — |
| city_day | 1_low | 0.1% | 2.5% | 31.72× | 20.4% | 0.6% | 38.1/3.4% |
| city_day | 2_mid | 1.2% | 14.7% | 12.14× | 8.7% | 0.6% | 21.2/3.6% |
| city_day | 3_high | 4.1% | 33.9% | 8.33× | 8.9% | 0.7% | 21.2/3.8% |
| city_night | 0_none | 0.0% | 0.0% | — | — | 0.8% | — |
| city_night | 1_low | 1.5% | 1.7% | 1.12× | 0.9% | 0.8% | 7.5/5.0% |
| city_night | 2_mid | 2.0% | 6.2% | 3.02× | 2.4% | 0.8% | 13.6/5.1% |
| city_night | 3_high | 4.9% | 14.3% | 2.89× | 2.4% | 0.7% | 11.8/5.1% |
| parking_lot | 0_none | 0.0% | 0.0% | — | — | 1.7% | — |
| parking_lot | 1_low | 0.9% | 1.3% | 1.57× | 3.1% | 1.9% | 12.8/7.7% |
| parking_lot | 2_mid | 10.7% | 15.9% | 1.48× | 4.5% | 2.8% | 13.1/9.9% |
| parking_lot | 3_high | 14.0% | 19.8% | 1.42× | 4.6% | 3.0% | 14.6/10.6% |

## 3. Summary

- **city_day:** best case — high lift, adaptive wins at 2_mid/3_high.
- **city_night:** mixed generalization — moderate lift, partial ATE gains.
- **parking_lot:** counter-example — high dynamic density collapses lift & breaks adaptive at 3_high.

