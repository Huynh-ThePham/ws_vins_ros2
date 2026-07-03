# AECE Paper Artifact Index

Generated from N=5 VIODE (`stereo3d_claim`) + EuRoC N=3 + detection dumps.

## Main tables (manuscript)

| Table | Content | Source file |
|---|---|---|
| Table 1 | Related-work positioning | `docs/MANUSCRIPT_GeoDF-VINS-AECE.md` |
| Table 2 | VIODE ATE/RPE N=5 (baseline vs PROPOSED) | `PAPER_RESULTS_N5.md` |
| Table 2b | Stereo 3D vs 2D-F ablation | `STEREO3D_CLAIM_N5.md` |
| Table 3 | EuRoC static safety N=3 | `EUROC_REPEAT_N3.md` |
| Table 4 | Feature-level detection | `DETECTION_EVAL_VIODE.md` |
| Table 5 | Runtime / overhead | `RUNTIME_TABLE.md` |

## Figures

| Fig | File |
|---|---|
| Fig. 1 Pipeline | `figures/pipeline_geodf_adaptive.png` |
| Fig. 2 ATE delta | `figures/viode_ate_delta_n5_gray.png` |
| Fig. 3 Detection lift | `figures/viode_detection_lift_gray.png` |

## JSON / stats

- `paper_results_n5.json` — VIODE aggregates
- `stereo3d_claim_n5.json` — 3-way ablation
- `stats_tests.json` — significance / effect size
- `runtime_summary.json` — Table 5 source
- `MASK_EVAL_2D_vs_3D.json` — mask eval detail

## Claims (N=5, use in paper)

- VIODE ATE: **3 wins / 3 losses / 6 neutral** (±3% band), 12/12 evaluated
- Best cell: **city_day/3_high +14.8%**
- Stereo 3D vs 2D-F: **+9.5%** on `city_day/3_high`
- Detection: **17.43× lift** on `city_day/3_high`, static FPR **0.03%**
- Limitation: **parking_lot** up to **−23.8%** on `3_high`
- Runtime: **1.76 ms** mean (`geo_ms`), **2.4%** frames armed

## Regenerate

```bash
bash scripts/postprocess_paper_artifacts.sh
python3 scripts/make_pipeline_figure.py
bash scripts/build_manuscript_docx.sh
```
