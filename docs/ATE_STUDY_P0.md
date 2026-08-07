# P0 correctness pass: measured effect on ATE

This records the frozen before/after study around the P0 correctness fixes. It is
written so a reader can reproduce every number without trusting a summary.

## Protocol

One protocol tag names exactly one binary. `scripts/run_ate_matrix.sh` refuses to
start when `src/` is dirty and writes the resolved `git_sha` into `RUN_META.json`
next to the results, so a tag cannot silently mix builds.

| item | value |
|---|---|
| train scenes | EuRoC `MH_03_medium`, `MH_04_difficult`, `MH_05_difficult`; VIODE `city_day/2_mid`, `city_day/3_high` |
| hold-out scene | VIODE `city_night/3_high` |
| methods | `baseline` (empty overlay), `union_weight` (Sem-GeoDF) |
| trials per cell | 3 |
| bag rate | 1.0, `use_sim_time:=true` |
| reported | median ATE, mean±std, success rate, trajectory coverage, failure-penalized ATE |

Nothing in the runtime path reads ground truth, sequence names, difficulty labels
or VIODE dynamic levels. Ground truth is only opened by the offline evaluator
after a run has finished.

Failed and diverged trials stay in the denominator. `summarize_before_ate.py`
counts attempted trials from `run_manifest.json` rather than from `metrics.json`,
so a trial that crashed before writing metrics still lowers the success rate
instead of disappearing. The failure-penalized ATE charges 10 m for every
non-successful trial.

`compare_ate_matrices.py` refuses to call a cell an improvement when trajectory
coverage drops by more than 2% or when the success rate falls, so a shorter run
cannot be presented as a lower error.

## What changed

HEAD before: `92c48f8` — HEAD after: `295648e`. The binary that ships is the tree
at `b46dc1f`, benchmarked as tag `final-p0-b46dc1f`; `d5b8bed` and its revert
`295648e` leave `src/` byte-identical to it.

| commit | contents |
|---|---|
| `2d7ef9e` | P0.1 design-matrix conditioning, P0.2 fail-closed Sampson, P0.3 true stereo reprojection |
| `0b1f006` | P0.4 signed-component MAD scale and continuous-time Huber EMA |
| `33fa40a` | P0.5 Evaluate-based weighted Jacobian checks, P0.6 marginalization integration test |
| `36bb8cf` | benchmark tooling and the hold-out waiter deadlock fix |
| `29754d4`, `1023dc5`, `ea61344`, `654feeb`, `32f2751` | paper-build reproducibility and LaTeX toolchain |
| `1e0306c` | frozen matrix protocol and guarded comparison |

### Defects found and corrected

1. **Correspondence pairs were desynchronised before the SVD.** Each view was
   Hartley-normalised independently, and each dropped its own non-finite points,
   so row *i* of the design matrix could pair unrelated observations. Pairs are
   now filtered jointly before normalisation.
2. **σ₉ was read from a thin SVD that does not always produce it.** With exactly
   eight correspondences the decomposition yields eight singular values, so the
   nullspace gap was computed from uninitialised or out-of-range data. κ_eff is
   now reported whenever σ₈ exists and the gap is explicitly marked unavailable.
3. **The conditioning gate scored all correspondences, including RANSAC
   outliers.** It now scores inliers; the all-correspondence metrics remain as a
   debug diagnostic.
4. **A degenerate Sampson denominator returned 0.0**, which is the value of a
   perfect inlier, so unobservable epipolar geometry was silently trusted. It now
   returns an invalid result with infinite squared distance, and every call site
   treats invalid as a non-inlier.
5. **Stereo reprojection did not use the camera model.** The triangulated point
   was compared against normalized-plane coordinates scaled by a focal constant,
   which is not the projection the estimator optimises. Production now reprojects
   with `Camera::spaceToPlane` against the original pixel measurements.
6. **Parallax was gated on `atan2(baseline, depth)`**, a proxy that ignores where
   the rays actually point. The gate is now the true angle between bearings, with
   the depth proxy kept only to reject absurdly distant triangulations.
7. **The robust scale was estimated from folded |r| samples** and then mixed a MAD
   estimate with a `median(|r|)/0.6745` estimate, applying the 1.4826 Gaussian
   constant to a distribution it does not describe. Signed whitened components are
   now used with MAD alone.
8. **The Huber delta EMA advanced once per optimisation**, so its adaptation rate
   depended on frame rate and on dropped frames. It is now continuous-time.
9. **Every projection factor's `check()` re-derived the residual by hand and
   omitted `sqrt_weight` on the finite-difference side**, so a Jacobian block that
   dropped the weight still compared clean. Both sides now come from `Evaluate`.
10. **Marginalization had no integration coverage**, only a constness assertion on
    `sqrt_weight`.

