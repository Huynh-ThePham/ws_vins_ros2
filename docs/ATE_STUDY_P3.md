# Phase 3 weighting study

Continuation of [`ATE_STUDY_P0.md`](ATE_STUDY_P0.md). Same frozen protocol
(`scripts/run_ate_matrix.sh`), same scenes, N=3, no GT online, no failed-trial
removal. Reference binary for "adaptation off" is tag `final-p0-b46dc1f`
(`src/` at `b46dc1f`).

## Keep criteria

A change is kept only when, on the training scenes:

1. median ATE does not rise in aggregate for the proposed method,
2. success rate and trajectory coverage do not regress,
3. easy/static scenes do not regress materially,

and the hold-out is reported but not used to select.

One mechanism at a time. Factor adaptation may only be on for every method or
for none (`ADAPTATION_MODE`, `audit_method_config_diff.py`).

## Phase 3.1 — gate GeoDF out of r_dynamic unless geometry is Healthy

**Commit** `8efed9e` → **reverted** `c51f44b`. Tag `p31-hgeo-8efed9e`.

Hypothesis: an ill-conditioned F is `h_geometry`, so GeoDF Sampson must not
enter `r_dynamic` unless the scene is Healthy (same gate as hard-reject).

| scene | union_weight Δ% vs P0 |
|---|---:|
| MH_03_medium | −0.0% |
| MH_04_difficult | +0.2% |
| MH_05_difficult | +0.0% |
| city_day_2_mid | −0.0% |
| city_day_3_high | **+20.1%** |
| city_night_3_high (hold-out) | **+4.9%** |

**Rejected.** Soft GeoDF risk on `city_day_3_high` is load-bearing even when
geometry is Weak. Same conclusion family as the earlier risksep rejection.

## Phase 3.2 — uniform visual observation quality (`q_measurement`)

**Infrastructure** `6820c06`: `ADAPTATION_MODE=visual|full|off` injects
`visual_adaptive_quality` (and optionally `imu_adaptive_covariance`) into every
resolved config via `--set`, so the audit stays clean.

**Tag** `p32-visq-6820c06`, `ADAPTATION_MODE=visual`. Binary still matches P0
`src/`; only the resolved configs differ.

Existing `visualObservationWeight` (LK / forward-backward / track age) is the
mechanism. It was already implemented and off on the main matrix; this run turns
it on for baseline and union_weight together.

### vs adaptation-off (P0 final)

| scene | baseline Δ% | union_weight Δ% | verdict (union) |
|---|---:|---:|---|
| MH_03_medium | −0.5% | +2.5% | worse |
| MH_04_difficult | −2.2% | −2.0% | unchanged |
| MH_05_difficult | −1.4% | **−4.6%** | better |
| city_day_2_mid | −6.9% | −1.8% | unchanged |
| city_day_3_high | +5.8% | +3.0% | worse |
| city_night_3_high (hold-out) | −1.3% | **−6.8%** | better |
| **mean** | **−1.1%** | **−1.6%** | |

SR = 1.00 and coverage unchanged on every cell.

### Within-matrix U−B advantage

| scene | P0 (no adapt) | P3.2 (visual) |
|---|---:|---:|
| MH_03_medium | −11.9% | −9.2% |
| MH_04_difficult | −3.3% | −3.1% |
| MH_05_difficult | +0.4% | −2.9% |
| city_day_2_mid | +3.3% | +9.0% |
| city_day_3_high | −36.3% | −38.0% |
| city_night_3_high | −61.8% | −63.9% |

### Decision

**Kept as an orthogonal contribution, not folded into the U+W claim.**

- Aggregate union_weight ATE falls (−1.6% mean).
- Recovers the P0 regression on `MH_05_difficult` (−4.6%).
- Hold-out improves (−6.8%).
- Paper protocol already forbids enabling quality on "ours" only; the fair form is
  this uniform matrix, reported separately from the fixed-backbone main claim
  (same rule as `full_adaptive`).
- Commons stay at `visual_adaptive_quality: 0` so the main claim matrix remains
  unconfounded. Reproduce with:

```bash
PROTOCOL_TAG=p32-visq-<sha> ADAPTATION_MODE=visual bash scripts/run_ate_matrix.sh
```

Caveats: `MH_03_medium` and `city_day_3_high` still show small union regressions
under adaptation; `city_day_2_mid` still has union worse than baseline in both
matrices. Three trials per cell.

## Phase 3.2b — uniform full adaptation (visual + IMU)

**Tag** `p32-full-9e50597`, `ADAPTATION_MODE=full`.

