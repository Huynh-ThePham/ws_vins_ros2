# Async ROS 2 Realtime Benchmark (SAD-VINS / SGTA-VINS)

Modular pipeline: `mask_node` (YOLOv11n-seg) publishes `/dynamic_mask`; `pht_vio_node` consumes mask with `sem_mask_max_age_ms: 150` and runs GeoDF/SGTA front-end (~1 ms).

## Run

```bash
export YOLO_DEVICE=cuda
./scripts/run_realtime_benchmark.sh
python3 scripts/summarize_realtime_benchmark.py --root results/realtime_benchmark
```

Env overrides: `SAD_BAG_RATE`, `YOLO_PROCESS_EVERY_N`, `ROOT`, `VIODE_BAG`, `EUROC_BAG`.

## VIODE `city_day/3_high` (20 Hz cam, 66.4 s, ~10 Hz VIO output)

| Config | Bag rate | YOLO n | Pose cov. | Pose Hz | YOLO ms | Mask lag p95 | Geo ms | ATE (m) | Realtime |
|--------|----------|--------|-----------|---------|---------|--------------|--------|---------|----------|
| baseline | 1.0 | — | 98.6% | 9.9 | — | — | — | 0.346 | yes |
| sad_sem | 1.0 | 1 | 98.6% | 9.9 | 24 | 100 ms | — | 0.180 | yes |
| sgta | 0.5 | 1 | 98.6% | 9.9 | 25 | 100 ms | 1.03 | 0.170 | yes |
| **sgta** | **1.0** | **1** | **98.6%** | **9.9** | **27** | **100 ms** | **1.21** | **0.176** | **yes** |
| sgta | 1.0 | 2 | 98.6% | 9.9 | 72 | 150 ms | 0.50 | 0.198 | yes* |

\* `process_every_n=2`: mask trust drops (~17% fresh YOLO on VIODE); optical-flow propagate hits 150 ms lag budget — not recommended.

**Key finding:** SGTA at **1.0× bag** matches 0.5× accuracy (ATE 0.176 vs 0.170 m) with full pose throughput (~10 Hz). Prior eval default of 0.5× was conservative, not a hard realtime limit.

## EuRoC `MH_01_easy` @ 40 s offset (1.0× bag)

| Config | Pose cov. | YOLO ms | Mask lag p95 | Geo ms | Realtime |
|--------|-----------|---------|--------------|--------|----------|
| baseline | 99.3% | — | — | — | yes |
| sgta n=1 | 99.3% | 19 | 100 ms | 1.06 | yes |
| sgta n=2 | 99.3% | 66 | 150 ms | 0.48 | yes (lag at limit) |

Static scene: semantic+SGTA keeps pose rate and mask latency within budget at full bag rate.

## Paper claim (supported)

> **ROS 2 modular semantic VIO** with decoupled YOLO inference: on GPU, YOLOv11n-seg (~15–27 ms) runs asynchronously while VINS+SGTA geometry stays ~1 ms/frame; mask–image lag p95 ≈ 100 ms (< 150 ms gate), maintaining ~10 Hz odometry at **1.0× sensor rate** without the 0.5× playback used in earlier accuracy evals.

## Metrics glossary

| Metric | Meaning |
|--------|---------|
| `pose_cov%` | `vio_poses / (effective_bag_duration × 10 Hz)` |
| `trk/cam%` | tracker frames / cam messages (informational; VINS subsamples) |
| `mask_lag_p95` | sim-time lag image→mask (ms), from `sem_stats.csv` |
| `yolo_ms` | per-frame inference from `yolo_mask_node.log` |
| `rt_ratio` | expected_play_time / wall_time (includes YOLO warmup in harness) |

Results JSON: `results/realtime_benchmark/realtime_summary.json`
