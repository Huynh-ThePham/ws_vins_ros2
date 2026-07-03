# AECE Submission Readiness Checklist

Target journal: **Advances in Electrical and Computer Engineering (AECE)**.

For a stronger WoS Q3 submission, use the extended ablation protocol now
available in this branch:

```bash
METHODS="baseline alwayson adaptive_fixed adaptive_no_quality adaptive_no_vote adaptive"
METHODS="$METHODS" bash scripts/run_euroc_n3_prepare.sh 3
METHODS="$METHODS" bash scripts/run_euroc_n3.sh 3
METHODS="$METHODS" bash scripts/run_viode_n5_prepare.sh 5
METHODS="$METHODS" bash scripts/run_viode_n5.sh 5
```

The additional baselines isolate always-on hard rejection, fixed activation
thresholding, quality-aware activation, and temporal voting.

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

## Manuscript Status

- **Submission-ready draft:** `docs/MANUSCRIPT_GeoDF-VINS-AECE.md` (updated with N=5/N=3 results)
- **Word export:** `docs/MANUSCRIPT_GeoDF-VINS-AECE.docx` — copy into official AECE `.doc` template
- **Artifact index:** `results/geodf_evaluation/PAPER_TABLES_AECE.md`

## Remaining Work Before Submission

Rebuild AECE submission artifacts from branch `paper/geodf-adaptive-vins-2026`
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
  for stereo-inertial visual odometry (stereo 3D motion consistency + scene-aware
  activation).
- VIODE N=5: improved ATE in **3/12** conditions under +/-3% band; strongest
  gain **city_day/3_high +14.8%**; **6/12 neutral**, **3/12 losses** (parking_lot).
- Stereo 3D gate beats 2D-F on `city_day/3_high` by **+9.5%** (same activation).
- EuRoC: static-scene safety preserved (N=3 repeats; see `EUROC_REPEAT_N3.md`).
- Detection: **17.43x** lift on `city_day/3_high`, static FPR **0.03%**.
- Runtime: mean **1.76 ms** per frame (`geo_ms`), **3.5%** of 20 Hz budget.
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
- Add the Q3 ablation table generated by the extended method list:
  `alwayson`, `adaptive_fixed`, `adaptive_no_quality`,
  `adaptive_no_vote`, and `adaptive`.
- Add 2-3 more cited related-work methods (motion segmentation, IMU-aided
  epipolar, optical-flow consistency).

## Recommended Submission Strategy

Submit to AECE first and prepare the manuscript specifically for AECE's
electrical and computer engineering audience. The writing should emphasize
algorithm integration, image-processing geometry, sensor fusion, reproducible
benchmarks, and the engineering limitation analysis rather than broad
state-of-the-art claims.
