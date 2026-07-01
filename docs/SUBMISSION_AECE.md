# AECE Submission Readiness Checklist

Target journal: **Advances in Electrical and Computer Engineering (AECE)**.

## Verdict

GeoDF-Adaptive is suitable for AECE if it is written as an applied electrical
and computer engineering contribution: a reproducible geometry-based algorithm
integrated into a stereo-inertial odometry pipeline and validated on public
datasets.

AECE is the primary target for the current result set because the paper has
strong engineering validation, public-dataset evaluation, and a practical
algorithm-integration contribution.

## Journal Requirements To Satisfy

- Use the official AECE Microsoft Word `.doc` template.
- Submit the final camera-ready Word file through the AECE online system.
- Page count must be even: 8, 10, or 12 pages.
- Target 8 pages to keep APC at the base level.
- APC is currently listed as 300 EUR for accepted papers, with 25 EUR per page
  beyond 8 pages.
- Include exactly five keywords or phrases, alphabetically ordered.
- Include permanent links for references whenever possible, preferably DOI or
  CrossRef links.
- Prepare and upload the AECE copyright transfer / author's guarantee form.
- Check originality/plagiarism before submission.
- Do not submit the work elsewhere simultaneously.

## Current Paper Assets

- Main manuscript draft: `docs/MANUSCRIPT_GeoDF-VINS-AECE.md`
- Word export (copy into AECE template): `docs/MANUSCRIPT_GeoDF-VINS-AECE.docx`
  (rebuild with `scripts/build_manuscript_docx.sh`)
- Main trajectory table: `results/geodf_evaluation/PAPER_RESULTS_N5.md`
- Main artifact index: `results/geodf_evaluation/PAPER_TABLES_AECE.md`
- Detection evaluation: `results/geodf_evaluation/DETECTION_EVAL_VIODE.md`
- Figures (grayscale-safe, vector + 300 dpi PNG):
  - Fig. 1 pipeline: `results/geodf_evaluation/figures/pipeline_geodf_adaptive.{svg,pdf,png}`
    (`scripts/make_pipeline_figure.py`)
  - Fig. 2 ATE delta: `results/geodf_evaluation/figures/viode_ate_delta_n5_gray.{svg,pdf,png}`
  - Fig. 3 detection lift: `results/geodf_evaluation/figures/viode_detection_lift_gray.{svg,pdf,png}`
    (Figs. 2-3 from `scripts/make_result_figures.py`; old red/green SVGs kept for reference)

## GeoDF-Adaptive Frozen Build Protocol

Rebuild AECE submission artifacts from branch `paper/geodf-adaptive-vins-2026-q4`
(worktree `../ws_vins_ros2_paper1_adaptive`). Reference freeze commit:

```text
c64674097ab230465bf73576c60d3728050d3ec2
Freeze AECE paper #1: figures, runtime, references, and Word export
```

Use the worktree helper before any adaptive-branch rebuild:

```bash
# Safe check: sync ../ws_vins_ros2_paper1_adaptive to the adaptive branch tip
bash scripts/paper1_adaptive_worktree.sh

# Rebuild adaptive binaries
bash scripts/paper1_adaptive_worktree.sh --build

# Optional full rerun (long)
FORCE=1 bash scripts/paper1_adaptive_worktree.sh --build --benchmark 5

# Rebuild the Word export from adaptive manuscript/assets
bash scripts/paper1_adaptive_worktree.sh --docx
```

Each manuscript branch owns its configs and evaluation artifacts. Regenerate
AECE tables from the adaptive branch when citing GeoDF-Adaptive results.

## Claims To Use

- GeoDF-Adaptive is a geometry-only front-end dynamic feature rejection module
  for stereo-inertial visual odometry.
- The method improves VIODE ATE in 7 of 12 evaluated conditions under a +/-3%
  band.
- Strong gains appear in `city_day/3_high` (+24.5%) and `city_night/0_none`
  (+41.3%).
- EuRoC static safety is preserved, with all five tested sequences improving by
  +2.0% to +6.2%.
- Rejected features align with VIODE moving-vehicle masks: `city_day` lift is
  8.33x to 31.72x with static FPR below 1%.
- High dynamic-density `parking_lot` scenes are a limitation, not a solved case.

## Claims To Avoid

- Do not claim universal improvement over baseline.
- Do not claim state-of-the-art visual-inertial odometry.
- Do not use hybrid/soft/YOLO experiments as main paper artifacts.

## Measured Runtime (now available)

- GeoDF-Adaptive per-frame cost: mean 0.28 ms, median 0.24 ms, p95 0.53 ms,
  p99 0.83 ms over 74,994 logged frames (60 VIODE adaptive trials).
- ~0.56% of the 50 ms (20 Hz) per-frame budget; rejection armed on 10.3% of
  frames; CPU-only (Intel Xeon W-11955M, 8C/16T).
- The "low-overhead" claim is now data-backed (manuscript Section 5.4, Table 5).

## Remaining Work Before Submission

Done:
- [x] Add final references with DOI/permanent links (manuscript References).
- [x] Replace the Related Work placeholder table with cited references.
- [x] Measure runtime/FPS (replaces the "remove overhead wording" option).

Done:
- [x] Method pipeline figure drawn (Fig. 1), grayscale-safe.
- [x] Result figures regenerated grayscale-safe (Figs. 2-3).
- [x] `.docx` generated with embedded figures + tables
  (`docs/MANUSCRIPT_GeoDF-VINS-AECE.docx`).

Remaining:
1. Paste content from `docs/MANUSCRIPT_GeoDF-VINS-AECE.docx` into the official
   AECE `.doc` template (apply AECE styles, two-column).
2. Prepare the copyright transfer / author's guarantee form.
3. Run an originality/plagiarism check.

## Length Estimate vs 8-Page Target

Measured paper-body content: ~2,530 prose words + 6 tables + 3 figures +
references (~340 words). In the AECE two-column layout this is roughly 6-7
pages, i.e. on or just under the 8-page base target -- so the risk is
under-filling, not overflowing; no trimming is required.

If more length is needed to reach a full 8 pages, expand (do not pad):
- Add an RPE summary table (data already collected for the N=5 runs).
- Add a fixed-rho vs auto-rho ablation paragraph + small table
  (`viode_stereo_imu_geodf_adaptive_fixed_config.yaml` exists for this).
- Add 2-3 more cited related-work methods (motion segmentation, IMU-aided
  epipolar, optical-flow consistency).

## Recommended Submission Strategy

Submit to AECE first and prepare the manuscript specifically for AECE's
electrical and computer engineering audience. The writing should emphasize
algorithm integration, image-processing geometry, sensor fusion, reproducible
benchmarks, and the engineering limitation analysis rather than broad
state-of-the-art claims.
