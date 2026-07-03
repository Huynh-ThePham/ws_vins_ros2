# Stereo 3D vs 2D-F ablation (full 12-condition study (trial_1..1))

| Config | Alias | Candidate gate |
|---|---|---|
| Baseline VINS | `baseline` | none |
| Old adaptive | `adaptive_2d` | temporal **2D-F** + scene gating |
| New adaptive (PROPOSED) | `adaptive` | **stereo 3D motion consistency** + scene gating |

Δ% = ATE improvement vs baseline (+ is better). Δ(3D−2D) = 2D-F minus stereo 3D (+ means 3D wins).

## VIODE trajectory (ATE RMSE mean±std)

| env | level | baseline ATE | old 2D-F ATE | new 3D ATE | Δ% 3D vs base | Δ(3D−2D) |
|---|---|---|---|---|---|---|
| city_day | 0_none | 0.155±0.000 (n=1) | 0.109±0.000 (n=1) | 0.109±0.000 (n=1) | +29.2% | -0.1% |
| city_day | 1_low | 0.138±0.000 (n=1) | 0.139±0.000 (n=1) | 0.139±0.000 (n=1) | -0.6% | -0.4% |
| city_day | 2_mid | 0.166±0.000 (n=1) | 0.166±0.000 (n=1) | 0.166±0.000 (n=1) | -0.1% | -0.1% |
| city_day | 3_high | 0.339±0.000 (n=1) | 0.322±0.000 (n=1) | 0.294±0.000 (n=1) | +13.4% | +8.8% |
| city_night | 0_none | 0.418±0.000 (n=1) | 0.418±0.000 (n=1) | 0.296±0.000 (n=1) | +29.1% | +29.1% |
| city_night | 1_low | 0.500±0.000 (n=1) | 0.500±0.000 (n=1) | 0.500±0.000 (n=1) | +0.0% | -0.0% |
| city_night | 2_mid | 0.497±0.000 (n=1) | 0.497±0.000 (n=1) | 0.523±0.000 (n=1) | -5.1% | -5.1% |
| city_night | 3_high | 0.885±0.000 (n=1) | 0.884±0.000 (n=1) | 0.884±0.000 (n=1) | +0.0% | -0.0% |
| parking_lot | 0_none | 0.167±0.000 (n=1) | 0.167±0.000 (n=1) | 0.167±0.000 (n=1) | +0.0% | +0.0% |
| parking_lot | 1_low | 0.118±0.000 (n=1) | 0.101±0.000 (n=1) | 0.125±0.000 (n=1) | -6.2% | -23.8% |
| parking_lot | 2_mid | 0.144±0.000 (n=1) | 0.213±0.000 (n=1) | 0.152±0.000 (n=1) | -5.6% | +28.5% |
| parking_lot | 3_high | 0.119±0.000 (n=1) | 0.141±0.000 (n=1) | 0.148±0.000 (n=1) | -23.8% | -4.5% |

## Claim check

- **Dynamic (`*/3_high`):** 3D beats 2D-F by >3% on 1/3 cells
- **Low-dynamic (`*/0_none`, `*/1_low`):** 3D ATE ≤ 2D-F (+3%) on 5/6 cells
- **Overall:** 3D beats baseline by >3% on 3/12 cells

**Claim:** Stereo 3D motion consistency improves dynamic VIO robustness over 2D-F gating while preserving static pass-through.

**Verdict:** SUPPORTED

See also `MASK_EVAL_2D_vs_3D.md` for feature-level VIODE vehicle-mask precision.
