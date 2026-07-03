# Repeatability study — ATE-RMSE mean±std (env=parking_lot)

Each cell = mean±std over N trials of the SAME config and SAME build. ATE is near-
deterministic within a build (std~0); compare configs only within one build.
Δ = improvement of mean vs baseline mean (positive = better).

| level | baseline | adaptive_2d | adaptive | Δ adaptive_2d vs base | Δ adaptive vs base |
|---|---|---|---|---|---|
| 0_none | 0.167±0.000 (n=5) | 0.167±0.000 (n=5) | 0.167±0.000 (n=5) | +0.0% | -0.1% |
| 1_low | 0.118±0.000 (n=5) | 0.101±0.000 (n=5) | 0.125±0.000 (n=5) | +14.2% | -6.1% |
| 2_mid | 0.144±0.000 (n=5) | 0.219±0.007 (n=5) | 0.152±0.000 (n=5) | -51.8% | -5.6% |
| 3_high | 0.119±0.000 (n=5) | 0.140±0.003 (n=5) | 0.148±0.000 (n=5) | -17.3% | -23.8% |