### Tests

`ctest` in `build/pht_vio`: 17/17 pass, including six added or rewritten here —
`geodf_degeneracy`, `stereo_validity`, `robust_scale_mc`, `temporal_smoothing`,
`marginalization_integration`, and the three projection gradient suites now
sweeping w ∈ {1, 0.75, 0.25, 1e-4, 0}.

`python3 -m unittest discover -s scripts/tests`: 55/55 pass.

## Results

Every cell below ran 3/3 successfully with identical trajectory coverage before
and after, so no comparison is confounded by a shortened or aborted run.

### BEFORE — `92c48f8`, tag `before-ate-92c48f8`

| scene | method | ok/attempted | median ATE | mean±std | coverage |
|---|---|---:|---:|---|---:|
| MH_03_medium | baseline | 3/3 | 0.2906 | 0.2906±0.0001 | 1165 |
| MH_03_medium | union_weight | 3/3 | 0.2575 | 0.2575±0.0000 | 1165 |
| MH_04_difficult | baseline | 3/3 | 0.4494 | 0.4494±0.0000 | 856 |
| MH_04_difficult | union_weight | 3/3 | 0.4342 | 0.4437±0.0164 | 856 |
| MH_05_difficult | baseline | 3/3 | 0.3008 | 0.3008±0.0000 | 976 |
| MH_05_difficult | union_weight | 3/3 | 0.2921 | 0.2937±0.0060 | 976 |
| city_day_2_mid | baseline | 3/3 | 0.1711 | 0.1723±0.0022 | 654 |
| city_day_2_mid | union_weight | 3/3 | 0.1726 | 0.1722±0.0007 | 654 |
| city_day_3_high | baseline | 3/3 | 0.4205 | 0.4207±0.0025 | 654 |
| city_day_3_high | union_weight | 3/3 | 0.2697 | 0.2503±0.0352 | 654 |
| city_night_3_high (hold-out) | baseline | 3/3 | 0.9016 | 0.9016±0.0000 | 608 |
| city_night_3_high (hold-out) | union_weight | 3/3 | 0.3369 | 0.3370±0.0002 | 608 |

### AFTER — `36bb8cf`, tag `after-p0-36bb8cf`

| scene | method | ok/attempted | median ATE | mean±std | coverage |
|---|---|---:|---:|---|---:|
| MH_03_medium | baseline | 3/3 | 0.2910 | 0.2908±0.0003 | 1165 |
| MH_03_medium | union_weight | 3/3 | 0.2564 | 0.2576±0.0021 | 1165 |
| MH_04_difficult | baseline | 3/3 | 0.4493 | 0.4492±0.0000 | 856 |
| MH_04_difficult | union_weight | 3/3 | 0.4346 | 0.4346±0.0000 | 856 |
| MH_05_difficult | baseline | 3/3 | 0.3016 | 0.3019±0.0005 | 976 |
| MH_05_difficult | union_weight | 3/3 | 0.3028 | 0.2996±0.0065 | 976 |
| city_day_2_mid | baseline | 3/3 | 0.1738 | 0.1738±0.0000 | 654 |
| city_day_2_mid | union_weight | 3/3 | 0.1796 | 0.1797±0.0001 | 654 |
| city_day_3_high | baseline | 3/3 | 0.3528 | 0.3937±0.0717 | 654 |
| city_day_3_high | union_weight | 3/3 | 0.2488 | 0.2524±0.0298 | 654 |
| city_night_3_high (hold-out) | baseline | 3/3 | 0.9012 | 0.9012±0.0000 | 608 |
| city_night_3_high (hold-out) | union_weight | 3/3 | 0.3435 | 0.3362±0.0133 | 608 |

### Change

Full table in `results/sem_geodf_ablation/COMPARE_before_after_p0.md`.

| scene | baseline Δ% | union_weight Δ% |
|---|---:|---:|
| MH_03_medium | +0.2% | −0.4% |
| MH_04_difficult | −0.0% | +0.1% |
| MH_05_difficult | +0.3% | +3.6% |
| city_day_2_mid | +1.6% | +4.1% |
| city_day_3_high | −16.1% | −7.8% |
| city_night_3_high (hold-out) | −0.0% | +2.0% |
| **mean over cells** | **−2.4%** | **+0.3%** |

This is the intermediate state that still carried the continuous-time Huber EMA.
The ablation below shows that EMA was the reason `city_day_3_high/union_weight`
only improved 7.8% here instead of the 16.7% the front-end work delivers on its
own, and the shipped configuration drops it.

