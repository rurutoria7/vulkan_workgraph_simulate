# Weekly Report 260511

## 本週工作摘要

- 本週把 2026-05 workgraph bottleneck investigation 整理成 canonical 主線。
- 核心路徑是：RDP/RGP occupancy validation -> duration measurement 修正 -> B/C ratio -> Q1/Q2 16-shard -> Q1 lane-pop -> high shard。
- 後續 duration 結論只採 app 端 GPU timestamp；shader-side metrics counters 只用來看壓力結構。
- 最後採用的優化是 `Q1/Q2 sharding + Q1 lane-pop`，不是 wave/request batching。
- 目前 main integrated default 的 median compute 約 `0.99 ms`。

## 結果總表

| 階段 | 設定 | Compute duration |
| --- | --- | ---: |
| RDP/RGP occupancy validation | 多組 RGP capture / RDP-RDS trace 檢查 | 排除 wavefront=0 作為因果證據 |
| 修正量測後 baseline | Q1 batch on, Q2 scalar, `C=24/B=72` | 約 `30.13-30.35 ms` avg |
| B/C ratio | Q1 batch on, Q2 scalar, `C=40/B=56` | `27.97 ms` avg |
| Q1 sharding + B/C refine | `Q1s16/Q2s1`, `C=38/B=58` | `13.70 ms` avg |
| Q2 sharding + B/C rebalance | `Q1s16/Q2s16`, `C=16/B=80` | `5.57 ms` avg |
| Q1 lane-pop | `Q1s16/Q2s16`, lane-pop on, `C=24/B=72` | `3.85 ms` avg / `3.84 ms` median |
| High shard default | `Q1s256/Q2s256`, lane-pop on, `C=24/B=72` | 約 `0.99 ms` median |

## 推導方法

這份週報的 bottleneck 判斷分成兩種證據：

- duration 只採 app 端 GPU timestamp；counter-enabled run 不拿來比時間。
- shader counters 只看壓力結構，例如 CAS fail/ok、Q2 high-water、ready fail、empty probe。
- RGP Wavefront occupancy 只作輔助，不單獨推論 dispatch lifetime。

判斷規則：只有 duration 下降，而且 queue-pressure counters 同步下降，才把假設推進成結論。

| Metric | 代表什麼 | 用法 |
| --- | --- | --- |
| CAS fail / ok | 同一 queue head/count/tail 的競爭程度 | 定位 global queue hot spot |
| Q2 high-water | Q2 backlog 的高水位 | 判斷 C writers 是否追得上 |
| ready fail | consumer claim 後等待 ready flag | 檢查 publish / ready protocol |
| empty probe | worker 掃到空 shard 的成本 | 檢查 polling / scan-start 策略 |

## Bottleneck 推導鏈

| Step | 觀察到的 metrics | 猜測 | 驗證 / 排除 |
| --- | --- | --- | --- |
| RGP | 0 occupancy 但 event bar 很長；ALU-only 仍同型態 | RGP 視圖不足以判斷 lifetime | 降級為輔助證據 |
| B/C | Q2 high-water 約 `50k`；Q2 enqueue fail/ok 約 `67.8x` | C writers 不足 | `C=40/B=56` 到 `27.97 ms`；high-water 約 `9k` |
| Q1 | Q1 enqueue fail 仍高；`Q1s16` 單獨到 `22.96 ms` | Q1 global queue 是 hot atomic 結構 | `C=38/B=58` 疊加到 `13.70 ms` |
| Q2 | Q2 enqueue/dequeue 約 `47.8x / 255.9x`；high-water 約 `68k` | Q2 count/head/tail 是下一個 hot spot | `Q2s16` 後到 `5.57 ms`；high-water 約 `1.5k` |
| Lane-pop | Q1 deq fail/ok 約 `60.54x` | lane 從同一 shard 起掃造成局部競爭 | 降到 `15.38x -> 12.98x`；median `3.84 ms` |
| 256 shards | Q1/Q2 16 時 CAS fail/ok 仍高 | 剩餘主因仍是 queue atomic contention | CAS fail/ok 都低於 `0.5x`；median 約 `0.99 ms` |

