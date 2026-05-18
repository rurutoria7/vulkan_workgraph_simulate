# Weekly Report 260504

## 本週工作摘要

- 完成 `workgraph_poc` 的 metrics infrastructure，可以量測 GPU compute time、queue behavior、atomic contention、worker idle polling，以及各 node task count。
- Baseline workload 是 deterministic Koch snowflake，`MAX_DEPTH=8`。
- 正確性穩定：`edges=196608`、`vertices=393216`、`stop_writes=1`。
- 本週用 3 個 verification experiments 驗證 CAS bottleneck，再提出第 4 個 fix experiment：wave intrinsic batch atomic。

## Baseline Setup

| Item              | Value                         |
| -------------------| -------------------------------|
| Build target      | `workgraph_poc`               |
| Build config      | `Release`                     |
| GPU               | AMD Radeon RX 7900 XTX        |
| Workload          | Koch snowflake, `MAX_DEPTH=8` |
| Expected edges    | `196608`                      |
| Expected vertices | `393216`                      |

## Baseline Result

| Metric          | Avg       | Min       | Median    | Max       | Note　　　　　　　　　　　　　　　　　　|
| -----------------| ----------:| ----------:| ----------:| ----------:| -----------------------------------------|
| GPU frame ms    | `46.42`   | `44.99`   | `46.24`   | `49.60`   | reset + compute + metrics copy + render |
| Compute ms      | `46.29`   | `44.92`   | `46.17`   | `49.53`   | 主要成本　　　　　　　　　　　　　　　　|
| Metrics copy ms | `0.081`   | `0.018`   | `0.022`   | `0.404`   | 成本小，偶爾有 spike　　　　　　　　　　|
| Render ms       | `0.0277`  | `0.0262`  | `0.0277`  | `0.0285`  | 不是瓶頸　　　　　　　　　　　　　　　　|
| Edges / ms      | `4250.52` | `3969.31` | `4258.26` | `4376.76` | throughput　　　　　　　　　　　　　　　|
| Vertices / ms   | `8501.05` | `7938.63` | `8516.52` | `8753.52` | throughput　　　　　　　　　　　　　　　|

## Correctness Counters

| Counter | Value | Note |
|---|---:|---|
| `edges` | `196608` | final edges |
| `vertices` | `393216` | output vertices |
| `node_b_tasks` | `262143` | Node B 從 Q1 取得的 task |
| `node_b_subdivide` | `65535` | subdivision tasks |
| `node_b_final` | `196608` | Node B 送到 Q2 的 final edges |
| `node_c_output` | `196608` | Node C 寫出的 final edges |
| `stop_writes` | `1` | stop signal 只寫一次 |

## Queue Bottlenecks

| Metric              | Avg      | Observation　　　　　　　　　　　|                    |
| ---------------------| ---------:| ----------------------------------| --------------------|
| `q1_enq_cas_fail`   | `3.15e6` | Q1 producer tail contention 明顯 |                    |
| `q1_deq_cas_fail`   | `4.00e7` | 最大 hotspot　　　　　　　　　　 | <!-- highlight --> |
| `q2_enq_cas_fail`   | `5.12e5` | enqueue contention 較低　　　　　|                    |
| `q2_deq_cas_fail`   | `1.41e7` | dequeue contention 仍然很高　　　| <!-- highlight --> |
| `q1_high_water`     | `9.70e4` | 約 queue capacity 的 49%　　　　 |                    |
| `q2_high_water`     | `1.69e2` | Q2 occupancy 很低　　　　　　　　|                    |
| `q1_ready_max_spin` | `3.88`   | 影響不大　　　　　　　　　　　　 |                    |
| `q2_ready_max_spin` | `17.88`  | 有等待，但不是主要瓶頸　　　　　 |                    |

## Initial Hypotheses

1. 單一 hot counter 會限制 CAS throughput。
2. CAS fail / retry 會降低有效 queue throughput。
3. 降低 CAS traffic 應該能改善 compute time，但 worker parallelism、global atomics 或 empty polling 可能成為新的瓶頸。

## Experiment Taxonomy（分類方式）

