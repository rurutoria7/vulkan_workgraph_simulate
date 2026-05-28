# WorkGraph bottleneck research backbone

日期：2026-05-10

這份文件是 2026-05 workgraph bottleneck investigation 的 canonical 研究骨幹。用途是把零散實驗整理成一條可引用的路徑，供 weekly report、後續優化，以及 Codex agent 接手時使用。

核心路徑：

```text
RDP/RGP occupancy validation -> fix clean duration -> B/C ratio -> Q1/Q2 16-shard -> Q1 lane-pop -> high shard
```

重要前提：

- Clean duration 指 app 端 GPU timestamp，只保留 timestamp，不開 shader-side metrics counters。
- Shader counters 只用來看壓力結構，不用來比較 duration。
- RGP Wavefront occupancy 在本輪 capture 中不能當作完整 compute dispatch lifetime；不要用 occupancy 中後段為 0 直接推論 shader 已經結束或卡在 memory/atomic。
- 最後合入 main 的優化是 sharding + shard selection strategy，不是 wave/request batching。

## 結果總表

| 階段 | 設定 | Clean duration |
| --- | --- | ---: |
| RDP/RGP occupancy validation | 多組 RGP capture / RDP-RDS trace 檢查 | 排除 wavefront=0 作為因果證據 |
| 修正量測後 baseline | Q1 batch on, Q2 scalar, `C=24/B=72` | 約 `30.13-30.35 ms` avg |
| B/C ratio | Q1 batch on, Q2 scalar, `C=40/B=56` | `27.97 ms` avg |
| Q1 sharding + B/C refine | `Q1s16/Q2s1`, `C=38/B=58` | `13.70 ms` avg |
| Q2 sharding + B/C rebalance | `Q1s16/Q2s16`, `C=16/B=80` | `5.57 ms` avg |
| Q1 lane-pop | `Q1s16/Q2s16`, lane-pop on, `C=24/B=72` | `3.85 ms` avg / `3.84 ms` median |
| High shard default | `Q1s256/Q2s256`, lane-pop on, `C=24/B=72` | 約 `0.99 ms` median |

## 0. RDP/RGP occupancy validation

Problem:

Batch dequeue 版本在 RGP Wavefront occupancy 圖中，中後段顯示 0 occupancy / 0 in-flight threads，但 event bar 仍然很長，而且 cache / memory counters 還有 activity。這看起來像是「shader 沒有 wavefront 卻還在 memory R/W」，需要先判斷它是 shader bug、atomic stall、還是 profiler/capture artifact。

Run / metrics:

- 確認 RDP/RDS connection 會影響是否產生新 `.rgp`；有效 trace 必須有新的檔名、時間戳與檔案大小。
- 用 wavefront-only RGP capture 關掉 counters / instruction tracing / shader instrumentation。
- 移除 output vertex buffer writes。
- 用 scalar dequeue 對照 batch dequeue。
- 用 depth7 workload 檢查 workload scaling。
- 在 stopFlag 後加入人工 ALU tail，讓 shader 執行尾段明確變長。
- 寫極簡 ALU-only shader，移除 queue / atomic，只保留固定長度運算。

Key observations:

| 實驗 | 觀察 | 排除 |
| --- | --- | --- |
| Wavefront-only RGP | Dispatch 仍約 `38.7 ms`，occupancy 仍只在前段 | 不是 profiler counters / instruction tracing 主導 |
| No output writes | App timestamp 仍約 `30 ms`，occupancy 形狀不變 | 不是 output buffer write/drain 主導 |
| Scalar dequeue | Scalar 也有前段 occupancy、中後段空白 | 不是 batch dequeue 專屬 bug |
| Depth7 scaling | Clean timestamp 約 `7.63 ms`，RGP event 約 `10.28 ms` | 不是固定 overhead 或完全假時間 |
| Artificial ALU tail | Clean timestamp 從約 `30 ms` 增到約 `45 ms`，RGP event 約 `59.6 ms`，occupancy 仍只在前段 | 不是 wavefront 真的只在前段執行 |
| ALU-only shader | 無 queue / atomic，clean timestamp 約 `29.44 ms`，RGP event 約 `38.80 ms`，occupancy 仍只在前段 | 不是 persistent queue / atomic 特有現象 |

