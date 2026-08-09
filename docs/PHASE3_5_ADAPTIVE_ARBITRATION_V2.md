# Phase 3.5 — Authority-aware adaptive expert arbitration v2

## Decision

**Rejected as the paper default; retained as a reproducible architecture ablation.**

The frozen F1/G2 v2 lowers the paired TRAIN mean ATE by 3.77% at N=5 and
substantially fixes the Phase 3.4 `city_day_3_high` regression. It nevertheless
misses a mandatory conservation criterion: `city_day_2_mid` regresses by 4.71%
against fixed U+W at N=5. The 95% paired-cell bootstrap interval also includes
zero. The main paper interpretation therefore remains fixed Semantic/U+W; this
result does not support a stronger adaptive-arbitration claim.

Phase 3.4 remains available as `adaptive_arbitration` (v1). Phase 3.5 is a
separate `adaptive_arbitration_v2` method and does not overwrite v1 or its
results.

## Architecture

### Independent reliability

Semantic reliability has no Semantic–GeoDF overlap input:

\[
q_s=\exp\left(\sum_j \alpha_j\log(\max(\epsilon,q_j))\right),
\quad
\alpha=(.10,.22,.13,.10,.20,.15,.10),
\]

where the components are availability, freshness, soft mask saturation,
confidence, temporal Semantic status consistency, support, and mask health.
The saturation component stays at one through dynamic-pixel ratio 0.65, makes
a smooth transition, and reaches a 0.15 floor at 0.92. Thus ordinary 20–40%
dynamic masks are not treated as unreliable.

GeoDF receives raw, independent qualities
`q_kappa`, `q_inlier`, `q_parallax`, `q_coverage`, and `q_measurable`. The
TRAIN-selected G2 combination is

\[
q_g^*=\exp\left(
  \frac{\sum_j \alpha_j\log(\max(\epsilon,q_j))}{\sum_j\alpha_j}
\right),\quad
\alpha=(.25,.25,.20,.15,.15),
\]

followed by a health ceiling, not another product:

\[
q_g=\min(q_g^*, c_{health}),\qquad
c_{health}\in\{1.0,0.65,0.25\}.
\]

Measurement quality remains a separate factor, `q_m`. For every action,
including KEEP,

\[
w_d=1-\alpha(q_{authority})\,
  \operatorname{smoothstep}(r_d;0.25,0.85),
\]

\[
w=\operatorname{clamp}(q_m w_d,0.05,1).
\]

KEEP therefore means `w_d=1` when no dynamic penalty is warranted; it never
restores a poor measurement to 0.999.

### Agreement, authority, and fusion

Agreement is explicit arbitration evidence,
`agreement = 1 - abs(r_s-r_g)`. It changes neither `q_s` nor `q_g`.
Authority is selected as Semantic, GeoDF, Joint, or None using reliability
thresholds of 0.70 and an agreement threshold of 0.65. A sole reliable expert
can become authoritative. Two healthy experts that disagree enter quarantine
instead of immediate rejection.

The selected F1 fusion uses the sole authoritative expert directly. Otherwise:

\[
r_d=\frac{q_s r_s+q_g r_g}{q_s+q_g+\epsilon}.
\]

F0 reliability-gated noisy-OR and F2 log-odds remain implemented ablations.
Observability and redundancy do not alter `r_d`; they only guard the action.
A single expert can request hard rejection only after high risk, reliability,
persistence, track age, observability, and redundancy checks. The lifecycle
then enforces a three-frame dwell and a minimum-surviving-track deletion budget.

The lifecycle states are `TRUSTED`, `SUSPECT`, `DISAGREEMENT`, `DOWNWEIGHTED`,
`QUARANTINED`, `REJECTED`, and `RECOVERING`. Only the v2 lifecycle can create
v2 irreversible reject candidates. The marginalized prior is not reweighted.

## Stage A selection (N=1, TRAIN only)

F1/G2 was frozen before opening the legacy hold-out. It offered the best
multi-scene balance: 0.2614/0.4478/0.1394/0.1958 m on MH03, MH04,
`city_day_2_mid`, and `city_day_3_high`. F0/G2, F2/G2, F1/G1, and F1/G3 were
rejected. F0 was locally best on MH04 and G3 locally best on MH03, but neither
was selected from one-scene wins. No thresholds were changed after the freeze.

## Stage B — full N=3 matrix

ATE is RMSE in metres, mean ± sample standard deviation. All 84/84 runs passed.

| Scene | Baseline | GeoDF | Semantic | U no weight | U+W | v1 | v2 |
|---|---:|---:|---:|---:|---:|---:|---:|
| MH03 | .281407±.016205 | .270579±.000167 | .257048±.003841 | .275888±.000059 | .256898±.000020 | .258637±.000029 | .256938±.000019 |
| MH04 | .449260±.000000 | .445694±.000002 | .451955±.000001 | .450794±.001048 | .453155±.016708 | .432852±.000006 | .440274±.013026 |
| city_day_2 | .172561±.002087 | .165547±.000027 | .144858±.000010 | .147174±.004459 | .166208±.011776 | .170045±.011266 | .168776±.018611 |
| city_day_3 | .352641±.000127 | .311883±.000278 | .260094±.020847 | .250969±.032434 | .243796±.007951 | .266834±.009328 | .238692±.046041 |

