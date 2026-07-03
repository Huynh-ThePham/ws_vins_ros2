# EuRoC Machine Hall — N=3 repeat study (mean±std)

Static safety check: baseline vs PROPOSED (adaptive). Δ% = ATE improvement of PROPOSED vs baseline (+ is better). Pass criterion: no regression beyond ±5% on static EuRoC.

## ATE / RPE (baseline vs PROPOSED)

| seq | baseline ATE | PROPOSED ATE | Δ% | baseline RPE | PROPOSED RPE |
|---|---|---|---|---|---|
| MH_01_easy | 0.180±0.000 | 0.180±0.000 | +0.0% | 0.045±0.000 | 0.045±0.000 |
| MH_02_easy | 0.169±0.000 | 0.162±0.005 | +3.9% | 0.039±0.000 | 0.042±0.002 |
| MH_03_medium | 0.292±0.000 | 0.292±0.000 | -0.0% | 0.050±0.000 | 0.050±0.000 |
| MH_04_difficult | 0.447±0.000 | 0.447±0.000 | +0.0% | 0.065±0.000 | 0.065±0.000 |
| MH_05_difficult | 0.298±0.000 | 0.298±0.000 | +0.0% | 0.057±0.000 | 0.057±0.000 |

## Static ablation baselines — ATE Δ% vs baseline

Ablations keep the same stereo-inertial backend and only alter the GeoDF front-end guard. Empty cells mean the corresponding trials have not been generated in this worktree.

| seq | alwayson | adaptive_fixed | adaptive_no_quality | adaptive_no_vote | PROPOSED |
|---|---|---|---|---|---|
| MH_01_easy | n/a | n/a | n/a | n/a | +0.0% |
| MH_02_easy | n/a | n/a | n/a | n/a | +3.9% |
| MH_03_medium | n/a | n/a | n/a | n/a | -0.0% |
| MH_04_difficult | n/a | n/a | n/a | n/a | +0.0% |
| MH_05_difficult | n/a | n/a | n/a | n/a | +0.0% |

## Repeatability — run-to-run ATE std

| seq | baseline std | PROPOSED std | baseline range |
|---|---|---|---|
| MH_01_easy | 0.000 | 0.000 | [0.180, 0.180] |
| MH_02_easy | 0.000 | 0.005 | [0.169, 0.169] |
| MH_03_medium | 0.000 | 0.000 | [0.292, 0.292] |
| MH_04_difficult | 0.000 | 0.000 | [0.447, 0.447] |
| MH_05_difficult | 0.000 | 0.000 | [0.298, 0.298] |

## Verdict

- **PASS**: no sequence exceeds 5% ATE regression vs baseline