Conclusion:

這組 capture 中，RGP Wavefront occupancy 對 compute dispatch lifetime 的可觀測性不足。`wavefront=0` / `in-flight threads=0` 只能解讀成「RGP occupancy 視圖在該區間沒有觀測到 wavefront」，不能直接推論 shader 已經結束，也不能直接推論中後段是 atomic 或 memory R/W 單獨塞住。

Next problem:

後續 bottleneck 判斷改以 app GPU timestamp 作為 clean duration，RGP event timing 只作輔助，shader counters 只作壓力結構證據。

## 1. Fix clean duration

Problem:

Q1 dequeue wave-batch / counter reductions looked large, but duration did not drop accordingly. The immediate issue was measurement validity, not a new shader bottleneck.

Run / metrics:

- Rechecked timing mode.
- Confirmed that shader-side metrics counters had not been disabled for duration runs.
- Switched clean timing to timestamp-only runs.

Observation:

- The previous duration numbers were polluted by shader-side metrics atomics.
- After this point, duration conclusions must use clean timestamp runs.
- Counter-enabled runs remain useful only as structural evidence.

Conclusion:

This was a measurement bug. It was fixed before continuing the bottleneck investigation.

## 2. B/C ratio

Problem:

After clean timing was fixed, the active question was whether Node C writers were insufficient and causing Q2 backlog.

Run / metrics:

- Kept total workgroups at 96.
- Swept Node C writer count while Q1 batch-on branch state and scalar Q2 were still under investigation.

Key results:

| C workgroups | B workgroups | Clean timestamp avg compute |
| ---: | ---: | ---: |
| 24 | 72 | `30.35 ms` |
| 32 | 64 | `28.44 ms` |
| 40 | 56 | `27.97 ms` |
| 48 | 48 | `28.59 ms` |
| 64 | 32 | `30.25 ms` |

Observation:

- `C=40/B=56` was best in this setup.
- Q2 high-water dropped from about `50k` at `C=24/B=72` to about `9k` at `C=40/B=56`.

Conclusion:

The original writer count was too low for that stage. Increasing Node C writers reduced Q2 backlog and improved clean duration by about `8.5%`.

Next problem:

Q2 contention was lower but still high; Q1 and Q2 global queues were still single hot atomic structures.

## 3. Q1 sharding + B/C refine

Problem:

Q1 enqueue/dequeue contention remained significant. The next question was whether sharding Q1 and rebalancing B/C could stack.

Run / metrics:

- Added Q1 sharding.
- Swept Q1 shard counts and B/C ratios.

Key results:

| Q1 shards | C workgroups | B workgroups | Avg compute |
| ---: | ---: | ---: | ---: |
| 4 | 40 | 56 | `23.24 ms` |
| 8 | 40 | 56 | `17.90 ms` |
| 16 | 36 | 60 | `13.95 ms` |
| 16 | 38 | 58 | `13.70 ms` |
| 16 | 40 | 56 | `14.08 ms` |

Observation:

- Q1 sharding and B/C ratio improvements stacked.
- Best point in this stage was `Q1s16/Q2s1`, `C=38/B=58`, about `13.70 ms`.
- Counters still showed Q2 enqueue/dequeue contention and Q2 backlog.

Conclusion:

Q1 sharding was a major improvement, but Q2 became the next obvious global queue pressure point.

Next problem:

Shard Q2 instead of applying naive Q2 request batching.

## 4. Q2 sharding + B/C rebalance

Problem:

With Q1 sharded, Q2 global `count/head/tail` became a large remaining source of contention.