Two caveats belong with any reading of these tables. First,
`city_day_3_high/baseline` improved in median but its spread widened from ±0.0025
to ±0.0717 (trials 0.3518, 0.3528, 0.4764), so the improvement is less stable than
the median suggests. Second, three trials per cell cannot separate a 2–4% shift
from run-to-run variation.

The P0 changes are kept because each corrects a defect that is wrong independent
of its ATE effect: a metric read past the end of a spectrum, a degenerate geometry
reported as a perfect inlier, a reprojection that does not use the camera model,
and a Gaussian constant applied to folded samples.

## Ablation

`baseline` leaves `geodf_enable` and `sem_enable` at 0, so P0.1 and P0.2 cannot
reach it; its before/after difference isolates P0.3 and P0.4. A third full matrix
with P0.4 reverted (`ablation/p0-no-robust-scale`, tag `abl-noP04-84c9bf2`)
separates the front-end geometry work from the robust-scale work. P0.5 and P0.6
are test-only and cannot change any trajectory.

Every arm ran 36/36 trials with success rate 1.00 and unchanged coverage.

Median ATE per arm:

| scene | method | no P0 | P0.1–P0.3 | + P0.4 |
|---|---|---:|---:|---:|
| MH_03_medium | baseline | 0.2906 | 0.2910 | 0.2910 |
| MH_03_medium | union_weight | 0.2575 | 0.2564 | 0.2564 |
| MH_04_difficult | baseline | 0.4494 | 0.4493 | 0.4493 |
| MH_04_difficult | union_weight | 0.4342 | 0.4346 | 0.4346 |
| MH_05_difficult | baseline | 0.3008 | 0.3024 | 0.3016 |
| MH_05_difficult | union_weight | 0.2921 | 0.3027 | 0.3028 |
| city_day_2_mid | baseline | 0.1711 | 0.1738 | 0.1738 |
| city_day_2_mid | union_weight | 0.1726 | 0.1796 | 0.1796 |
| city_day_3_high | baseline | 0.4205 | 0.3528 | 0.3528 |
| city_day_3_high | union_weight | 0.2697 | 0.2245 | 0.2488 |
| city_night_3_high (hold-out) | baseline | 0.9016 | 0.9012 | 0.9012 |
| city_night_3_high (hold-out) | union_weight | 0.3369 | 0.3538 | 0.3435 |

**P0.1–P0.3 (front-end geometry)** own the entire improvement: −16.1% on
`city_day_3_high` for `baseline` and −16.8% for `union_weight`. Because P0.1 and
P0.2 are inert in `baseline`, the identical size of the `baseline` gain shows the
mechanism is the corrected stereo reprojection and parallax gate (P0.3), not the
GeoDF conditioning or Sampson changes. They also own the regressions:
`MH_05_difficult` +3.6%, `city_day_2_mid` +4.0% and hold-out +5.0% for
`union_weight`.

**P0.4 (robust scale)** left ten of twelve cells identical to four decimal places.
Its only effects were +10.8% on `city_day_3_high/union_weight` and −2.9% on the
hold-out. The ten unchanged cells confirm that the signed-component MAD is
numerically equivalent to the previous folded-sample estimator for symmetric
residuals — both recover σ — so the measured differences came from the
continuous-time EMA, not from the scale estimator.

Per-arm comparisons: `results/sem_geodf_ablation/COMPARE_p0_frontend.md` and
`COMPARE_p0_robust_scale.md`.

### Shipped configuration

Dropping the continuous-time EMA and keeping everything else gives tag
`final-p0-b46dc1f`, which is what the branch ships. It recovers the full front-end
gain that the EMA had eroded:

| scene | method | BEFORE | shipped | Δ% |
|---|---|---:|---:|---:|
| MH_03_medium | baseline | 0.2906 | 0.2910 | +0.2% |
| MH_03_medium | union_weight | 0.2575 | 0.2564 | −0.4% |
| MH_04_difficult | baseline | 0.4494 | 0.4493 | −0.0% |
| MH_04_difficult | union_weight | 0.4342 | 0.4346 | +0.1% |
| MH_05_difficult | baseline | 0.3008 | 0.3016 | +0.3% |
| MH_05_difficult | union_weight | 0.2921 | 0.3027 | +3.6% |
| city_day_2_mid | baseline | 0.1711 | 0.1738 | +1.6% |
| city_day_2_mid | union_weight | 0.1726 | 0.1795 | +4.0% |
| city_day_3_high | baseline | 0.4205 | 0.3528 | **−16.1%** |
| city_day_3_high | union_weight | 0.2697 | 0.2246 | **−16.7%** |
| city_night_3_high (hold-out) | baseline | 0.9016 | 0.9012 | −0.0% |
| city_night_3_high (hold-out) | union_weight | 0.3369 | 0.3446 | +2.3% |
| **mean over cells** | baseline | | | **−2.4%** |
| **mean over cells** | union_weight | | | **−1.2%** |

