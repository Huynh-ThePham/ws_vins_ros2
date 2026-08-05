# IEEE Access submission — Sem-GeoDF

**Primary venue:** IEEE Access (RA-L deprecated for this package).

## Package

| Item | Path |
|------|------|
| English manuscript (**submit**) | `paper/sem_geodf/en/main.tex` — `\documentclass{ieeeaccess}` |
| Official template assets | `paper/sem_geodf/template/` (`ieeeaccess.cls`, logos) |
| Vietnamese parallel | `paper/sem_geodf/vi/main.tex` |
| Figures / tables | `paper/sem_geodf/figures/`, `paper/sem_geodf/tables/` |
| Fit rationale | `docs/CONTRIBUTION_FIT.md` |
| Venue plan | `docs/JOURNAL_PLAN.md` |

Re-download the latest zip from the [IEEE Template Selector](https://template-selector.ieee.org/) before camera-ready if IEEE updates the class.

## Claim framing (must keep)

- **Do claim:** spectrum robustness; gains vs GeoDF-Adaptive on high-dynamic hold-outs; reduced static regression vs always-on semantics; label-free policy; fair \(1.0\times\) protocol.
- **Do not claim:** universal per-cell ATE win over SAD-Sem; frontier novelty over full dynamic-VIO stacks; matched DynaVINS SOTA without identical bags/rates.

## Build

```bash
python3 scripts/make_sem_geodf_paper_assets.py \
  --root results/sem_geodf_ablation/paper_postfix --out paper/sem_geodf
cd paper/sem_geodf/en && tectonic -X compile main.tex
cd ../vi && tectonic -X compile main.tex
```

Refresh macros/tables after N=3 finishes (`PAPER_N3_SUMMARY`).
