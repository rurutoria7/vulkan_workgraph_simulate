# Request batching validation findings

Date: 2026-05-25

Branch: `experiment-request-batching-validation`

## Observation

- Raw data: 132 raw CSV files, `run_manifest.csv`, and `request_batching_validation_summary.csv`.
- Method: 3 repeats per case, 150 benchmark frames per repeat, first 30 recorded frames dropped in summary.
- Timing runs used `--wg-timestamps-only`; counter runs used `--wg-metrics`.
- Correctness rule: `edges=196608`, `vertices=393216`, `stop_writes=1` on counter runs. Failed process runs are not counted as successful optimizations.
- Important caveat: the 256-shard off baseline in this run measured median `1.943 ms`, not the historical `~0.99 ms`. Treat this report as same-binary batching on/off validation, not as an update to the absolute main-path best number.

### Old unsharded reproduction

Baseline for Q2 comparison is old `q1_on_q2_off`.

| case | median ms | p90 ms | median delta vs Q2 off | correctness |
|---|---:|---:|---:|---|
| scalar q1 off q2 off | 39.227 | 40.078 | +26.8% | ok |
| q1 on q2 off | 30.948 | 31.669 | 0.0% | ok |
| q1 on q2 batch limit 1 | 41.410 | 42.520 | +33.8% | ok |
| q1 on q2 batch limit 2 | 39.052 | 40.049 | +26.2% | ok |
| q1 on q2 batch limit 4 | 36.279 | 37.218 | +17.2% | ok |
| q1 on q2 batch limit 8 | 34.756 | 35.651 | +12.3% | ok |
| q1 on q2 batch limit 16 | 34.016 | 34.930 | +9.9% | ok |
| q1 on q2 batch limit 32 | 33.708 | 34.636 | +8.9% | ok |

Old Q2 batching reproduced the old pattern: Q2 dequeue CAS fail per ok dropped a lot (`145.3` off to `1.25` at limit 32), but compute median still got worse. The best Q2 batch case was still +8.9% slower than Q2 off.

### Main sharded, 16-shard high-contention case

Flags: `--wg-queue-shards 16 --wg-q2-shards 16 --wg-q1-lane-pop --wg-node-c-start 72`.

| case | median ms | p90 ms | median delta | p90 delta | correctness |
|---|---:|---:|---:|---:|---|
| Q2 batch off | 4.244 | 4.906 | 0.0% | 0.0% | ok |
| limit 1 | 7.369 | 7.498 | +73.6% | +52.8% | ok |
| limit 2 | 5.128 | 5.277 | +20.8% | +7.6% | ok |
| limit 4 | 4.255 | 4.673 | +0.3% | -4.7% | ok |
| limit 8 | 4.174 | 5.102 | -1.7% | +4.0% | ok |
| limit 16 | 4.262 | 5.390 | +0.4% | +9.9% | ok |
| limit 32 | 4.117 | 5.263 | -3.0% | +7.3% | ok |

Q2 batching reduced Q2 dequeue CAS fail per ok from `12.16` to roughly `0.26-0.47`, but it did not produce a >=10% median win. The two median-improving cases, limits 8 and 32, worsened p90.

### Main sharded, 256-shard adopted-default case

Flags: `--wg-queue-shards 256 --wg-q2-shards 256 --wg-q1-lane-pop --wg-node-c-start 72`.

| case | median ms | p90 ms | median delta | p90 delta | correctness |
|---|---:|---:|---:|---:|---|
| Q2 batch off | 1.943 | 1.969 | 0.0% | 0.0% | ok |
| limit 1 | n/a | n/a | n/a | n/a | failed all repeats |
| limit 2 | n/a | n/a | n/a | n/a | failed all repeats |
| limit 4 | n/a | n/a | n/a | n/a | failed all repeats |
| limit 8 | 11.711 | 11.755 | +502.7% | +497.0% | ok |
| limit 16 | 11.428 | 11.466 | +488.1% | +482.3% | ok |
| limit 32 | 11.278 | 11.303 | +480.4% | +474.0% | ok |

Limits 1/2/4 exited with code `-1` in all timing and counter repeats. Limits 8/16/32 completed correctly, but were about 5.8x slower than batching off.

The counter shape is the main negative evidence: Q2 dequeue CAS fail per ok fell from `0.275` off to about `0.031`, but Q2 empty dequeue per ok exploded from `5.83` to about `116`. Batch slots per batch stayed around `0.26`, so most batch probes were empty or underfilled.

## Hypothesis

- Old unsharded Q2 batching reduces CAS contention, but it shifts cost into empty polling, partial batches, and batch coordination. End-to-end compute time gets worse even when CAS counters improve.
- In the 16-shard stress case, contention is high enough for batching to reduce CAS traffic, but the benefit is too small and tail latency often worsens. This suggests the batching overhead and ready/empty behavior are offsetting the atomic reduction.
- In the 256-shard path, simple subgroup batching is a poor fit. There are too many shards for a workgroup-local batch probe to fill reliably, so the shader spends most of the time scanning empty or underfilled shard batches.

## Conclusion

- The old data is reproducible in the important way: Q2 batching can lower CAS failures without lowering compute time.
- For the current 256-shard main path, "request batching not working" still holds strongly for this implementation. Small limits fail; larger limits are much slower.
- For the 16-shard high-contention case, batching is not a successful optimization. It may have low-shard diagnostic value because it lowers CAS traffic, but it does not meet the >=10% median improvement rule and p90 is not consistently safe.
- The result does not prove every possible wave-local architecture is bad. It only rejects this request/dequeue batching shape.

## Recommendation

- Do not adopt Q2 request/dequeue batching into the main optimized path.
- Do not spend more time on small batching tweaks before a different wave-local architecture. The failure mode is structural: empty polling, partial batches, and underfilled batches dominate.
- Keep the data as a negative controlled experiment: "batching reduces some atomic counters, but does not improve end-to-end compute under the accepted criteria."
