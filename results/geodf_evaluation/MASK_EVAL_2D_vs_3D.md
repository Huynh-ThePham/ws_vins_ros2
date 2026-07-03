# Feature-level mask eval — 2D-F vs stereo 3D (VIODE vehicle masks)

Ground truth: VIODE segmentation (moving vehicles). Prediction: GeoDF hard-reject.
One dump run per (env, level, method).

| env | level | method | precision | recall | lift | static-FPR | dyn base-rate |
|---|---|---|---:|---:|---:|---:|---:|
| city_day | 0_none | Old 2D-F | n/a | n/a | n/a | 0.00% | 0.00% |
| city_day | 0_none | New stereo 3D | n/a | n/a | n/a | 0.00% | 0.00% |
| city_day | 1_low | Old 2D-F | n/a | 0.00% | n/a | 0.00% | 0.10% |
| city_day | 1_low | New stereo 3D | n/a | 0.00% | n/a | 0.00% | 0.10% |
| city_day | 2_mid | Old 2D-F | n/a | 0.00% | n/a | 0.00% | 1.34% |
| city_day | 2_mid | New stereo 3D | n/a | 0.00% | n/a | 0.00% | 1.35% |
| city_day | 3_high | Old 2D-F | 31.82% | 0.24% | 7.43x | 0.02% | 4.28% |
| city_day | 3_high | New stereo 3D | 74.47% | 1.80% | 17.43x | 0.03% | 4.27% |
| city_night | 0_none | Old 2D-F | n/a | n/a | n/a | 0.00% | 0.00% |
| city_night | 0_none | New stereo 3D | n/a | n/a | n/a | 0.00% | 0.00% |
| city_night | 1_low | Old 2D-F | n/a | 0.00% | n/a | 0.00% | 1.49% |
| city_night | 1_low | New stereo 3D | n/a | 0.00% | n/a | 0.00% | 1.49% |
| city_night | 2_mid | Old 2D-F | n/a | 0.00% | n/a | 0.00% | 2.05% |
| city_night | 2_mid | New stereo 3D | n/a | 0.00% | n/a | 0.00% | 2.05% |
| city_night | 3_high | Old 2D-F | n/a | 0.00% | n/a | 0.00% | 4.98% |
| city_night | 3_high | New stereo 3D | n/a | 0.00% | n/a | 0.00% | 4.98% |
| parking_lot | 0_none | Old 2D-F | n/a | n/a | n/a | 0.00% | 0.00% |
| parking_lot | 0_none | New stereo 3D | n/a | n/a | n/a | 0.00% | 0.00% |
| parking_lot | 1_low | Old 2D-F | 0.00% | 0.00% | 0.00x | 0.02% | 0.88% |
| parking_lot | 1_low | New stereo 3D | 60.93% | 11.75% | 78.11x | 0.06% | 0.78% |
| parking_lot | 2_mid | Old 2D-F | 22.37% | 0.16% | 2.11x | 0.07% | 10.59% |
| parking_lot | 2_mid | New stereo 3D | 66.27% | 3.22% | 6.36x | 0.19% | 10.42% |
| parking_lot | 3_high | Old 2D-F | 25.40% | 0.11% | 1.78x | 0.06% | 14.28% |
| parking_lot | 3_high | New stereo 3D | 68.08% | 2.24% | 4.96x | 0.17% | 13.73% |

## Summary

- Cells with both methods: 3/12
- Stereo 3D precision ≥ 2D-F: 3/3

**Claim support:** rejections align with VIODE dynamic masks; compare lift/precision between 2D-F and stereo 3D gates on the same adaptive activation stack.
