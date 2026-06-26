# GeoDF-Inertial — N-trial results (Paper #2)

ATE/RPE RMSE in metres. **Inertial** = IMU-predicted epipolar geometry (Paper #2). **Adaptive** = GeoDF-Adaptive feature-fit (Paper #1). Δ% = improvement (+ is better).

## VIODE — ATE (baseline vs adaptive vs inertial)

| env | level | baseline | adaptive (P1) | inertial (P2) | Δ% vs baseline | Δ% vs adaptive |
|---|---|---|---|---|---|---|
| city_day | 0_none | 0.110±0.001 | 0.120±0.000 | 0.136±0.021 | -24.2% | -13.2% |
| city_day | 1_low | 0.138±0.001 | 0.148±0.014 | 0.163±0.033 | -17.8% | -10.2% |
| city_day | 2_mid | 0.166±0.000 | 0.152±0.009 | 0.141±0.014 | +14.7% | +7.2% |
| city_day | 3_high | 0.409±0.052 | 0.309±0.000 | 0.280±0.034 | +31.5% | +9.2% |
| city_night | 0_none | 0.420±0.002 | 0.246±0.000 | 0.404±0.000 | +3.6% | -64.4% |
| city_night | 1_low | 0.504±0.009 | 0.538±0.036 | n/a | n/a | n/a |
| city_night | 2_mid | 0.502±0.010 | 0.460±0.000 | n/a | n/a | n/a |
| city_night | 3_high | 0.875±0.018 | 0.835±0.026 | n/a | n/a | n/a |
| parking_lot | 0_none | 0.167±0.000 | 0.147±0.003 | n/a | n/a | n/a |
| parking_lot | 1_low | 0.118±0.000 | 0.106±0.000 | n/a | n/a | n/a |
| parking_lot | 2_mid | 0.144±0.000 | 0.197±0.013 | n/a | n/a | n/a |
| parking_lot | 3_high | 0.119±0.000 | 0.172±0.000 | 0.126±0.015 | -5.8% | +26.7% |

## VIODE — headline parking_lot recovery

Inertial beats adaptive on **1/1** parking_lot mid/high conditions (Paper #1 regression zone).

## EuRoC static safety (inertial vs adaptive)

| sequence | baseline | adaptive (P1) | inertial (P2) | Δ% vs adaptive |
|---|---|---|---|---|
| MH_01_easy | 0.185±0.007 | 0.177±0.000 | n/a | n/a |
| MH_02_easy | 0.169±0.000 | 0.165±0.005 | n/a | n/a |
| MH_03_medium | 0.292±0.000 | 0.274±0.000 | n/a | n/a |
| MH_04_difficult | 0.447±0.000 | 0.436±0.007 | n/a | n/a |
| MH_05_difficult | 0.298±0.000 | 0.290±0.009 | n/a | n/a |

## GeoDF mode usage (from geo_df_stats.csv col 22 = mode)

Mode codes: 0=feature-fit fallback, 1=inertial Sampson, 2=derotation.

- **city_day_0_none_inertial**: mode0=100.0% mode1=0.0% mode2=0.0%
- **city_day_1_low_inertial**: mode0=1.7% mode1=89.8% mode2=8.4%
- **city_day_2_mid_inertial**: mode0=1.7% mode1=89.7% mode2=8.6%
- **city_day_3_high_inertial**: mode0=1.9% mode1=89.3% mode2=8.8%
- **city_night_0_none_inertial**: mode0=1.8% mode1=83.7% mode2=14.5%
- **parking_lot_2_mid_inertial**: mode0=100.0% mode1=0.0% mode2=0.0%
- **parking_lot_3_high_inertial**: mode0=1.9% mode1=74.8% mode2=23.3%