## 量測修正

之前部分 duration run 沒有關掉 shader-side metrics counters，導致時間被 metrics atomics 污染。修正後：

- duration 結論只採 app 端 GPU timestamp。
- shader counters 只用來看 queue pressure、atomic contention、backlog 等結構。
- RGP event timing 只作輔助。

觀察 / 猜測 / 驗證：

- 觀察：counter-enabled duration 和 counter reduction 不一致。
- 猜測：metrics atomics 污染 timing。
- 驗證：改用 timestamp-only 後，再重掃主線。

## RDP/RGP Occupancy Validation

RGP Wavefront occupancy 在這輪 capture 裡不能當作完整 compute dispatch lifetime。

關鍵觀察：

| 實驗 | 觀察 | 排除 |
| --- | --- | --- |
| Wavefront-only RGP | Dispatch 仍約 `38.7 ms`，occupancy 仍只在前段 | 不是 profiler counters / instruction tracing 主導 |
| No output writes | App timestamp 仍約 `30 ms`，occupancy 形狀不變 | 不是 output buffer write/drain 主導 |
| Scalar dequeue | Scalar 也有前段 occupancy、中後段空白 | 不是 batch dequeue 專屬 bug |
| Depth7 scaling | GPU timestamp 約 `7.63 ms`，RGP event 約 `10.28 ms` | 不是固定 overhead 或完全假時間 |
| Artificial ALU tail | GPU timestamp 從約 `30 ms` 增到約 `45 ms` | 不是 wavefront 真的只在前段執行 |
| ALU-only shader | 無 queue / atomic，GPU timestamp 約 `29.44 ms` | 不是 persistent queue / atomic 特有現象 |

結論：`wavefront=0` / `in-flight threads=0` 只能代表 RGP occupancy 視圖在該區間沒有觀測到 wavefront，不能直接推論 shader 已結束，也不能直接推論中後段是 memory/atomic stall。

觀察 / 猜測 / 驗證：

- 觀察：0 occupancy 和很長的 event bar 同時存在。
- 猜測：這可能不是 shader 真正沒有 wavefront，而是 RGP 視圖可觀測性不足。
- 驗證：ALU-only shader 和 artificial ALU tail 仍出現同型態，所以 RGP occupancy 只作輔助證據。

## NVIDIA Nsight / GTX 1080 Ti 支援限制

這一頁用 NVIDIA 官方文件補上工具限制：我們要的是 GPU Trace 類型的性能分析，不只是 frame debugging。

- Nsight Graphics GPUs Full List 中，`GeForce GTX 1080 Ti[1]` 被註記為 [1]。
- 頁面底部註解 [1] 寫明 GTX 1080 Ti 這類 GPU 可做 Frame Debugging 和 C++ Capture，但 `"profiling activities are not supported."`
- Nsight Graphics User Guide 的 GPU Trace Overview 說明，GPU Trace Profiler 是 low-level profiler，用來看 GPU units utilization，且支援範圍是 `"Turing architecture and above."`

結論：

- GeForce GTX 1080 Ti 屬 Pascal，低於 GPU Trace 要求的 Turing+。
- 官方標註 GeForce GTX 1080 Ti 不支援 profiling activities。
- 這次分析需要的是 occupancy / SM / warp / memory profiling，不是 frame debugging。

參考：

- NVIDIA Nsight Graphics GPUs Full List: <https://developer.nvidia.com/nsight-graphics-gpus-full-list>
- NVIDIA Nsight Graphics User Guide - GPU Trace Overview: <https://docs.nvidia.com/nsight-graphics/UserGuide/gpu-trace-overview.html>

## B/C Ratio

修正 duration 後，第一個問題是 Node C writer 是否不足，造成 Q2 backlog。

| C workgroups | B workgroups | GPU timestamp avg compute |
| ---: | ---: | ---: |
| 24 | 72 | `30.35 ms` |
| 32 | 64 | `28.44 ms` |
| 40 | 56 | `27.97 ms` |
| 48 | 48 | `28.59 ms` |
| 64 | 32 | `30.25 ms` |

