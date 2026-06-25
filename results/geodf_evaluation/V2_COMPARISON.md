# GeoDF-VINS-Hard v2 comparison — (B) auto rho_on + (F) stereo cross-check

Single-session run (baseline / adaptive-v1 / adaptive-v2) so trajectory deltas are internally consistent. v2 = auto-calibrated activation threshold + right-view temporal epipolar cross-check (OR fusion, right Sampson gate = 6.0).

## 1. VIODE trajectory ATE-RMSE (m) — lower is better

| env | level | baseline | adaptive v1 | adaptive v2 | v2 Δ vs base | v2 Δ vs v1 |
|---|---|---:|---:|---:|---:|---:|
| city_day | 0_none | 0.109 | 0.112 | 0.174 | -58.7% | -54.6% |
| city_day | 1_low | 0.138 | 0.138 | 0.141 | -1.8% | -2.3% |
| city_day | 2_mid | 0.172 | 0.148 | 0.147 | +14.4% | +0.8% |
| city_day | 3_high | 0.346 | 0.225 | 0.323 | +6.4% | -43.9% |
| city_night | 0_none | 0.418 | 0.377 | 0.434 | -3.9% | -15.2% |
| city_night | 1_low | 0.500 | 0.536 | 0.641 | -28.1% | -19.4% |
| city_night | 2_mid | 0.497 | 0.445 | 0.590 | -18.7% | -32.6% |
| city_night | 3_high | 0.884 | 0.891 | 0.926 | -4.8% | -4.0% |
| parking_lot | 0_none | 0.169 | 0.130 | 0.151 | +11.0% | -15.7% |
| parking_lot | 1_low | 0.118 | 0.101 | 0.088 | +25.6% | +13.2% |
| parking_lot | 2_mid | 0.145 | 0.117 | 0.281 | -94.1% | -140.8% |
| parking_lot | 3_high | 0.119 | 0.217 | 0.138 | -15.4% | +36.6% |

## 2. Detection quality vs simulator GT (always-on dump): v1 vs v2

v1 = left temporal epipolar only. v2 = left OR right-view stereo cross-check.

| env | level | recall v1 | recall v2 | prec v1 | prec v2 | lift v1 | lift v2 | TP v1 | TP v2 | static-FPR v1 | static-FPR v2 |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| city_day | 0_none | n/a | n/a | 0.000 | 0.000 | n/a | n/a | 0 | 0 | 0.0059 | 0.0073 |
| city_day | 1_low | 0.204 | 0.182 | 0.025 | 0.020 | 31.7 | 24.0 | 23 | 22 | 0.0063 | 0.0074 |
| city_day | 2_mid | 0.087 | 0.133 | 0.147 | 0.155 | 12.1 | 13.4 | 151 | 217 | 0.0062 | 0.0084 |
| city_day | 3_high | 0.089 | 0.124 | 0.339 | 0.338 | 8.3 | 8.7 | 495 | 656 | 0.0074 | 0.0098 |
| city_night | 0_none | n/a | n/a | 0.000 | 0.000 | n/a | n/a | 0 | 0 | 0.0077 | 0.0096 |
| city_night | 1_low | 0.009 | 0.013 | 0.017 | 0.022 | 1.1 | 1.5 | 16 | 25 | 0.0077 | 0.0090 |
| city_night | 2_mid | 0.024 | 0.030 | 0.062 | 0.067 | 3.0 | 3.2 | 60 | 78 | 0.0076 | 0.0090 |
| city_night | 3_high | 0.024 | 0.024 | 0.143 | 0.122 | 2.9 | 2.5 | 144 | 144 | 0.0074 | 0.0088 |
| parking_lot | 0_none | n/a | n/a | 0.000 | 0.000 | n/a | n/a | 0 | 0 | 0.0172 | 0.0199 |
| parking_lot | 1_low | 0.031 | 0.088 | 0.013 | 0.034 | 1.6 | 4.1 | 26 | 73 | 0.0193 | 0.0210 |
| parking_lot | 2_mid | 0.045 | 0.041 | 0.159 | 0.148 | 1.5 | 1.4 | 469 | 435 | 0.0283 | 0.0287 |
| parking_lot | 3_high | 0.046 | 0.048 | 0.198 | 0.195 | 1.4 | 1.4 | 625 | 647 | 0.0304 | 0.0321 |

## 3. Auto-calibration & stereo-cue evidence (adaptive_v2 geo_df_stats)

rho_on is computed per-frame as floor*1.8 + 0.05 (clamped [0.10, 0.40]); stereo-added = candidates contributed by the right-view cross-check.

| env | level | armed % | rho_on (mean/max) | outlier floor (mean/max) | stereo-added (total/frames) | geo ms (mean/p95) |
|---|---|---:|---:|---:|---:|---:|
| city_day | 0_none | 2.9 | 0.101/0.110 | 0.017/0.034 | 403/195 | 1.07/1.64 |
| city_day | 1_low | 2.2 | 0.101/0.108 | 0.019/0.032 | 336/189 | 1.08/1.59 |
| city_day | 2_mid | 6.6 | 0.105/0.138 | 0.020/0.049 | 426/201 | 1.07/1.60 |
| city_day | 3_high | 11.7 | 0.112/0.179 | 0.027/0.071 | 477/212 | 1.11/1.77 |
| city_night | 0_none | 7.8 | 0.111/0.141 | 0.028/0.051 | 234/151 | 1.11/1.69 |
| city_night | 1_low | 10.2 | 0.111/0.148 | 0.028/0.055 | 177/123 | 1.10/1.66 |
| city_night | 2_mid | 8.1 | 0.114/0.155 | 0.030/0.058 | 216/113 | 1.10/1.60 |
| city_night | 3_high | 11.1 | 0.115/0.150 | 0.031/0.055 | 171/108 | 1.09/1.64 |
| parking_lot | 0_none | 22.4 | 0.142/0.212 | 0.048/0.090 | 238/139 | 1.16/1.82 |
| parking_lot | 1_low | 15.0 | 0.146/0.216 | 0.051/0.092 | 232/93 | 1.19/1.89 |
| parking_lot | 2_mid | 17.0 | 0.177/0.279 | 0.068/0.127 | 148/60 | 1.27/2.12 |
| parking_lot | 3_high | 12.3 | 0.199/0.275 | 0.080/0.125 | 137/54 | 1.32/2.19 |

## 4. EuRoC static no-regression — ATE-RMSE (m)

| seq | baseline | adaptive v2 | Δ vs base |
|---|---:|---:|---:|
| MH_01_easy | 0.180 | 0.186 | -3.4% |
| MH_02_easy | 0.169 | 0.169 | -0.1% |
| MH_03_medium | 0.264 | 0.262 | +0.7% |
| MH_04_difficult | 0.447 | 0.456 | -2.2% |
| MH_05_difficult | 0.298 | 0.302 | -1.2% |

