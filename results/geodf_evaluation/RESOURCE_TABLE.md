# End-to-end computational cost (whole VIO process)

Process-level resource use sampled at 2 Hz over every trial (all threads). Complements the in-pipeline GeoDF module time (Table 5): shows the proposed filter does not materially change memory/CPU vs baseline.

| Dataset | Method | Trials | Peak RSS (MB) | Mean CPU (%) | Peak CPU (%) | Peak threads |
|---|---|---:|---:|---:|---:|---:|
| viode | baseline | 48 | 125±3 | 117±5 | 153±9 | 35 |
| viode | adaptive | 48 | 125±2 | 140±21 | 186±13 | 35 |
| euroc | baseline | 15 | 117±2 | 124±5 | 160±5 | 35 |
| euroc | adaptive | 14 | 117±2 | 158±5 | 206±8 | 35 |
