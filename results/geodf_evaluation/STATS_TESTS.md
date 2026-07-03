# Statistical significance — GeoDF-Adaptive vs baseline

Independent-sample tests over per-trial ATE-RMSE. `Welch p` = Welch t-test (unequal variance); `MWU p` = Mann-Whitney U (non-parametric). Significance: `*` p<0.05, `**` p<0.01, `***` p<0.001, `ns` not significant. Effect size = Cohen's d (pooled). Positive Δ% and positive d favour the proposed method.

> Small-N caveat: with n=5 (VIODE) / n=3 (EuRoC) the minimum achievable MWU p-value is bounded; report both parametric and non-parametric results.

## VIODE (n per group = 5)

| Condition | Baseline ATE | Proposed ATE | Δ% | 95% CI diff | Welch p | MWU p | Cohen d |
| --- | --- | --- | --- | --- | --- | --- | --- |
| city_day/0_none | 0.118±0.018 | 0.109±0.000 | +7.7% | [-0.016, 0.034] | 0.370 ns | 0.045 * | 0.64 |
| city_day/1_low | 0.139±0.000 | 0.139±0.000 | -0.2% | [-0.001, 0.000] | 0.230 ns | 0.332 ns | -0.89 |
| city_day/2_mid | 0.166±0.001 | 0.166±0.000 | -0.2% | [-0.001, 0.001] | 0.336 ns | 0.599 ns | -0.69 |
| city_day/3_high | 0.344±0.003 | 0.293±0.000 | +14.8% | [0.047, 0.055] | 0.000 *** | 0.012 * | 23.79 |
| city_night/0_none | 0.418±0.000 | 0.394±0.049 | +5.8% | [-0.043, 0.092] | 0.375 ns | 0.833 ns | 0.63 |
| city_night/1_low | 0.505±0.009 | 0.500±0.000 | +0.9% | [-0.008, 0.017] | 0.377 ns | 1.000 ns | 0.63 |
| city_night/2_mid | 0.497±0.000 | 0.502±0.010 | -1.0% | [-0.019, 0.009] | 0.376 ns | 1.000 ns | -0.63 |
| city_night/3_high | 0.884±0.000 | 0.884±0.000 | +0.0% | [-0.000, 0.000] | 0.407 ns | 0.830 ns | 0.56 |
| parking_lot/0_none | 0.167±0.000 | 0.167±0.000 | -0.1% | [-0.001, 0.000] | 0.385 ns | 0.607 ns | -0.62 |
| parking_lot/1_low | 0.118±0.000 | 0.125±0.000 | -6.1% | [-0.007, -0.007] | 0.000 *** | 0.009 ** | -205.94 |
| parking_lot/2_mid | 0.144±0.000 | 0.152±0.000 | -5.6% | [-0.008, -0.008] | 0.000 *** | 0.004 ** | — |
| parking_lot/3_high | 0.119±0.000 | 0.148±0.000 | -23.8% | [-0.028, -0.028] | 0.000 *** | 0.009 ** | -1574.74 |

Significant improvements (MWU p<0.05, Δ>0): **2/12**; significant regressions: **3/12**.

## EuRoC static safety (n per group = 3)

| Condition | Baseline ATE | Proposed ATE | Δ% | 95% CI diff | Welch p | MWU p | Cohen d |
| --- | --- | --- | --- | --- | --- | --- | --- |
| MH_01_easy | 0.180±0.000 | 0.180±0.000 | +0.0% | [-0.000, 0.000] | 0.839 ns | 1.000 ns | 0.19 |
| MH_02_easy | 0.169±0.000 | 0.162±0.005 | +3.9% | [-0.008, 0.021] | 0.184 ns | 0.268 ns | 1.63 |
| MH_03_medium | 0.292±0.000 | 0.292±0.000 | -0.0% | [-0.000, 0.000] | 0.300 ns | 0.164 ns | -1.13 |
| MH_04_difficult | 0.447±0.000 | 0.447±0.000 | +0.0% | [-0.000, 0.000] | 0.616 ns | 1.000 ns | 0.46 |
| MH_05_difficult | 0.298±0.000 | 0.298±0.000 | +0.0% | [-0.000, 0.000] | 0.211 ns | 0.197 ns | 1.48 |

Significant improvements: **0/5**; significant regressions: **0/5**.

## Determinism (variance reduction) — VIODE

Levene p<0.05 with std_base > std_prop indicates the filter significantly reduces run-to-run variance.

| Condition | std base | std prop | std ratio | Levene p |
| --- | --- | --- | --- | --- |
| city_day/0_none | 0.018 | 0.000 | 229.48 | 0.350 ns |
| city_day/1_low | 0.000 | 0.000 | 10.66 | 0.250 ns |
| city_day/2_mid | 0.001 | 0.000 | 16.34 | 0.341 ns |
| city_day/3_high | 0.003 | 0.000 | 5.41 | 0.345 ns |
| city_night/0_none | 0.000 | 0.049 | 0.00 | 0.346 ns |
| city_night/1_low | 0.009 | 0.000 | 527.40 | 0.344 ns |
| city_night/2_mid | 0.000 | 0.010 | 0.00 | 0.349 ns |
| city_night/3_high | 0.000 | 0.000 | 1.38 | 0.652 ns |
| parking_lot/0_none | 0.000 | 0.000 | 0.00 | 0.335 ns |
| parking_lot/1_low | 0.000 | 0.000 | 18.48 | 0.248 ns |
| parking_lot/2_mid | 0.000 | 0.000 | — | —  |
| parking_lot/3_high | 0.000 | 0.000 | 0.02 | 0.142 ns |

