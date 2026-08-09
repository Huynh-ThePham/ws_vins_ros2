# Phase 3.6 — N=5 final paired matrix

Tag: `p36-final-n5-653defa`  
Binary/docs SHA at launch: `653defacc…` (estimator code from `59440f8`)  
Methods: `union_weight` (M0), `qm_only` (M1), `adaptive_arbitration_v2` (M3)  
Matrix: 6 scenes × 3 methods × N=5 = **90/90**, SR=1.0, coverage ≥0.979.

## Per-scene ATE RMSE (m), mean±std

| Scene | U+W | qm_only | v2 | qm Δ% | v2 Δ% |
|---|---:|---:|---:|---:|---:|
| MH_03_medium | 0.2565±0.0013 | 0.2577±0.0010 | 0.2577±0.0013 | +0.45 | +0.44 |
| MH_04_difficult | 0.4397±0.0130 | 0.4414±0.0142 | 0.4382±0.0182 | +0.39 | −0.32 |
| MH_05_difficult | 0.2989±0.0048 | **0.2904±0.0048** | **0.2944±0.0041** | **−2.83** | **−1.49** |
| city_day_2_mid | 0.1665±0.0118 | 0.1744±0.0075 | **0.1569±0.0098** | **+4.70** | **−5.78** |
| city_day_3_high | 0.2568±0.0189 | 0.2536±0.0197 | **0.2088±0.0305** | −1.24 | **−18.69** |
| city_night_3_high | 0.3349±0.0149 | 0.3334±0.0055 | 0.3244±0.0142 | −0.43 | −3.12 |

## Trial-paired bootstrap 95% CI (cand − U+W), seed 3500, 1e5 resamples

### qm_only

| Scene | Δ mean | 95% CI | |
|---|---:|---|---|
| MH03 | +0.00115 | [+0.00021, +0.00205] | SIG worse |
| MH04 | +0.00173 | [−0.00924, +0.01270] | ns |
| MH05 | −0.00847 | [−0.01368, −0.00313] | SIG better |
| city_day_2 | +0.00782 | [+0.00010, +0.01585] | **SIG worse** |
| city_day_3 | −0.00318 | [−0.02262, +0.02173] | ns |
| city_night | −0.00144 | [−0.01652, +0.01364] | ns |

TRAIN equal-cell mean Δ: −0.00019 m (−0.07%), CI [−0.00462, +0.00440] **includes 0**.  
W/T/L (±1%): 2/3/1. **Fails city_day_2 conservation (+4.70%).**

### adaptive_arbitration_v2

| Scene | Δ mean | 95% CI | |
|---|---:|---|---|
| MH03 | +0.00112 | [−0.00059, +0.00314] | ns |
| MH04 | −0.00142 | [−0.02130, +0.01782] | ns |
| MH05 | −0.00444 | [−0.00625, −0.00113] | SIG better |
| city_day_2 | −0.00963 | [−0.01877, −0.00331] | SIG better |
| city_day_3 | −0.04799 | [−0.07635, −0.01091] | SIG better |
| city_night | −0.01045 | [−0.03136, +0.01047] | ns |

TRAIN equal-cell mean Δ: **−0.01247 m (−4.40%)**, CI **[−0.03057, −0.00101] excludes 0**.  
W/T/L (±1%): **4/2/0**. No scene >3% regression.

## Decisions

| Mechanism | Decision | Reason |
|---|---|---|
| M1 qm_only alone | **Reject as Proposed** | city_day_2 +4.70% SIG; TRAIN CI includes 0 |
| M2 arbitration_only | **Reject** (from N=3) | city_day_2 +10.3% |
| Q3 qm_stereo | **Reject** (from N=3) | city_day_3 +11.4% |
| M3 full v2 | **Proposed** | TRAIN −4.40% with CI excluding 0; SIG wins on MH05, city_day_2, city_day_3; no conservation failure |

### Phase 3.5 vs Phase 3.6 on city_day_2 / v2

Phase 3.5 N=5 reported v2 **+4.71%** on city_day_2 and rejected v2 as paper default.  
Phase 3.6 N=5 on this binary reports v2 **−5.78%** (SIG). Same seed rule `1000+N`.  
U+W means are close (0.1636 vs 0.1665); v2 means diverge (0.1713 vs 0.1569). Treat as
reproducibility risk: Proposed is locked to tag `p36-final-n5-653defa` pending a
fresh same-binary re-pair if the paper promotes v2 over fixed U+W.

## Final Proposed mechanisms

```text
Proposed = adaptive arbitration v2
         = measurement quality q_m
         + authority-aware expert arbitration
         + observability-guarded lifecycle
```

Isolated M1/M2 ablations show neither factor alone is sufficient on VIODE mid;
the retained contribution is the **joint** mechanism with N=5 paired evidence.

Fixed Semantic/U+W remains the conservative paper baseline if the Phase 3.5
city_day_2 discrepancy cannot be closed under a single frozen binary re-run.
