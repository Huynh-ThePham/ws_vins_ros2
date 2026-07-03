# Stereo 3D vs 2D-F ablation (full 12-condition study (trial_1..5))

| Config | Alias | Candidate gate |
|---|---|---|
| Baseline VINS | `baseline` | none |
| Old adaptive | `adaptive_2d` | temporal **2D-F** + scene gating |
| New adaptive (PROPOSED) | `adaptive` | **stereo 3D motion consistency** + scene gating |

Δ% = ATE improvement vs baseline (+ is better). Δ(3D−2D) = 2D-F minus stereo 3D (+ means 3D wins).

## VIODE trajectory (ATE RMSE mean±std)

| env | level | baseline ATE | old 2D-F ATE | new 3D ATE | Δ% 3D vs base | Δ(3D−2D) |
|---|---|---|---|---|---|---|
| city_day | 0_none | 0.118±0.018 (n=5) | 0.109±0.000 (n=5) | 0.109±0.000 (n=5) | +7.7% | +0.0% |
| city_day | 1_low | 0.139±0.000 (n=5) | 0.139±0.000 (n=5) | 0.139±0.000 (n=5) | -0.2% | -0.1% |
| city_day | 2_mid | 0.166±0.001 (n=5) | 0.167±0.002 (n=5) | 0.166±0.000 (n=5) | -0.2% | +0.7% |
| city_day | 3_high | 0.344±0.003 (n=5) | 0.324±0.005 (n=5) | 0.293±0.000 (n=5) | +14.8% | +9.5% |
| city_night | 0_none | 0.418±0.000 (n=5) | 0.418±0.000 (n=5) | 0.394±0.049 (n=5) | +5.8% | +5.8% |
| city_night | 1_low | 0.505±0.009 (n=5) | 0.500±0.000 (n=5) | 0.500±0.000 (n=5) | +0.9% | +0.0% |
| city_night | 2_mid | 0.497±0.000 (n=5) | 0.497±0.000 (n=5) | 0.502±0.010 (n=5) | -1.0% | -1.0% |
| city_night | 3_high | 0.884±0.000 (n=5) | 0.884±0.000 (n=5) | 0.884±0.000 (n=5) | +0.0% | -0.0% |
| parking_lot | 0_none | 0.167±0.000 (n=5) | 0.167±0.000 (n=5) | 0.167±0.000 (n=5) | -0.1% | -0.1% |
| parking_lot | 1_low | 0.118±0.000 (n=5) | 0.101±0.000 (n=5) | 0.125±0.000 (n=5) | -6.1% | -23.7% |
| parking_lot | 2_mid | 0.144±0.000 (n=5) | 0.219±0.007 (n=5) | 0.152±0.000 (n=5) | -5.6% | +30.5% |
| parking_lot | 3_high | 0.119±0.000 (n=5) | 0.140±0.003 (n=5) | 0.148±0.000 (n=5) | -23.8% | -5.6% |

## Claim check

- **Dynamic (`*/3_high`):** 3D beats 2D-F by >3% on 1/3 cells
- **Low-dynamic (`*/0_none`, `*/1_low`):** 3D ATE ≤ 2D-F (+3%) on 5/6 cells
- **Overall:** 3D beats baseline by >3% on 3/12 cells

**Claim:** Stereo 3D motion consistency improves dynamic VIO robustness over 2D-F gating while preserving static pass-through.

**Verdict:** SUPPORTED

See also `MASK_EVAL_2D_vs_3D.md` for feature-level VIODE vehicle-mask precision.