結論：`C=40/B=56` 在這個階段最好。Q2 high-water 約從 `50k` 降到 `9k`，timestamp duration 改善約 `8.5%`。

觀察 / 猜測 / 驗證：

- 觀察：baseline 的 Q2 high-water 約 `50k`，Q2 enqueue fail/ok 約 `67.8x`。
- 猜測：C writers 原本不足，Q2 backlog 放大 enqueue 競爭。
- 驗證：`C=40/B=56` 後 Q2 high-water 約 `9k`，Q2 enqueue fail/ok 約 `31.8x`，compute 到 `27.97 ms`。

## Q1 Sharding + B/C Refine

Q1 enqueue/dequeue contention 仍然明顯，所以加入 Q1 sharding 並重新掃 B/C ratio。

| Q1 shards | C workgroups | B workgroups | Avg compute |
| ---: | ---: | ---: | ---: |
| 4 | 40 | 56 | `23.24 ms` |
| 8 | 40 | 56 | `17.90 ms` |
| 16 | 36 | 60 | `13.95 ms` |
| 16 | 38 | 58 | `13.70 ms` |
| 16 | 40 | 56 | `14.08 ms` |

結論：Q1 sharding 和 B/C ratio 改善可以疊加。此階段最佳點是 `Q1s16/Q2s1`, `C=38/B=58`, 約 `13.70 ms`。

觀察 / 猜測 / 驗證：

- 觀察：`Q1 shards=16, C=24/B=72` 單獨把 compute 拉到約 `22.96 ms`。
- 猜測：Q1 global queue 是 hot atomic 結構。
- 驗證：和 B/C refine 疊加後到 `13.70 ms`；但 Q2 enqueue/dequeue counters 仍高，下一步轉向 Q2 sharding。

## Q2 Sharding + B/C Rebalance

Q1 sharded 後，Q2 global `count/head/tail` 成為下一個主要壓力點。

| Q2 shards | Avg compute |
| ---: | ---: |
| 1 | `16.95 ms` |
| 2 | `10.24 ms` |
| 4 | `8.38 ms` |
| 8 | `6.87 ms` |
| 16 | `6.08 ms` |

重新平衡 B/C 後：

| C workgroups | B workgroups | Avg compute |
| ---: | ---: | ---: |
| 12 | 84 | `5.62 ms` |
| 16 | 80 | `5.57 ms` |
| 24 | 72 | `5.66 ms` |
| 38 | 58 | `6.09 ms` |

結論：Q2 sharding 幾乎移除單一 global Q2 queue bottleneck。此階段最佳點是 `Q1s16/Q2s16`, `C=16/B=80`, 約 `5.57 ms`。

觀察 / 猜測 / 驗證：

- 觀察：Q2 enqueue/dequeue fail/ok 約 `47.8x / 255.9x`，Q2 high-water 約 `68k`。
- 猜測：Q2 global count/head/tail 是 Q1 sharding 後的下一個 hot spot。
- 驗證：`Q2s16` 後 Q2 high-water 從約 `68k` 降到約 `1.5k`，重掃 B/C 後 compute 到 `5.57 ms`。

## Q1 Lane-Pop

Q1/Q2 都 sharded 到 16 後，同一 workgroup 內所有 lane 仍可能從同一 shard 開始 pop，造成局部 shard contention。

- 舊行為：`wgId % Q1_QUEUE_SHARDS`
- 新行為：`(wgId * local_size_x + localId) % Q1_QUEUE_SHARDS`

| Mode | Avg compute | Median compute | Min compute |
| --- | ---: | ---: | ---: |
| Q1 lane-pop off, `C=16/B=80` | `6.01 ms` | `5.60 ms` | `5.54 ms` |
| Q1 lane-pop on, `C=16/B=80` | `4.39 ms` | `4.00 ms` | `3.90 ms` |
| Q1 lane-pop on, `C=24/B=72` | `3.85 ms` | `3.84 ms` | `3.73 ms` |

結論：Q1 lane-pop 是 shard selection / scan-start strategy，不是 request batching。它明顯降低 Q1 dequeue contention。

觀察 / 猜測 / 驗證：

