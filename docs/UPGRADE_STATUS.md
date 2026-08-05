# Sem-GeoDF upgrade status

Tracks `Sem-GeoDF Upgrade Plan.docx` against what is actually in the repository.
Every "done" row below is backed by code plus a test that fails if it regresses.

Reproduce the whole verification locally:

```bash
source /opt/ros/humble/setup.bash
colcon build --packages-up-to pht_vio --cmake-args -DBUILD_TESTING=ON
cd build/pht_vio && ctest --output-on-failure && cd -
python3 -m unittest discover -s scripts/tests
python3 scripts/audit_method_config_diff.py --base src/config/paper/viode_common.yaml
python3 scripts/audit_method_config_diff.py --base src/config/paper/euroc_common.yaml
python3 scripts/audit_publication_hygiene.py
```

Current: **ctest 14/14**, **Python 47/47**, both audits PASS.

---

## P0 — blocking correctness

| Item | Status | Where |
|---|---|---|
| P0.1 Remove confounded ablation | done | `src/config/paper/` (one backbone + 6 overlays), `scripts/generate_paper_config.py`, `scripts/audit_method_config_diff.py` |
| P0.2 Fix the backend weight equation | done | `sem_geodf_risk.h` split into `computeFusedRisk` / `riskToWeight`; EN+VI equations and `backend_weight_flow` figure corrected; `test_sem_geodf_risk_properties.cpp` |
| P0.3 Fail-closed evaluation | done | `|| true` removed from the publication path, missing bag is fatal (exit 20), `expected_matrix.json`, `validate_experiment_matrix.py`, no silent ATE exclusion, assets gated on `validation.json` |
| P0.4 Restore failure detection | done | `estimator/failure_detection.h`, `FailureReason` enum, `failure_status.json`, `test_failure_detection.cpp` |
| P0.5 Complete the weighting ablation | **code done, runs pending** | `union_noweight` vs `union_weight` differ in `sem_geodf_backend_weight` alone, enforced by the audit. The paired numbers need the matrix re-run. |

## P1 — policy, frontend, semantics, backend

| Item | Status | Where |
|---|---|---|
| P1.1 Dice overlap + minimum support | done | `sem_policy.h` (5 metrics, selectable), `test_sem_geodf_overlap.cpp` |
| P1.2 Timestamp-based hold | done | `sem_policy::PolicyFsm`, `test_sem_geodf_policy_fsm.cpp` (hold duration invariant at 5/10/30 Hz) |
| P1.3 Health separated from action | done | `semanticHealth` / `geometricHealth` / `observabilityHealth`, `test_sem_geodf_track_lifecycle.cpp` |
| P1.4 Per-track factor lifecycle | done | `sem_policy::LifecycleManager` + per-frame deletion allowance |
| P1.5 Stereo physical validity contract | done | `stereo_validity.h`, `test_stereo_validity.cpp` (validated across 4 rig geometries) |
| P1.6 GeoDF reuses validated stereo | done | second LK removed; GeoDF reads `stereo_validity_by_id` |
| P1.7 Fundamental-matrix degeneracy | done | `geodf_degeneracy.h`, `test_geodf_degeneracy.cpp` |
| P1.8 No new timestamp on a reused mask | done | `mask_node.py` stores `last_mask_msg` and republishes it unchanged |
| P1.9 Real latest-only worker | done | `mask_node.py` worker thread + `latest_lock` |
| P1.10 Lock model and dependencies | done | `requirements-lock.txt`, `environment.yml`, `Dockerfile.paper`, `models/model_manifest.json`, hash verified at node startup |
| P1.11 Separate risk / target / applied weight | done | three quantities logged apart in `sem_geodf_stats.csv` |
| P1.12 Log survivors post-guard | done | `weighted_candidates_pre_guard`, `weighted_survivors_post_guard`, `rejected_weighted_tracks`, `min_survivor_weight` |
| P1.13 Marginalization consistency | done | `sqrt_weight` is `const` in all three factors; `test_marginalization_weight_freeze.cpp` |

## Tests and CI

| Item | Status |
|---|---|
| Unit tests (risk, overlap, policy FSM, lifecycle, stereo validity, failure detection, degeneracy) | done |
| Factor gradient tests at `w ∈ {1.0, 0.75, 0.25}` | done |
| Synthetic integration tests (all 8 plan scenarios) | done |
| `build-test.yml`, `protocol-audit.yml`, `paper-build.yml`, `synthetic-integration.yml` | done |

Two real bugs were found by tests written for this upgrade:

1. `ProjectionOneFrameTwoCamFactor` used the uncorrected `pts_i` instead of `pts_i_td`
   in its inverse-depth Jacobian, so that block was wrong whenever `estimate_td` was
   enabled. Paper configs run `estimate_td: 0`, so published results are unaffected.
2. The lifecycle's redundancy guard compared the track count against the floor once
   per track, so a batch of deletions in one frame could overshoot it (45 tracks, 30
   flagged → 15 left). Fixed with a per-frame deletion allowance.

---

## Blocked on compute or data — NOT done

These are the only plan items not implemented, and none is blocked on a design
question. Everything needed to execute them is committed.

### PR-08 — re-run the clean experiment matrix

**Why it is blocked:** it needs the VIODE and EuRoC bags, a CUDA GPU, and on the order
of hundreds of GPU-hours. `src/config/paper/expected_matrix.json` declares
2 datasets × (12 + 5) scenes × 5 methods × 10 trials = 850 runs.

**Every published number is currently invalid.** The existing tables were produced
before these fixes, under a configuration where:

- Sem-GeoDF enabled `visual_adaptive_quality` and `imu_adaptive_covariance` while the
  baselines did not, so no improvement was attributable to Sem-GeoDF;
- failure detection was inert, so diverged runs entered the averages;
- runs above 50 m ATE were dropped silently, removing the cases the method claims to
  improve, with no success rate reported;
- the agreement trigger used a directed maximum without minimum support, and the hold
  was a frame count.

Each changes behaviour, so the matrix must be re-run in full. `paper/sem_geodf/*/main.tex`
now says this explicitly in "Status of the numerical results", and the LaTeX macros
still resolve to the old values until regenerated.

**Procedure:**

```bash
# 1. cheap gate: N=1 across the whole matrix, then inspect
N=1 TRAIN_N=1 ./scripts/run_paper_n5_postfix_matrix.sh

# 2. gate the tree before believing anything
python3 scripts/validate_experiment_matrix.py \
  --root results/sem_geodf_ablation/paper_postfix --min-trials 1

# 3. finish to the declared trial count
N=10 SKIP_TRAIN=1 SKIP_BUILD=1 FORCE=0 ./scripts/run_paper_n5_postfix_matrix.sh

# 4. full gate, then and only then build assets
python3 scripts/validate_experiment_matrix.py \
  --root results/sem_geodf_ablation/paper_postfix
python3 scripts/make_sem_geodf_paper_assets.py \
  --root results/sem_geodf_ablation/paper_postfix --out paper/sem_geodf
```

Step 4 refuses to run if step 3's gate did not pass.

### PR-09 — artifact release and final paper numbers

Depends entirely on PR-08. The manuscript's method sections are already correct and
test-enforced; only the numeric results, the deltas, and the abstract's figures need
regenerating from the new artifacts.

### Real-world dynamic evaluation

The plan requires at least one real dynamic sequence. VIODE is simulated and EuRoC
Machine Hall is static, so neither supports a generalization claim — the manuscript
now says so rather than implying otherwise. No suitable recorded sequence is present
in this repository; this needs either a public real dynamic VIO dataset or a new
recording, which is a data-collection decision rather than an engineering task.

### Robustness injection runs

The injection scenarios (semantic delay, frame drop, mask dilation/erosion, low
parallax, blur, aggressive rotation, stereo mismatch, mover dominance, feature
scarcity) are all covered as synthetic *unit and integration* tests, which is what
makes them CI-runnable. Running them as full VIO sequences with measured ATE is part
of the PR-08 compute budget.

---

## Definition of Done (plan section 15)

| Condition | Status |
|---|---|
| Paper equation matches code | done (property test) |
| Main methods share one backbone | done (audit) |
| Backend weighting has a complete paired ablation | config done, **runs pending** |
| No publication `|| true` | done (hygiene audit) |
| No silent skip or ATE exclusion | done (hygiene audit + validator) |
| Failure detection works | done |
| Stereo physical contract has tests | done |
| FSM uses timestamps not frame counts | done |
| Overlap has minimum support | done |
| Full expected matrix PASS | **runs pending** |
| Every run has full provenance | done (writer + validator) |
| Raw artifacts published | **pending PR-08/09** |
| Gradient tests PASS | done |
| Synthetic integration tests PASS | done |
| ATE reported with RPE, failure rate, coverage | tables implemented, **numbers pending** |
| At least one real dynamic evaluation | **blocked on data** |
| Abstract claims do not exceed evidence | done for the claim text; the abstract's numeric deltas are regenerated by PR-08 |

**Not ready to submit.** Every correctness, fairness and protocol item is closed; the
remaining work is the compute to re-run the matrix and one real dynamic sequence.