| aggregate | vs P0 | vs visual-only |
|---|---:|---:|
| baseline mean Δ% | −1.0% | +0.0% |
| union_weight mean Δ% | −1.0% | **+0.7%** |

IMU covariance on top of visual quality hurts `MH_04` (+6.1% vs visual) and
`MH_05` (+2.4%). **Rejected as an addition to visual-only.** Prefer
`ADAPTATION_MODE=visual`.

## Phase 3.3 — lifecycle DownWeight → residual scale

**Commit** `93ae100`, tag `p33-life-93ae100`.

`Action::DownWeight` was telemetry only. It now multiplies the applied backend
weight by `sem_lifecycle_downweight_scale` (commons default 0.55). Baseline is
unaffected (`sem_geodf_backend_weight=0`). Adaptation off.

| scene | union_weight Δ% vs P0 |
|---|---:|
| MH_03_medium | +0.2% |
| MH_04_difficult | −0.2% |
| MH_05_difficult | **−2.9%** |
| city_day_2_mid | **−8.8%** |
| city_day_3_high | +6.4% |
| city_night_3_high (hold-out) | **−5.6%** |
| **mean** | **−1.8%** |

SR = 1.00 everywhere. **Kept:** aggregate and hold-out improve; recovers the P0
regressions on `MH_05` and `city_day_2_mid`. `city_day_3_high` gives back some of
the P0 gain (0.2246 → 0.2389) but remains below BEFORE (0.2697).

## Full publication ablation

**Tag** `pub-ablation-c560406` on HEAD with P0 + Phase 3.3 lifecycle scale,
`ADAPTATION_MODE=off`. Methods: baseline / geodf / semantic / union_noweight /
union_weight. 90/90 trials, SR = 1.00, coverage matched.

Median ATE (Δ% vs baseline in parentheses):

| scene | baseline | geodf | semantic | union_noweight | union_weight |
|---|---:|---:|---:|---:|---:|
| MH_03_medium | 0.2910 | 0.2706 (−7.0%) | 0.2591 (−11.0%) | 0.2759 (−5.2%) | **0.2569 (−11.7%)** |
| MH_04_difficult | 0.4493 | 0.4457 (−0.8%) | 0.4367 (−2.8%) | 0.4496 (+0.1%) | **0.4339 (−3.4%)** |
| MH_05_difficult | 0.3016 | 0.3014 (−0.1%) | 0.2989 (−0.9%) | **0.2970 (−1.5%)** | 0.3039 (+0.8%) |
| city_day_2_mid | 0.1738 | 0.1655 (−4.8%) | **0.1439 (−17.2%)** | 0.1446 (−16.8%) | 0.1789 (+2.9%) |
| city_day_3_high | 0.3528 | 0.3120 (−11.5%) | 0.2400 (−32.0%) | **0.2322 (−34.2%)** | 0.2388 (−32.3%) |
| city_night_3_high | 0.9012 | 0.8558 (−5.0%) | **0.3250 (−63.9%)** | 0.3944 (−56.2%) | 0.3602 (−60.0%) |

Mean Δ% vs baseline over six scenes: geodf −4.9%, semantic **−21.3%**,
union_noweight −19.0%, union_weight −17.3%.

Honest reading for the paper row:

- On EuRoC MH03/MH04, **U+W is best**.
- On high-dynamic VIODE, **U (noweight) or semantic** beat U+W slightly;
  backend weighting is not free.
- `city_day_2_mid` still shows U+W worse than baseline; semantic/U help there.
- Hold-out is dominated by the semantic expert (~−60%).

Raw table: `results/sem_geodf_ablation/pub-ablation-c560406/ATE_TABLE.md`.

## Shipped stack after Phase 3

| layer | status |
|---|---|
| P0 correctness | kept (`b46dc1f` math) |
| P3.1 h_geometry risk gate | rejected |
| P3.2 visual quality (uniform) | kept as orthogonal matrix |
| P3.2b full (visual+IMU) | rejected vs visual-only |
| P3.3 lifecycle DownWeight scale | **kept in default commons** |
| Full method ablation | recorded at `pub-ablation-c560406` |

## Shipped recommendation

| claim row | binary / config |
|---|---|
| Fixed-backbone U+W | `src/` at `b46dc1f` (= HEAD after Phase 3.1 revert), adaptation off |
| Visual quality extension | same binary, `ADAPTATION_MODE=visual` for every method |
| Rejected | risksep, P3.1 h_geometry gate, continuous-time Huber EMA |
