# GeoDF-VINS AECE Paper Tables and Figures

This file is the AECE-facing artifact index. Use it when converting the
manuscript draft into the official AECE Microsoft Word `.doc` template.

## Target Journal

**Advances in Electrical and Computer Engineering (AECE)**

- Publisher: Stefan cel Mare University of Suceava, Romania.
- Indexing: Scopus, SCIE, DOAJ, INSPEC.
- Recent metrics from AECE author page: JCR IF 0.700, Scopus CiteScore 2.0.
- APC: 300 EUR for accepted papers, plus extra-page charges beyond 8 pages.
- Format: `.doc` template; manuscript pages must be even: 8, 10, or 12.

## Main Claim

GeoDF-Adaptive is a geometry-only front-end dynamic feature rejection module for
stereo-inertial visual odometry. It improves trajectory accuracy in several
moderate-dynamic VIODE scenes, preserves static-scene safety on EuRoC, and
provides feature-level validation against VIODE dynamic segmentation masks.

For AECE, keep the claim practical and engineering-oriented. Do not claim
universal dynamic-scene VIO improvement.

## Reproducibility Freeze

Paper #1 artifacts are tied to the frozen GeoDF-Adaptive commit:

```text
c64674097ab230465bf73576c60d3728050d3ec2
Freeze AECE paper #1: figures, runtime, references, and Word export
```

Use `bash scripts/paper1_freeze_worktree.sh --build` before rebuilding Paper #1
binaries or Word artifacts. Do not regenerate Paper #1 submission results from
the Paper #2 hybrid branch, because it modifies the shared GeoDF front-end path.

## Table A: VIODE N=5 Trajectory Summary

Source: `results/geodf_evaluation/PAPER_RESULTS_N5.md`

- Trials: N=5 for VIODE baseline/adaptive.
- Methods: baseline VINS-Fusion vs PROPOSED GeoDF-Adaptive.
- Metrics: ATE RMSE and RPE RMSE in metres.
- Verdict: 7 wins, 5 losses, 0 neutral under a +/-3% band.
- Strong positive cases: `city_day/3_high` +24.5%, `city_night/0_none` +41.3%.
- Limitation cases: `parking_lot/2_mid` -36.4%, `parking_lot/3_high` -44.3%.

## Table B: EuRoC Static Safety

Source: `results/geodf_evaluation/PAPER_RESULTS_N5.md`

- Sequences: MH_01_easy through MH_05_difficult.
- PROPOSED improves all five static sequences by +2.0% to +6.2%.
- Use this table to support the claim that scene-aware activation does not
  degrade static stereo-inertial odometry in the tested EuRoC sequences.

## Table C: Detection Against VIODE GT Masks

Source: `results/geodf_evaluation/DETECTION_EVAL_VIODE.md`

- Ground truth: VIODE segmentation IDs `vehicle_dynamic_0..10`.
- Timestamp match: nearest mask within 30 ms, 100% matched.
- `city_day` lift: 31.72x, 12.14x, 8.33x for low/mid/high dynamic levels.
- Static false-positive rate on `city_day`: 0.59% to 0.71%.
- `parking_lot` lift drops to 1.42x to 1.48x, explaining the trajectory failure
  under high dynamic density.

## Figure Assets

- `figures/viode_ate_delta_n5.svg`: VIODE ATE improvement bar chart.
- `figures/viode_detection_lift.svg`: VIODE dynamic-feature detection lift chart.

AECE publishes in print and online. Keep figures readable in grayscale and avoid
using colour as the only distinction between methods.

## Recommended 8-Page Allocation

| Section | Target length |
|---|---:|
| Abstract + keywords | 0.4 page |
| Introduction | 1.0 page |
| Related work | 1.0 page |
| Method | 2.0 pages |
| Experiments | 1.0 page |
| Results and discussion | 2.0 pages |
| Conclusion + references | 0.6 page |

If the paper exceeds 8 pages, use 10 pages rather than compressing critical
method and result explanations too aggressively.

## Do Not Use As Main Paper Artifacts

The hybrid, soft-weight, and YOLO experiments are exploratory and are excluded
from this AECE artifact set. They should only appear as future work if discussed
at all.

## Final Submission Checks

- Rebuild/check from the frozen Paper #1 worktree before final export.
- Paste/export into the official AECE `.doc` template.
- Copyright transfer form.
