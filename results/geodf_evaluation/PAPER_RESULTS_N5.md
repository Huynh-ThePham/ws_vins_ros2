# GeoDF-VINS — N=5 single-build results (mean±std)

ATE/RPE RMSE in metres. PROPOSED = adaptive (hard reject + scene-gating + auto-ρ + temporal voting). Δ% = improvement of PROPOSED vs baseline (+ is better).

## VIODE — ATE/RPE (baseline vs PROPOSED)

| env | level | baseline ATE | PROPOSED ATE | Δ% | baseline RPE | PROPOSED RPE |
|---|---|---|---|---|---|---|
| city_day | 0_none | 0.110±0.001 | 0.120±0.000 | -9.7% | 0.112±0.000 | 0.112±0.000 |
| city_day | 1_low | 0.138±0.001 | 0.148±0.014 | -6.8% | 0.126±0.000 | 0.106±0.039 |
| city_day | 2_mid | 0.166±0.000 | 0.152±0.009 | +8.1% | 0.032±0.000 | 0.029±0.000 |
| city_day | 3_high | 0.409±0.052 | 0.309±0.000 | +24.5% | 0.137±0.026 | 0.176±0.000 |
| city_night | 0_none | 0.420±0.002 | 0.246±0.000 | +41.3% | 0.041±0.000 | 0.035±0.000 |
| city_night | 1_low | 0.504±0.009 | 0.538±0.036 | -6.7% | 0.055±0.002 | 0.058±0.002 |
| city_night | 2_mid | 0.502±0.010 | 0.460±0.000 | +8.5% | 0.049±0.000 | 0.048±0.000 |
| city_night | 3_high | 0.875±0.018 | 0.835±0.026 | +4.6% | 0.094±0.002 | 0.090±0.000 |
| parking_lot | 0_none | 0.167±0.000 | 0.147±0.003 | +12.1% | 0.028±0.000 | 0.023±0.000 |
| parking_lot | 1_low | 0.118±0.000 | 0.106±0.000 | +9.7% | 0.024±0.000 | 0.024±0.000 |
| parking_lot | 2_mid | 0.144±0.000 | 0.197±0.013 | -36.4% | 0.036±0.000 | 0.041±0.006 |
| parking_lot | 3_high | 0.119±0.000 | 0.172±0.000 | -44.3% | 0.052±0.000 | 0.051±0.000 |

## Determinism — run-to-run ATE std (lower = more repeatable)

Dynamic scenes show baseline variance that the PROPOSED filter collapses.

| env | level | baseline std | PROPOSED std | baseline range |
|---|---|---|---|---|
| city_day | 0_none | 0.001 | 0.000 | [0.109, 0.110] |
| city_day | 1_low | 0.001 | 0.014 | [0.137, 0.139] |
| city_day | 2_mid | 0.000 | 0.009 | [0.166, 0.166] |
| city_day | 3_high | 0.052 | 0.000 | [0.345, 0.458] |
| city_night | 0_none | 0.002 | 0.000 | [0.418, 0.423] |
| city_night | 1_low | 0.009 | 0.036 | [0.500, 0.522] |
| city_night | 2_mid | 0.010 | 0.000 | [0.497, 0.523] |
| city_night | 3_high | 0.018 | 0.026 | [0.839, 0.884] |
| parking_lot | 0_none | 0.000 | 0.003 | [0.167, 0.168] |
| parking_lot | 1_low | 0.000 | 0.000 | [0.118, 0.118] |
| parking_lot | 2_mid | 0.000 | 0.013 | [0.144, 0.144] |
| parking_lot | 3_high | 0.000 | 0.000 | [0.119, 0.119] |

## EuRoC — static safety (ATE, baseline vs PROPOSED)

| seq | baseline ATE | PROPOSED ATE | Δ% |
|---|---|---|---|
| MH_01_easy | 0.185±0.007 | 0.177±0.000 | +4.3% |
| MH_02_easy | 0.169±0.000 | 0.165±0.005 | +2.0% |
| MH_03_medium | 0.292±0.000 | 0.274±0.000 | +6.2% |
| MH_04_difficult | 0.447±0.000 | 0.436±0.007 | +2.3% |
| MH_05_difficult | 0.298±0.000 | 0.290±0.009 | +2.6% |

## VIODE ATE verdict (PROPOSED vs baseline, ±3% band)

- wins(>+3%)=7  losses(<-3%)=5  neutral=0