| Experiment | Branch / commit | Purpose | Result |
|---|---|---|---|
| CAS multi-counter microbenchmark | `experiment-cas-multicounter` / `b33c6467` | 把 CAS traffic 分散到 N 個 counters | speedup 明顯，支持 hypothesis 1 |
| Workgroup CAS sweep | `experiment-wg-cas-sweep` / `4c91cbd0` | 降低 workgroup 數，觀察 CAS fail ratio | CAS fail 下降，但 parallelism 也下降，所以 compute time 上升 |
| Q1 queue sharding | `experiment-q1-queue-shards` / `097fa9ed` | 把 Q1 queue 切成 N 個 shards | Q1 CAS traffic 下降，但整體 compute time 只小幅改善 |
| Q1 wave-batched dequeue | `fix-wave-batched-atomics` / `b22e1d9a` | 用 wave intrinsic batch atomic 降低 Q1 dequeue CAS | Q1 dequeue CAS attempts 降 `375.82x`，compute 約 `1.10x` speedup | <!-- highlight -->

## Experiment 1: CAS Multi-Counter Microbenchmark

這個 microbenchmark 固定 CAS work 數量，並把 CAS increment 分散到 `N` 個 counters。

| counter_count | avg compute_ms | speedup vs 1 | CAS fail / success |
|---:|---:|---:|---:|
| 1 | `1923.06` | `1.00x` | `132.73` |
| 2 | `594.58` | `3.23x` | `240.88` |
| 4 | `369.53` | `5.20x` | `111.00` |
| 8 | `200.32` | `9.60x` | `58.32` |
| 16 | `82.15` | `23.41x` | `29.70` |
| 32 | `39.57` | `48.59x` | `16.93` |
| 64 | `21.10` | `91.14x` | `7.77` |
| 128 | `11.77` | `163.43x` | `3.65` | <!-- highlight -->

**Conclusion:** 單一 hot counter 是主要瓶頸。分散 CAS traffic 可以大幅降低 contention 與 retry cost。

## Experiment 2: Workgroup CAS Sweep

這個 experiment 固定 Koch task 數量，只改變 `NUM_WORKGROUPS`。

| workgroups | B/C workers | avg compute_ms | Q1 fail / success | Q2 fail / success |
|---:|---:|---:|---:|---:|
| 96 | `72/24` | `46.89` | `84.02` | `37.39` |
| 80 | `60/20` | `46.98` | `82.57` | `41.10` |
| 64 | `48/16` | `51.59` | `85.03` | `46.12` |
| 48 | `36/12` | `58.76` | `85.09` | `52.26` |
| 32 | `24/8` | `71.57` | `70.13` | `48.51` |
| 24 | `18/6` | `79.61` | `54.95` | `39.47` |
| 16 | `12/4` | `102.58` | `37.03` | `29.68` |
| 12 | `9/3` | `120.08` | `29.24` | `24.84` |
| 8 | `6/2` | `166.92` | `22.42` | `20.73` | <!-- highlight -->

**Conclusion:** 減少 workgroup 會降低 CAS fail ratio，但 compute time 反而變差，原因是 worker parallelism 也一起下降。

### Fixed Node C Check

| workgroups | B/C workers | avg compute_ms | Q1 fail / success | Q2 fail / success | Q2 empty / success |
|---:|---:|---:|---:|---:|---:|
| 96 | `72/24` | `46.95` | `83.66` | `37.90` | `119.75` |
| 80 | `56/24` | `48.72` | `77.93` | `40.62` | `167.72` |
| 64 | `40/24` | `52.70` | `73.76` | `48.83` | `221.79` |
| 48 | `24/24` | `62.09` | `61.44` | `67.93` | `348.49` |
| 40 | `16/24` | `72.31` | `48.16` | `85.06` | `475.04` |
| 32 | `8/24` | `101.19` | `30.19` | `112.18` | `806.05` | <!-- highlight -->

**Conclusion:** Node B workers 太少時，Q2 empty polling 會大幅惡化。

## Experiment 3: Q1 Queue Sharding

Q1 最多切成 `16` 個 shards。Runtime 使用 `--wg-queue-shards N` 選擇 `1/2/4/8/16` 個 shards。

