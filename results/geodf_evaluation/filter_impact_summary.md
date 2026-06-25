# GeoDF Filter Impact Metrics (Reviewer Evidence)

Metrics prove the front-end filter **runs**, **scores tracks**, and **removes features** without relying on ATE alone. `dual_gate_reduction` = % of high-Sampson tracks blocked by the RANSAC-outlier gate (not entering the candidate set).

Sampson threshold τ = **3.0** (pseudo-pixel space, f=460).

## 2. VIODE real dynamic (filter activity vs dynamic level)

| Run | Frames | Frames w/ reject | Mean reject ratio | Total rejected | Total candidates | Cand/scored | Dual-gate reduction | Guard triggered | Mean max Sampson | GeoDF ms | Reject when active |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| VIODE 0_none | 1327 | 66.01% | 2.08% | 3089 | 3089 | 2.17% | 96.31% | 0.00% | 515.92 | 0.38 | 3.5 |
| VIODE 1_low | 1327 | 66.24% | 2.12% | 3130 | 3130 | 2.20% | 96.19% | 0.00% | 582.65 | 0.39 | 3.6 |
| VIODE 2_mid | 1327 | 69.25% | 2.35% | 3443 | 3443 | 2.46% | 95.73% | 0.00% | 560.37 | 0.38 | 3.7 |
| VIODE 3_high | 1322 | 70.73% | 2.93% | 4005 | 4009 | 2.98% | 94.63% | 0.00% | 621.82 | 0.39 | 4.3 |

## 4. Metric definitions (paper Table / appendix)

| Metric | Definition |
| --- | --- |
| Frames w/ reject | % frames where `rejected ≥ 1` |
| Mean reject ratio | mean(`rejected / tracks_before`) per frame |
| Total rejected | Σ rejected features over run |
| Total candidates | Σ features passing RANSAC-outlier ∧ Sampson>τ |
| Cand/scored | total_candidates / total_scored tracks |
| Dual-gate reduction | 1 − candidates / sampson_above_th (RANSAC gate effect) |
| Guard triggered | % frames where \|C\| > floor(ρ·N) and cap applied |
| Mean max Sampson | mean of per-frame max Sampson over scored tracks |
| GeoDF ms | mean GeoDF module latency per frame (when logged) |
