# Monthly v5 Measurement Notes

Date: 2026-05-25

Measurement rules used by the v5 monthly deck.

## Rules

- Headline performance numbers use median clean `compute_ms` after warmup.
- Average remains secondary context; median sets headline speedup.
- Final confirmation reports median, P10, P90, and min from clean timestamp runs.
- Counter-enabled runs are structural evidence only because shader counters add atomic traffic.
- RGP occupancy supplies auxiliary context. Treat `wavefront = 0` as context only; clean timestamps carry dispatch-duration proof.
- Warmup policy is source-specific for historical runs and is recorded in the CSV instead of being hidden.
- Request batching is compared only against the suite-local `q2_batch_off` row.

## Normalized Timeline

| stage | config | warmup_frames_dropped | sample_count | median_compute_ms | avg_compute_ms |
| --- | --- | --- | --- | --- | --- |
| Baseline | Q1 batch on, Q2 scalar, C=24/B=72 | 1 | 9 | 30.7499 | 30.3533 |
| B/C ratio | C=40/B=56 | 1 | 9 | 27.8938 | 27.9742 |
| Q1 sharding | Q1s16/Q2s1, C=38/B=58 | 1 | 13 | 13.7416 | 13.7046 |
| Q2 sharding | Q1s16/Q2s16, C=16/B=80 | 2 | 14 | 5.5708 | 5.5736 |
| Q1 lane-pop | Q1s16/Q2s16, C=24/B=72 | 30 | 278 | 3.8509 | 3.8526 |
| High shard scaling | Q1s256/Q2s256, C=24/B=72 | 30 | 323 | 0.9868 | 1.0050 |

## Final Confirmation

| stage | sample_count | median_compute_ms | p10_compute_ms | p90_compute_ms | min_compute_ms |
| --- | --- | --- | --- | --- | --- |
| Final repeat 1 | 1836 | 0.9904 | 0.9804 | 1.0036 | 0.9659 |
| Final repeat 2 | 1867 | 0.9916 | 0.9819 | 1.0053 | 0.9657 |
| Final repeat 3 | 1868 | 0.9907 | 0.9810 | 1.0042 | 0.9593 |
| Final repeat 4 | 1866 | 0.9903 | 0.9807 | 1.0038 | 0.9615 |

## Request Batching Validation

| stage | config | sample_count | median_compute_ms | p90_compute_ms | correctness_ok |
| --- | --- | --- | --- | --- | --- |
| main_sharded/default_256_shards | q2_batch_l1 | 0 |  |  | 0 |
| main_sharded/default_256_shards | q2_batch_l16 | 354 | 11.4284 | 11.4658 | 1 |
| main_sharded/default_256_shards | q2_batch_l2 | 0 |  |  | 0 |
| main_sharded/default_256_shards | q2_batch_l32 | 354 | 11.2778 | 11.3034 | 1 |
| main_sharded/default_256_shards | q2_batch_l4 | 0 |  |  | 0 |
| main_sharded/default_256_shards | q2_batch_l8 | 354 | 11.7109 | 11.7553 | 1 |
| main_sharded/default_256_shards | q2_batch_off | 354 | 1.9432 | 1.9691 | 1 |
| main_sharded/stress_16_shards | q2_batch_l1 | 354 | 7.3685 | 7.4978 | 1 |
| main_sharded/stress_16_shards | q2_batch_l16 | 354 | 4.2623 | 5.3898 | 1 |
| main_sharded/stress_16_shards | q2_batch_l2 | 354 | 5.1275 | 5.2766 | 1 |
| main_sharded/stress_16_shards | q2_batch_l32 | 354 | 4.1165 | 5.2635 | 1 |
| main_sharded/stress_16_shards | q2_batch_l4 | 354 | 4.2553 | 4.6734 | 1 |
| main_sharded/stress_16_shards | q2_batch_l8 | 354 | 4.1737 | 5.1022 | 1 |
| main_sharded/stress_16_shards | q2_batch_off | 354 | 4.2442 | 4.9059 | 1 |
| old_repro/old_unsharded | q1_on_q2_batch_l1 | 354 | 41.4104 | 42.5200 | 1 |
| old_repro/old_unsharded | q1_on_q2_batch_l16 | 354 | 34.0159 | 34.9301 | 1 |
| old_repro/old_unsharded | q1_on_q2_batch_l2 | 354 | 39.0516 | 40.0489 | 1 |
| old_repro/old_unsharded | q1_on_q2_batch_l32 | 354 | 33.7080 | 34.6361 | 1 |
| old_repro/old_unsharded | q1_on_q2_batch_l4 | 354 | 36.2794 | 37.2180 | 1 |
| old_repro/old_unsharded | q1_on_q2_batch_l8 | 354 | 34.7559 | 35.6508 | 1 |
| old_repro/old_unsharded | q1_on_q2_off | 354 | 30.9477 | 31.6694 | 1 |
| old_repro/old_unsharded | scalar_q1_off_q2_off | 354 | 39.2272 | 40.0777 | 1 |

## Source Of Truth

Numeric source: `reports/2026-05/metrics/monthly_v5_normalized_measurements_20260525.csv` plus measurement notes. The CSV contains the source file and warmup rule for each row.