Run / metrics:

- Added `--wg-q2-shards`.
- Swept Q2 shards while keeping Q1 shards at 16.
- Rebalanced B/C after Q2 contention dropped.

Key results:

| Q2 shards | Avg compute |
| ---: | ---: |
| 1 | `16.95 ms` |
| 2 | `10.24 ms` |
| 4 | `8.38 ms` |
| 8 | `6.87 ms` |
| 16 | `6.08 ms` |

After B/C rebalance:

| C workgroups | B workgroups | Avg compute |
| ---: | ---: | ---: |
| 12 | 84 | `5.62 ms` |
| 16 | 80 | `5.57 ms` |
| 24 | 72 | `5.66 ms` |
| 38 | 58 | `6.09 ms` |

Observation:

- Q2 sharding nearly removed the single global Q2 queue bottleneck.
- Best point in this stage was `Q1s16/Q2s16`, `C=16/B=80`, about `5.57 ms`.
- Remaining pressure pointed back to Q1 dequeue polling / CAS behavior and shard access strategy.

Conclusion:

Q2 sharding was the second major structural optimization.

Next problem:

Improve shard access strategy, especially Q1 pop behavior.

## 5. Q1 lane-pop

Problem:

With Q1/Q2 both sharded to 16, all lanes in the same workgroup could still start Q1 pop from the same shard. This created avoidable local shard contention.

Run / metrics:

- Added Q1 lane-pop.
- Old behavior: Q1 pop starts from `wgId % Q1_QUEUE_SHARDS`.
- New behavior: Q1 pop starts from `(wgId * local_size_x + localId) % Q1_QUEUE_SHARDS`.

Key results:

| Mode | Avg compute | Median compute | Min compute |
| --- | ---: | ---: | ---: |
| Q1 lane-pop off, `C=16/B=80` | `6.01 ms` | `5.60 ms` | `5.54 ms` |
| Q1 lane-pop on, `C=16/B=80` | `4.39 ms` | `4.00 ms` | `3.90 ms` |
| Q1 lane-pop on, `C=24/B=72` | `3.85 ms` | `3.84 ms` | `3.73 ms` |

Observation:

- Lane-pop strongly reduced Q1 dequeue contention.
- After lane-pop, `C=24/B=72` was better than the previous `C=16/B=80` rebalance point because B consumption and Q2 production changed.

Conclusion:

Q1 lane-pop is part of the adopted optimization path. It is not request batching; it is a better shard selection / scan-start strategy.

Next problem:

Check whether the remaining cost is output writes, ready protocol, writer batching, or simply insufficient shard count.

## 6. High shard scaling

Problem:

After Q1/Q2 16-shard plus lane-pop, the remaining bottleneck still tracked queue atomic contention. The next question was where the shard-count tradeoff peaks.

Run / metrics:

- Swept equal Q1/Q2 shard counts up to 512.
- Also ran asymmetric sweeps around 256.
- Tested output-write removal, Q2 ready-first pop, and C output batching as alternative explanations.

Key equal-shard results:

| Q1/Q2 shards | Best C/B tested | Avg compute | Median compute | Min compute |
| ---: | ---: | ---: | ---: | ---: |
| 16 | 24/72 | `3.91 ms` | `3.86 ms` | `3.77 ms` |
| 32 | 28/68 | `2.46 ms` | `2.40 ms` | `2.36 ms` |
| 64 | 28/68 | `1.56 ms` | `1.54 ms` | `1.52 ms` |
| 128 | 24/72 | `1.19 ms` | `1.14 ms` | `1.12 ms` |
| 192 | 24/72 | `1.13 ms` | `1.08 ms` | `1.05 ms` |
| 256 | 24/72 | `1.00 ms` | `0.99 ms` | `0.96 ms` |
| 384 | 24/72 | `1.05 ms` | `1.05 ms` | `1.02 ms` |
| 512 | 28/68 | `1.13 ms` | `1.12 ms` | `1.09 ms` |

