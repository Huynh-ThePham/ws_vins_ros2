# Phase 3.6 — N=3 mechanism isolation results

Tag: `p36-mech-n3-59440f8`  
Binary SHA: `59440f8ccbc57eae257fb93d9df5f0eaaaa9ecfd`  
Matrix: 6 scenes × 5 methods × N=3 = **90/90** success, coverage conserved (≥0.98).

## Isolation table (ATE RMSE mean±std, m)

| Scene | M0 U+W | M1 q_m | M2 arb | M3 full v2 | Q3 qm_stereo |
|---|---:|---:|---:|---:|---:|
| MH_03_medium | 0.2569±0.0000 | 0.2581±0.0000 | 0.2571±0.0023 | 0.2568±0.0001 | **0.2502±0.0001** |
| MH_04_difficult | 0.4531±0.0167 | 0.4431±0.0149 | **0.4345±0.0001** | **0.4332±0.0127** | 0.4409±0.0000 |
| MH_05_difficult | 0.2962±0.0057 | **0.2897±0.0053** | 0.2983±0.0055 | 0.2937±0.0052 | 0.2970±0.0000 |
| city_day_2_mid | 0.1731±0.0089 | 0.1770±0.0010 | 0.1910±0.0214 | **0.1524±0.0011** | 0.1733±0.0331 |
| city_day_3_high | 0.2387±0.0006 | 0.2408±0.0003 | 0.2371±0.0555 | **0.2256±0.0504** | 0.2660±0.0331 |
| city_night_3_high | 0.3353±0.0148 | **0.3284±0.0015** | 0.3332±0.0141 | **0.3274±0.0122** | 0.3404±0.0035 |

Paired % vs U+W (mean):

| Scene | ΔE_qm% | ΔE_arb% | ΔE_full% | Q3% |
|---|---:|---:|---:|---:|
| MH03 | +0.46 | +0.08 | −0.03 | **−2.62** |
| MH04 | **−2.21** | **−4.10** | **−4.40** | −2.70 |
| MH05 | **−2.20** | +0.71 | −0.86 | +0.25 |
| city_day_2 | +2.29 | **+10.34** | **−11.98** | +0.14 |
| city_day_3 | +0.88 | −0.67 | −5.50 | **+11.43** |
| city_night | **−2.06** | −0.64 | −2.35 | +1.52 |

## Mechanism decisions (TRAIN selection freeze)

### Keep for N≥5

1. **M1 `qm_only`** — isolated measurement-quality gain on MH04, MH05, city_night
   (≥3 scenes), ≤2.3% regression elsewhere, low variance. Paper-facing claim
   candidate: *degradation-aware visual measurement quality*.

2. **M3 `adaptive_arbitration_v2`** — best aggregate at N=3, but Phase 3.5 N=5
   previously saw city_day_2 +4.71%. Must re-check at N=5 before any claim.
   Interaction on city_day_2 (arb alone hurts, combined helps) is the Phase 3.5
   confound in reverse and is **not** yet trusted.

### Reject / hold

3. **M2 `arbitration_only`** — REJECT as standalone. city_day_2 +10.3% with high
   variance; without `q_m` the dynamic path is harmful on mid-dynamic VIODE.

4. **Q3 `qm_stereo`** — HOLD / reject for Proposed. Strong MH03/MH04 signal but
   city_day_3 +11.4% fails conservation. Stereo covariance needs a redesign
   before re-entry (likely over-downweights valid far stereo in high-dynamic).

## N=5 protocol (frozen before opening)

```text
METHODS="union_weight qm_only adaptive_arbitration_v2"
N=5
ADAPTATION_MODE=off
scenes: MH03 MH04 MH05 + city_day_2/3 + city_night hold-out
```

Proposed after N=5: only mechanisms whose paired CI excludes zero harm and that
win on ≥2 scenes without >5% regression on any important scene.