| Q1 shards | correct frames | avg compute_ms | speedup | Q1 deq attempt reduction | Q1 deq probe reduction | Q1 deq fail / success |
|---:|---:|---:|---:|---:|---:|---:|
| 1 | `4/4` | `44.44` | `1.00x` | `1.00x` | `1.00x` | `366.15` |
| 2 | `4/4` | `44.30` | `1.00x` | `4.03x` | `1.07x` | `181.32` |
| 4 | `4/4` | `44.09` | `1.01x` | `15.19x` | `1.71x` | `95.65` |
| 8 | `4/4` | `43.40` | `1.02x` | `50.73x` | `3.08x` | `56.90` |
| 16 | `4/4` | `42.47` | `1.05x` | `157.48x` | `6.06x` | `36.30` | <!-- highlight -->

**Conclusion:** Q1 sharding 可以大幅降低 Q1 CAS traffic，但 compute time 只改善 `1.05x`。剩餘成本可能來自 Q2、global atomics，以及 empty polling。

## Experiment 4: Q1 Wave-Batched Dequeue Fix

這是第一個 fixes experiment。目標是用 wave intrinsic（wave / subgroup 內建操作）把 Q1 dequeue 的 per-invocation atomic 改成 per-wave batch atomic。這裡只做 P0：Q1 dequeue batching；Q1 enqueue、Q2、`vertexCount`、`totalProcessed` 都保持原樣，方便做 ablation study（消融研究）。

| Mode | metric rows | avg compute_ms | median compute_ms | p95 compute_ms | correctness | Q1 deq fail / success | Q1 enq fail / success | Q1 deq CAS attempts | Q1 deq CAS fail |
|---|---:|---:|---:|---:|---|---:|---:|---:|---:|
| scalar baseline | `12` | `44.3067` | `43.3185` | `49.9771` | `196608 / 393216` | `191.4292` | `11.5962` | `605327616` | `602181900` |
| Q1 wave batch | `12` | `40.2748` | `39.7179` | `47.1022` | `196608 / 393216` | `0.4803` | `114.4552` | `1610665` | `1510955` | <!-- highlight -->

Derived result:

| Metric | Value |
|---|---:|
| Compute speedup | `1.10x` |
| Q1 dequeue CAS attempt reduction | `375.82x` |
| Q1 dequeue CAS fail reduction | `398.54x` |
| Q1 dequeue batch slots | `3145716` |
| Q1 dequeue batches | `6698102` |

**Conclusion:** wave batching 成功打掉 Q1 dequeue hot counter。Q1 dequeue fail / success 從 `191.43` 降到 `0.48`，CAS attempts 下降約 `375.82x`。但是 Q1 enqueue fail / success 從 `11.60` 升到 `114.46`，代表 dequeue 被疏通後，producer side enqueue 變成新的瓶頸。下一個 fix 應該做 Q1 enqueue wave batching。 <!-- highlight -->

## 本週結論

1. Baseline 的主要 hotspot 是 Q1 dequeue CAS，其次是 Q2 dequeue CAS 與 Q2 empty polling。
2. CAS multi-counter microbenchmark 確認單一 hot counter 會限制 throughput。
3. Workgroup sweep 顯示降低 workgroup count 會降低 CAS fail ratio，但也會降低 parallelism，所以不能直接當成 fix。
4. Q1 queue sharding 證明 queue-level atomic traffic 可以被分散，但整體 compute improvement 仍有限。
5. Q1 wave-batched dequeue 是目前最有效的 fix：大幅降低 Q1 dequeue CAS traffic，並帶來約 `1.10x` compute speedup。 <!-- highlight -->
6. 新瓶頸已轉移到 Q1 enqueue，下一步應該針對 producer side 做 batch atomic。

## Next

- P1 fix：實作 Q1 enqueue wave batching，降低 producer side CAS contention。 <!-- highlight -->
- 之後再做 Q2 dequeue / enqueue batching，以及 `vertexCount` / `totalProcessed` wave-level atomic。
- 做完整 taxonomy（分類方式）：baseline、Q1 sharding only、wave batching only、Q1 sharding + wave batching。
- 保留 ablation study（消融研究）順序，不要一次混入 sharding、enqueue batching、Q2 batching。
- 繼續觀察 Q2 empty polling，必要時研究 Q2 sharding 或 B/C worker scheduling。
