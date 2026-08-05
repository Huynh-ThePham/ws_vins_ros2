# Sem-GeoDF manuscript — IEEE Access (official template)

**Submit:** English `en/main.tex` built with `ieeeaccess.cls`.  
**Parallel:** Vietnamese `vi/main.tex` (same class; Unicode fonts for accents).

## Template assets

Vendored under `template/` (see `template/SOURCE.md`):

| File | Role |
|------|------|
| `ieeeaccess.cls` | Official Access class (local patches for Tectonic/XeTeX) |
| `IEEEtran.cls` | Bundled dependency |
| `Logo.png`, `notaglineLogo.png` | Running header |
| `bullet.png` | `\EOD` end mark |

Symlinked into `en/` and `vi/` for compile.

**Upstream:** Prefer re-download from [IEEE Template Selector](https://template-selector.ieee.org/) → IEEE Access → LaTeX before camera-ready. Current mirror: community IEEE Access package (`ieeeaccess.cls` + logos).

## Local patches (in `template/ieeeaccess.cls`)

Needed so Tectonic/XeTeX builds succeed:

- CMYK `accessblue` instead of Pantone `spotcolor`
- Caption width uses `\wd\@tempboxa` (fixes undefined `\xfigwd`)
- Biography TOC flag / counter fixes
- Access page stock `203.2mm × 276.2mm` via `\special{papersize=...}`
- Simpler `\hbox` running headers (avoids 0-width head box under XeTeX)

Camera-ready with official pdfLaTeX + Formata fonts may restore Pantone spot color if desired.

## Build

```bash
# Method diagrams (no ablation tree needed)
python3 scripts/make_sem_geodf_method_figures.py

# Tables + result plots (needs paper_postfix tree)
python3 scripts/make_sem_geodf_paper_assets.py \
  --root results/sem_geodf_ablation/paper_postfix --out paper/sem_geodf

cd paper/sem_geodf/en && tectonic -X compile main.tex
cd ../vi && tectonic -X compile main.tex
```

## Figures

| File | Role |
|------|------|
| `system_overview_sem_geodf` | Full-width architecture (Fig. overview) |
| `pipeline_sem_geodf` | Compact per-frame front-end |
| `fusion_compare_sem_geodf` | AND / Sequential / OR contrast |
| `overlap_bidirectional` | Bidirectional overlap on raw GeoDF |
| `policy_fsm_sem_geodf` | Three-state online policy |
| `backend_weight_flow` | Residual-weight path into Ceres |
| `eval_protocol_sem_geodf` | Train → freeze → hold-out hygiene |
| `viode_ate_delta_sem_geodf` | ATE Δ vs GeoDF-Adaptive |
| `method_compare_selected` | Selected static/dynamic bar chart |

Fill real `\author`, `\address`, `\corresp`, and `IEEEbiographynophoto` blocks before submission (Access requires biographies for all authors).

## Manuscript status (2026-07)

| Section | Status |
|---------|--------|
| Abstract / Intro / Related / Method / Setup / Results / Discussion / Conclusion | Complete |
| Tables: VIODE ATE, EuRoC, Δ vs Adaptive, static safety, weight params | Complete |
| Method + result figures (9 stems) | Complete |
| Paired \(w_i\) ATE ablation (`sem_geodf_noweight`) | Pending camera-ready |
| Author names / bios / DOI | Placeholders |