All 36 trials succeeded in every arm and coverage never moved, so no cell in this
table is comparing trajectories of different length.

## Rejected changes

**Continuous-time Huber delta EMA** (part of `0b1f006`, reverted in `b46dc1f`).
Judged on training cells only, it changed nothing on four of five and cost 10.8%
median ATE on the fifth. The hold-out improvement of 2.9% was deliberately not
used to justify keeping it, since selecting on the hold-out would invalidate it.
It also reinterpreted a per-frame smoothing constant against a hard-coded 20 Hz
reference that no configuration declares. The signed-component MAD scale from the
same commit is kept: it is the correct estimator and is measurably free.

**Stacked-norm stereo reprojection residual.** The P0.3 draft reported
`||[e0; e1]||₂` where the previous code reported the per-view RMS
`sqrt(0.5(||e0||² + ||e1||²))`. That silently tightened `stereo_reprojection_max_px`
by a factor of √2 while leaving the configured value at 2.0. Both code paths keep
the per-view RMS convention so the threshold retains its calibration.

**Separating measurement quality from dynamic risk for unmeasurable Sampson
residuals** (`d5b8bed`, reverted in `295648e`, tag `risksep-d5b8bed`). The
argument was principled: a residual with no computable denominator means the
epipolar geometry is unobservable, which says nothing about whether the feature
moved, yet the fail-closed change routed that condition into the mover share,
hard-reject candidacy and the risk confidence as if it were motion evidence.
Excluding it from all three left ten of twelve cells unchanged, did not recover
any of the regressions it was meant to explain, and cost 24.8% median ATE on
`city_day_3_high/union_weight`. The measurement contradicts the argument: in
high-dynamic scenes a track on a moving object frequently does produce degenerate
geometry with respect to the static-scene fundamental matrix, so "unmeasurable"
carries real motion evidence there. The aggressive treatment stands, and the
regressions on `MH_05_difficult`, `city_day_2_mid` and the hold-out remain
unexplained.

**Gating all GeoDF contributions to r_dynamic on Healthy geometry** (`8efed9e`,
reverted in `c51f44b`, tag `p31-hgeo-8efed9e`). Phase 3.1 argued that an
ill-conditioned F is an `h_geometry` problem, so GeoDF Sampson should not enter
`r_dynamic` unless the scene is Healthy — matching the existing hard-reject gate.
Against `final-p0-b46dc1f` the change left baseline cells bit-identical and cost
`union_weight` **+20.1%** median ATE on the training cell `city_day_3_high` and
**+4.9%** on the hold-out. GeoDF soft risk on that scene is load-bearing even when
geometry is Weak; stripping it removes the proposed method's gain. Reverted.

**Turning the nullspace gap into a gate.** `geodf_min_nullspace_gap` ships at 0,
so g_F = σ₈/σ₉ is telemetry. Making it a rejection criterion has not been
benchmarked and would be a second mechanism changing at the same time.

## Reproducing

```bash
PROTOCOL_TAG=<tag> bash scripts/run_ate_matrix.sh
python3 scripts/summarize_before_ate.py \
  --root results/sem_geodf_ablation/<tag> --out results/sem_geodf_ablation/<tag>/ATE_TABLE.md
python3 scripts/compare_ate_matrices.py \
  --before results/sem_geodf_ablation/<before-tag>/BEFORE_TABLE.json \
  --after  results/sem_geodf_ablation/<tag>/ATE_TABLE.json \
  --out    results/sem_geodf_ablation/COMPARE.md
```

Result trees are ignored by git (`results/*`), so the tables above are the record.

## Limitations

- Three trials per cell. Differences below roughly 5% are not separated from run
  variance by this sample size.
- The matrix covers six scenes from two datasets. `city_night/3_high` is the only
  hold-out, so hold-out conclusions rest on one environment.
- Per-item ablation is partial. P0.4 has its own arm and P0.5/P0.6 are test-only,
  but P0.1 and P0.2 are separated from P0.3 only through the `baseline` arm, which
  assumes the mechanisms compose additively.
- `geodf_min_nullspace_gap` ships at 0, so the nullspace gap is telemetry. Turning
  it into a gate has not been benchmarked.
- The regressions on `MH_05_difficult` (+3.6%), `city_day_2_mid` (+4.0%) and the
  hold-out (+2.3%) are attributed to P0.1–P0.3 but not explained. The one
  hypothesis tested — that fail-closed Sampson was over-reporting motion — was
  measured and refuted.
- The net effect on the proposed method is −1.2% mean over six cells, carried
  almost entirely by one scene. This is a correctness pass whose ATE effect
  happens to be favourable, not an accuracy result.
