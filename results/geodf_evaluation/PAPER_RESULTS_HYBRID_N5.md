# GeoDF-Hybrid — N-trial results (Paper #2)

ATE RMSE in metres. **Hybrid** = two-source geometry filter with reliability-gated arbitration (P1 feature-fit vs IMU epipolar vs derotation). **Inertial-only** = ablation without arbitration. Δ% = improvement (+ is better).

## VIODE — ATE (baseline vs P1 vs inertial vs hybrid)

| env | level | baseline | adaptive (P1) | inertial | hybrid (P2) | Δ% hybrid vs P1 | Δ% hybrid vs inertial |
|---|---|---|---|---|---|---|---|
| city_day | 0_none | 0.110±0.001 | 0.120±0.000 | 0.136±0.021 | 0.120±0.000 | -0.0% | +11.7% |
| city_day | 1_low | 0.138±0.001 | 0.148±0.014 | 0.163±0.033 | 0.155±0.000 | -4.8% | +5.0% |
| city_day | 2_mid | 0.166±0.000 | 0.152±0.009 | 0.141±0.014 | 0.142±0.000 | +7.1% | -0.1% |
| city_day | 3_high | 0.409±0.052 | 0.309±0.000 | 0.280±0.034 | 0.308±0.002 | +0.3% | -9.8% |
| city_night | 0_none | 0.420±0.002 | 0.246±0.000 | 0.342±0.050 | 0.246±0.000 | +0.1% | +28.2% |
| city_night | 1_low | 0.504±0.009 | 0.538±0.036 | 0.509±0.032 | 0.567±0.000 | -5.4% | -11.6% |
| city_night | 2_mid | 0.502±0.010 | 0.460±0.000 | 0.513±0.017 | 0.481±0.043 | -4.6% | +6.3% |
| city_night | 3_high | 0.875±0.018 | 0.835±0.026 | 0.867±0.033 | 0.822±0.000 | +1.5% | +5.2% |
| parking_lot | 0_none | 0.167±0.000 | 0.147±0.003 | 0.164±0.010 | 0.149±0.000 | -1.1% | +9.2% |
| parking_lot | 1_low | 0.118±0.000 | 0.106±0.000 | 0.099±0.000 | 0.106±0.000 | -0.0% | -6.9% |
| parking_lot | 2_mid | 0.144±0.000 | 0.197±0.013 | n/a | 0.179±0.040 | +9.0% | n/a |
| parking_lot | 3_high | 0.119±0.000 | 0.172±0.000 | 0.126±0.015 | 0.134±0.004 | +22.4% | -5.9% |

## Headline checks

- parking_lot mid/high: hybrid beats P1 on **2/2** conditions

- static safety (0_none): hybrid worse than P1 on **2/3** envs (target: 0)

## EuRoC static safety

| sequence | baseline | adaptive (P1) | inertial | hybrid (P2) | Δ% hybrid vs P1 |
|---|---|---|---|---|---|
| MH_01_easy | 0.185±0.007 | 0.177±0.000 | n/a | 0.177±0.000 | -0.1% |
| MH_02_easy | 0.169±0.000 | 0.165±0.005 | n/a | 0.167±0.004 | -0.8% |
| MH_03_medium | 0.292±0.000 | 0.274±0.000 | n/a | 0.274±0.000 | -0.0% |
| MH_04_difficult | 0.447±0.000 | 0.436±0.007 | n/a | 0.441±0.000 | -1.2% |
| MH_05_difficult | 0.298±0.000 | 0.290±0.009 | n/a | 0.297±0.000 | -2.3% |

## Geometry mode + arbitration (hybrid trial_1 stats)

Mode: 0=P1 feature-fit, 1=inertial Sampson, 2=derotation. Arb: 0=n/a, 1=forced P1, 2=dynamic→inertial, 3=dynamic→derot. dyn_latch = % frames the hysteresis latch held the inertial/derot side; signal_mean = mean hybrid arbitration cue (for inertial_floor ablation). Rows marked stale were produced before hybrid diagnostics existed and must be regenerated with `FORCE=1`.

- **city_day_0_none_hybrid**: mode0=100.0%, mode1=0.0%, mode2=0.0%; arb1=100.0%; dyn_latch=0.0%; signal_mean=0.018
- **city_day_1_low_hybrid**: mode0=100.0%, mode1=0.0%, mode2=0.0%; arb1=100.0%; dyn_latch=0.0%; signal_mean=0.018
- **city_day_2_mid_hybrid**: mode0=100.0%, mode1=0.0%, mode2=0.0%; arb1=100.0%; dyn_latch=0.0%; signal_mean=0.021
- **city_day_3_high_hybrid**: mode0=100.0%, mode1=0.0%, mode2=0.0%; arb1=100.0%; dyn_latch=0.0%; signal_mean=0.028
- **city_night_0_none_hybrid**: mode0=100.0%, mode1=0.0%, mode2=0.0%; arb1=100.0%; dyn_latch=0.0%; signal_mean=0.029
- **city_night_1_low_hybrid**: mode0=100.0%, mode1=0.0%, mode2=0.0%; arb1=100.0%; dyn_latch=0.0%; signal_mean=0.029
- **city_night_2_mid_hybrid**: mode0=100.0%, mode1=0.0%, mode2=0.0%; arb1=100.0%; dyn_latch=0.0%; signal_mean=0.030
- **city_night_3_high_hybrid**: mode0=100.0%, mode1=0.0%, mode2=0.0%; arb1=100.0%; dyn_latch=0.0%; signal_mean=0.032
- **parking_lot_0_none_hybrid**: mode0=86.2%, mode1=8.7%, mode2=5.1%; arb1=86.2%, arb2=8.7%, arb3=5.1%; dyn_latch=13.8%; signal_mean=0.049
- **parking_lot_1_low_hybrid**: mode0=91.0%, mode1=6.6%, mode2=2.4%; arb1=91.0%, arb2=6.6%, arb3=2.4%; dyn_latch=9.0%; signal_mean=0.051
- **parking_lot_2_mid_hybrid**: mode0=49.6%, mode1=37.1%, mode2=13.4%; arb1=49.6%, arb2=37.1%, arb3=13.4%; dyn_latch=50.4%; signal_mean=0.071
- **parking_lot_3_high_hybrid**: mode0=40.1%, mode1=47.5%, mode2=12.4%; arb1=40.1%, arb2=47.5%, arb3=12.4%; dyn_latch=59.9%; signal_mean=0.081