Representative asymmetric results:

| Q1 shards | Q2 shards | C/B | Median compute |
| ---: | ---: | ---: | ---: |
| 256 | 256 | 24/72 | `0.987 ms` |
| 256 | 192 | 24/72 | `1.006 ms` |
| 192 | 256 | 24/72 | `1.066 ms` |
| 256 | 128 | 24/72 | `1.955 ms` |

Eliminated alternatives:

- Output writes were not the main bottleneck: output writes off only improved about `3.86 ms -> 3.78 ms`.
- Q2 ready-first pop was slower: about `3.86 ms -> 5.06 ms`.
- C output batching did not beat batch size 1.

Conclusion:

The final adopted default is:

- `MAX_QUEUE_SHARDS=256`
- `Q1_QUEUE_SHARDS_DEFAULT=256`
- `Q2_QUEUE_SHARDS_DEFAULT=256`
- Q1 lane-pop enabled by default
- `NODE_C_START=72`, meaning `C=24/B=72`

Main worktree confirmation:

| Run | Median compute | P10 | P90 | Min compute |
| --- | ---: | ---: | ---: | ---: |
| repeat 1 | `0.9905 ms` | `0.9804 ms` | `1.0043 ms` | `0.9668 ms` |
| repeat 2 | `0.9915 ms` | `0.9819 ms` | `1.0056 ms` | `0.9682 ms` |
| repeat 3 | `0.9906 ms` | `0.9810 ms` | `1.0046 ms` | `0.9593 ms` |
| repeat 4 | `0.9902 ms` | `0.9806 ms` | `1.0042 ms` | `0.9615 ms` |

2026-05-28 measurement stability note:

- A later rerun showed apparent `~1.9 ms` high-shard timing and `~40 ms`
  baseline timing. This was investigated in
  `reports/2026-05/metrics/perf_inconsistency_20260528/perf_inconsistency_findings.md`.
- The high-shard `~1.9 ms` case reproduced on both current HEAD and old
  optimized commit `5e57e873`. One sequence returned to `~1.0 ms` after
  explicitly running the RX 7900 XTX as `--gpu 0`, but follow-up controlled
  runs showed `--gpu 0` is not a deterministic recovery switch. Treat this as
  an AMD driver / GPU performance-state / timing-state measurement problem
  unless a lower-level driver trace proves otherwise. ADL PMLog follow-up
  showed the current slow default path running around `1.45-1.50 GHz` GFXCLK
  with no throttle flags, while the unsharded scalar path can drive the same
  RX 7900 XTX to about `3.17 GHz`; see the perf inconsistency report for the
  raw sensor/timing CSVs.
- The `~40 ms` baseline is the scalar-Q1 baseline, not the old Q1-batched
  `~30 ms` baseline. Do not compare those two as the same configuration.

## What was not adopted

- Q2 naive request batching / subgroup batch claim was not adopted. It reduced some CAS pressure but amplified ready-spin and made clean duration worse.
- Q2 ready-first pop was not adopted; it increased polling and backlog.
- C output batching was not adopted; output/global processed atomics were not the main bottleneck at this stage.
- Wave/request batching is not part of the current main optimized path. The main path is sharding plus shard selection strategy.

## References

- `reports/2026-05/metrics/rgp_occupancy_20260509/occupancy_elimination_notes.md`
- `reports/2026-05/metrics/rgp_occupancy_20260509/bottleneck_investigation_20260510.md`
- `reports/2026-05/metrics/q1_shards_nodec_20260510/q1_shards_nodec_findings.md`
- `reports/2026-05/metrics/q2_shards_20260510/q2_sharding_findings.md`
- `reports/2026-05/metrics/q2_shards_20260510/q1_lane_pop_findings.md`
- `reports/2026-05/metrics/q2_shards_20260510/shard_scale_and_writer_ablation_20260510.md`