- 觀察：lane-pop off 時 Q1 deq fail/ok 約 `60.54x`。
- 猜測：同一 workgroup 內所有 lane 從同一 shard 起掃，造成局部 shard contention。
- 驗證：lane-pop on 後 Q1 deq fail/ok 降到 `15.38x`，重掃 C 後約 `12.98x`；median 到 `3.84 ms`。

## High Shard Scaling

Q1/Q2 16-shard plus lane-pop 後，剩餘成本仍追蹤 queue atomic contention，所以繼續掃 shard count。

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

代表性非對稱結果：

| Q1 shards | Q2 shards | C/B | Median compute |
| ---: | ---: | ---: | ---: |
| 256 | 256 | 24/72 | `0.987 ms` |
| 256 | 192 | 24/72 | `1.006 ms` |
| 192 | 256 | 24/72 | `1.066 ms` |
| 256 | 128 | 24/72 | `1.955 ms` |

結論：目前最佳 default 是 Q1/Q2 都 256 shards。

觀察 / 猜測 / 驗證：

- 觀察：`Q1s16/Q2s16` 時，Q1 deq fail/ok、Q2 enqueue fail/ok、Q2 dequeue fail/ok 仍分別約 `12.98x / 7.67x / 11.70x`。
- 猜測：剩餘主因仍是 queue atomic contention。
- 驗證：`Q1s256/Q2s256` 後 counters 降到約 `0.44x / 0.45x / 0.27x`，median 到約 `0.99 ms`。

## 未採用方案 / 排除項目

- Output writes 不是主要瓶頸：output writes off 只改善約 `3.86 ms -> 3.78 ms`。
- Q2 ready-first pop 較慢：約 `3.86 ms -> 5.06 ms`，且 ready fail / high-water 變大。
- C output batching 沒有打贏 batch size 1，global output atomics 不是主要問題。
- Q2 naive request batching / subgroup batch claim 沒有採用，因為會放大 ready-spin 並讓 timestamp duration 變差。
- Wave/request batching 不屬於目前 main optimized path。

## Adopted Default

最後採用的 default：

- `MAX_QUEUE_SHARDS=256`
- `Q1_QUEUE_SHARDS_DEFAULT=256`
- `Q2_QUEUE_SHARDS_DEFAULT=256`
- Q1 lane-pop enabled by default
- `NODE_C_START=72`，也就是 `C=24/B=72`

main worktree confirmation：

| Run | Median compute | P10 | P90 | Min compute |
| --- | ---: | ---: | ---: | ---: |
| repeat 1 | `0.9905 ms` | `0.9804 ms` | `1.0043 ms` | `0.9668 ms` |
| repeat 2 | `0.9915 ms` | `0.9819 ms` | `1.0056 ms` | `0.9682 ms` |
| repeat 3 | `0.9906 ms` | `0.9810 ms` | `1.0046 ms` | `0.9593 ms` |
| repeat 4 | `0.9902 ms` | `0.9806 ms` | `1.0042 ms` | `0.9615 ms` |

## 本週結論 / Next

1. RGP Wavefront occupancy 在本輪 capture 中不能單獨作為 dispatch lifetime 證據。
2. duration 判斷已改以 app 端 GPU timestamp 為準。
3. Q1 sharding、Q2 sharding、Q1 lane-pop 是本週主要有效優化。
4. 最後採用的 main path 是 sharding + shard selection strategy，不是 wave/request batching。
5. 後續優化以 `Q1/Q2=256`, Q1 lane-pop on, `C=24/B=72` 作為起點。

## References

- `reports/2026-05/metrics/q2_shards_20260510/research_backbone_20260510.md`
- `reports/2026-05/metrics/rgp_occupancy_20260509/occupancy_elimination_notes.md`
- `reports/2026-05/metrics/rgp_occupancy_20260509/bottleneck_investigation_20260510.md`
- `reports/2026-05/metrics/q1_shards_nodec_20260510/q1_shards_nodec_findings.md`
- `reports/2026-05/metrics/q2_shards_20260510/q2_sharding_findings.md`
- `reports/2026-05/metrics/q2_shards_20260510/q1_lane_pop_findings.md`
