# Repeatability study — ATE-RMSE mean±std (env=city_day)

Each cell = mean±std over N trials of the SAME config and SAME build. ATE is near-
deterministic within a build (std~0); compare configs only within one build.
Δ = improvement of mean vs baseline mean (positive = better).

| level | baseline | adaptive | adaptiveB | adaptive_v2 | Δ adaptive vs base | Δ adaptiveB vs base | Δ adaptive_v2 vs base |
|---|---|---|---|---|---|---|---|
| 2_mid | 0.166±0.000 (n=5) | 0.152±0.009 (n=5) | 0.167±0.000 (n=5) | 0.147±0.000 (n=5) | +8.1% | -0.9% | +11.1% |
| 3_high | 0.369±0.052 (n=5) | 0.224±0.001 (n=5) | 0.282±0.054 (n=5) | 0.323±0.000 (n=5) | +39.3% | +23.7% | +12.4% |