Across the 12 paired TRAIN cells, U+W is 0.280014 m and v2 is 0.276170 m
(-1.37%). The paired delta 95% bootstrap CI is
[-0.016927, +0.008704] m (100,000 resamples, seed 3500). With a ±1% scene tie
band, v2 records 2 wins / 1 tie / 1 loss.

## Stage C — paired N=5

All 20/20 paired TRAIN cells passed and reuse the same trial/seed pairs.

| Scene | U+W ATE | v2 ATE | v2 delta |
|---|---:|---:|---:|
| MH03 | .256908±.000021 | .257014±.000105 | +0.04% |
| MH04 | .456913±.012887 | .443910±.010482 | -2.85% |
| city_day_2 | .163623±.009071 | .171329±.021383 | **+4.71%** |
| city_day_3 | .264292±.028753 | .226410±.037469 | **-14.33%** |

The equal-cell paired mean changes from 0.285434 m to 0.274666 m (-3.77%).
The mean paired delta is -0.010768 m with 95% bootstrap CI
[-0.025876, +0.002743] m (100,000 resamples, seed 3500); the interval does not
establish a non-zero aggregate improvement. Scene W/T/L remains 2/1/1.

The earlier Phase 3.4 `city_day_2` gain did not reproduce in this frozen N=3
matrix (v1 itself is 2.31% worse than U+W), and v2 is 0.75% better than v1 at
N=3. At N=5, however, v2 is 4.71% worse than U+W. The required preservation of
the `city_day_2` benefit is therefore not demonstrated, which determines the
negative final decision despite the better aggregate mean.

## Legacy hold-out and telemetry

`city_night_3_high` carries the required disclaimer: **legacy hold-out has been
observed in prior research iteration**. Parameters were frozen before this
Phase 3.5 evaluation. N=3 gives U+W 0.346529±0.000272 m, v1
0.349286±0.000418 m, and v2 0.332781±0.000321 m; v2 is 3.97% better than U+W.
Attempts to extend it to N=5 were excluded because the execution sandbox denied
ROS 2 sockets/GPU access; they are listed in `excluded_attempts.csv` and are not
scientific failures. Every locally available alternative VIODE/EuRoC scene had
already been observed in earlier research iterations, so no scene is presented
as a genuinely untouched hold-out.

Telemetry below aggregates all five v2 TRAIN trials and three clean legacy
hold-out trials, weighted by tracks for means.

| Scene | q_s mean/med | q_g mean/med | Sem/Geo/Joint/None authority | Disagree | Keep/Down/Quarantine/Reject | q_m | w_d | w_final |
|---|---:|---:|---:|---:|---:|---:|---:|---:|
| MH03 | .979/.981 | .953/1.000 | 6.0/0.0/93.1/0.8% | .7% | 99.3/.01/.7/0.0% | .936 | 1.000 | .898 |
| MH04 | .961/.961 | .924/1.000 | 9.7/0.0/89.2/1.0% | .8% | 99.2/.001/.8/0.0% | .944 | 1.000 | .907 |
| city_day_2 | .962/.961 | .970/1.000 | 3.5/.1/94.5/1.9% | 1.3% | 98.7/.01/1.3/0.0% | .889 | 1.000 | .851 |
| city_day_3 | .960/.961 | .965/1.000 | 4.2/.1/93.4/2.4% | 1.8% | 98.1/.06/1.8/0.0% | .886 | .999 | .847 |
| city_night_3 | .955/.961 | .943/1.000 | 7.1/0.0/90.8/2.1% | 1.6% | 98.4/.02/1.6/0.0% | .894 | .999 | .853 |

The logs answer the requested mechanism checks directly: `q_s` remains high
without using overlap, `q_g` does not collapse, measurement quality remains in
the final weight, and the lifecycle does not block Semantic authority. Hard
reject is 0% in these runs because almost all evidence stays KEEP and healthy
disagreement goes to quarantine; observed changes primarily arise from retained
measurement quality, recovery, and the small quarantine population—not from
aggressive feature deletion.

## Reproducibility and safety

The compact evidence bundle is in `experiments/phase3_5/`:

- `per_trial_metrics.csv`: 137 clean manifests covering Stage A, the complete
  N=3 matrix, N=5 paired extension, and legacy hold-out;
- `run_manifest_hashes.csv`: manifest and resolved-config SHA-256 per run;
- `expected_train_n3_matrix.json`: fail-closed 4-scene × 7-method declaration;
- `telemetry_aggregate.csv`: requested authority/action/reliability/weight data;
- `analysis.json`: exact scene statistics, every paired delta, deterministic
  bootstrap settings/results, W/T/L, success, and coverage;
- `excluded_attempts.csv`: transparent infrastructure-attempt audit;
- `SHA256SUMS`: hashes of the committed compact artifacts.

The included scientific cells have 100% success and minimum temporal trajectory
coverage 0.977491. No ground truth enters runtime code, and production logic has
no sequence-name, difficulty-label, or scene-specific branch. GT is read only by
the offline evaluator after a trajectory is produced. No GT dataset or large
trajectory is committed.
