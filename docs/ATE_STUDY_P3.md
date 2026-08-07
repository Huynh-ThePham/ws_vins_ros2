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

## Not yet run

- `ADAPTATION_MODE=full` (visual + IMU covariance together).
- Phase 3.3 lifecycle / redundancy-aware drop changes (would be a third
  mechanism; deferred after two risk-channel edits failed keep criteria).

## Shipped recommendation

| claim row | binary / config |
|---|---|
| Fixed-backbone U+W | `src/` at `b46dc1f` (= HEAD after Phase 3.1 revert), adaptation off |
| Visual quality extension | same binary, `ADAPTATION_MODE=visual` for every method |
| Rejected | risksep, P3.1 h_geometry gate, continuous-time Huber EMA |
