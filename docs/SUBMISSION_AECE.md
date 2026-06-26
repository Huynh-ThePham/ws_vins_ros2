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
- Main trajectory table: `results/geodf_evaluation/PAPER_RESULTS_N5.md`
- Main artifact index: `results/geodf_evaluation/PAPER_TABLES_AECE.md`
- Detection evaluation: `results/geodf_evaluation/DETECTION_EVAL_VIODE.md`
- Figures:
  - `results/geodf_evaluation/figures/viode_ate_delta_n5.svg`
  - `results/geodf_evaluation/figures/viode_detection_lift.svg`

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
- Do not claim measured low overhead until FPS/runtime is measured.
- Do not use hybrid/soft/YOLO experiments as main paper artifacts.

## Remaining Work Before Submission

1. Convert `docs/MANUSCRIPT_GeoDF-VINS-AECE.md` into the official AECE `.doc`
   template.
2. Add final references with DOI/permanent links.
3. Replace the Related Work placeholder table with cited references.
4. Draw the method pipeline figure.
5. Measure runtime/FPS or remove all measured-overhead wording.
6. Check that the final page count is 8, 10, or 12 pages.
7. Prepare the copyright transfer / author's guarantee form.
8. Run an originality/plagiarism check.

## Recommended Submission Strategy

Submit to AECE first and prepare the manuscript specifically for AECE's
electrical and computer engineering audience. The writing should emphasize
algorithm integration, image-processing geometry, sensor fusion, reproducible
benchmarks, and the engineering limitation analysis rather than broad
state-of-the-art claims.
