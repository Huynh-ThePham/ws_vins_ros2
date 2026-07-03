# GeoDF-VINS — N=5 single-build results (mean±std)

ATE/RPE RMSE in metres. PROPOSED = adaptive (hard reject + scene-gating + auto-ρ + temporal voting). Δ% = improvement of PROPOSED vs baseline (+ is better).

## VIODE — ATE/RPE (baseline vs PROPOSED)

| env | level | baseline ATE | PROPOSED ATE | Δ% | baseline RPE | PROPOSED RPE |
|---|---|---|---|---|---|---|
| city_day | 0_none | 0.118±0.018 | 0.109±0.000 | +7.7% | 0.094±0.035 | 0.111±0.000 |
| city_day | 1_low | 0.139±0.000 | 0.139±0.000 | -0.2% | 0.126±0.000 | 0.126±0.000 |
| city_day | 2_mid | 0.166±0.001 | 0.166±0.000 | -0.2% | 0.032±0.000 | 0.032±0.000 |
| city_day | 3_high | 0.344±0.003 | 0.293±0.000 | +14.8% | 0.106±0.001 | 0.115±0.000 |
| city_night | 0_none | 0.418±0.000 | 0.394±0.049 | +5.8% | 0.041±0.000 | 0.040±0.002 |
| city_night | 1_low | 0.505±0.009 | 0.500±0.000 | +0.9% | 0.055±0.002 | 0.054±0.000 |
| city_night | 2_mid | 0.497±0.000 | 0.502±0.010 | -1.0% | 0.049±0.000 | 0.049±0.000 |
| city_night | 3_high | 0.884±0.000 | 0.884±0.000 | +0.0% | 0.095±0.000 | 0.095±0.000 |
| parking_lot | 0_none | 0.167±0.000 | 0.167±0.000 | -0.1% | 0.027±0.000 | 0.028±0.000 |
| parking_lot | 1_low | 0.118±0.000 | 0.125±0.000 | -6.1% | 0.024±0.000 | 0.024±0.000 |
| parking_lot | 2_mid | 0.144±0.000 | 0.152±0.000 | -5.6% | 0.036±0.000 | 0.036±0.000 |
| parking_lot | 3_high | 0.119±0.000 | 0.148±0.000 | -23.8% | 0.052±0.000 | 0.038±0.000 |

## VIODE — Q3 ablation baselines (ATE Δ% vs baseline)

Ablations isolate the contribution of each guard while keeping the same VINS backend and dataset protocol. Empty cells mean that the trials have not been generated in this worktree.

| env | level | alwayson | adaptive_fixed | adaptive_no_quality | adaptive_no_vote | PROPOSED |
|---|---|---|---|---|---|---|
| city_day | 0_none | n/a | n/a | n/a | n/a | +7.7% |
| city_day | 1_low | n/a | n/a | n/a | n/a | -0.2% |
| city_day | 2_mid | n/a | n/a | n/a | n/a | -0.2% |
| city_day | 3_high | n/a | n/a | n/a | n/a | +14.8% |
| city_night | 0_none | n/a | n/a | n/a | n/a | +5.8% |
| city_night | 1_low | n/a | n/a | n/a | n/a | +0.9% |
| city_night | 2_mid | n/a | n/a | n/a | n/a | -1.0% |
| city_night | 3_high | n/a | n/a | n/a | n/a | +0.0% |
| parking_lot | 0_none | n/a | n/a | n/a | n/a | -0.1% |
| parking_lot | 1_low | n/a | n/a | n/a | n/a | -6.1% |
| parking_lot | 2_mid | n/a | n/a | n/a | n/a | -5.6% |
| parking_lot | 3_high | n/a | n/a | n/a | n/a | -23.8% |

## Determinism — run-to-run ATE std (lower = more repeatable)

Dynamic scenes show baseline variance that the PROPOSED filter collapses.

| env | level | baseline std | PROPOSED std | baseline range |
|---|---|---|---|---|
| city_day | 0_none | 0.018 | 0.000 | [0.109, 0.155] |
| city_day | 1_low | 0.000 | 0.000 | [0.138, 0.139] |
| city_day | 2_mid | 0.001 | 0.000 | [0.164, 0.166] |
| city_day | 3_high | 0.003 | 0.000 | [0.339, 0.346] |
| city_night | 0_none | 0.000 | 0.049 | [0.418, 0.418] |
| city_night | 1_low | 0.009 | 0.000 | [0.500, 0.523] |
| city_night | 2_mid | 0.000 | 0.010 | [0.497, 0.497] |
| city_night | 3_high | 0.000 | 0.000 | [0.884, 0.885] |
| parking_lot | 0_none | 0.000 | 0.000 | [0.167, 0.167] |
| parking_lot | 1_low | 0.000 | 0.000 | [0.118, 0.118] |
| parking_lot | 2_mid | 0.000 | 0.000 | [0.144, 0.144] |
| parking_lot | 3_high | 0.000 | 0.000 | [0.119, 0.119] |

## VIODE ATE verdict (PROPOSED vs baseline, ±3% band)

- evaluated=12/12  wins(>+3%)=3  losses(<-3%)=3  neutral=6

