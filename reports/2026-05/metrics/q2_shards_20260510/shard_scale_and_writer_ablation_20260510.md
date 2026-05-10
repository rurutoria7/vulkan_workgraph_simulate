# Shard scale 與 writer 消融測試結果

日期：2026-05-10

## 背景

前一輪最佳組合是：

- `Q1 shards=16`
- `Q2 shards=16`
- `--wg-q1-lane-pop`
- `C=24/B=72`
- clean timestamp 中位數約 `3.84 ms`

這一輪要確認剩下的瓶頸是：

1. C writer 的 output writes / global output atomics，
2. Q2 ready-spin / publish ordering，
3. C writer batching 的優化機會，
4. 還是 queue head/count atomic contention。

所有 clean duration 都使用 `--wg-timestamps-only`。Shader counters 只作為結構性證據，不拿來當乾淨時間。

## Output Write 消融測試

`--wg-no-output-writes` 會保留 `totalProcessed`，所以終止條件和工作量不變。它只跳過 `vertexCount` 和 vertex buffer writes。

固定 `Q1s16/Q2s16/C24`，lane-pop on：

| 模式 | 平均 compute | 中位數 compute | 最小 compute |
| --- | ---: | ---: | ---: |
| Output writes on | 3.86 ms | 3.84 ms | 3.70 ms |
| Output writes off | 3.78 ms | 3.78 ms | 3.68 ms |

結論：output stores 和 `vertexCount` atomic 目前不是主要瓶頸。它們對 tail/outliers 有一點影響，但不足以解釋剩餘 duration。

## Q2 Ready-First Pop

測試一個變體：Q2 consumers 先檢查 head slot 的 ready flag，再推進 head/count。

固定 `Q1s16/Q2s16/C24`，lane-pop on：

| 模式 | 平均 compute | 中位數 compute | 最小 compute |
| --- | ---: | ---: | ---: |
| Existing pop | 3.86 ms | 3.85 ms | 3.75 ms |
| Q2 ready-first pop | 5.06 ms | 5.05 ms | 4.81 ms |

Counters：

| 模式 | Q2 ready fail | Q2 high-water |
| --- | ---: | ---: |
| Existing pop | 159K | 41K |
| Q2 ready-first pop | 6,682K | 79K |

結論：ready-spin 不是主要時間瓶頸。ready-first 變體避免 consumer 卡在一個已 claim 的 slot 上，但它造成更多 polling 和更大的 Q2 backlog。因此不要採用這個變體。

## C Writer Batching

測試 `--wg-c-output-batch N`。這個模式讓每個 C lane 一次 pop 多個 Q2 tasks，然後用一次 `vertexCount` atomic 和一次 `totalProcessed` atomic 處理整批。

固定 `Q1s16/Q2s16/C24`，lane-pop on：

| Batch | 平均 compute | 中位數 compute |
| ---: | ---: | ---: |
| 1 | 3.85 ms | 3.85 ms |
| 2 | 3.95 ms | 3.89 ms |
| 4 | 3.90 ms | 3.90 ms |
| 8 | 3.96 ms | 3.96 ms |

針對 batch=4 重新調整 C 數量後，仍然沒有贏過 batch=1。結論：global output/processed atomic frequency 不是主要問題。Batching 會增加 queue pop 和 register pressure，但沒有明確收益。

## Queue Shard Scaling

關鍵實驗是提高 `MAX_QUEUE_SHARDS`，並掃描 Q1/Q2 相同 shard 數。Lane-pop 保持 on，ready-first 保持 off，C output batch 保持 1。

| Q1/Q2 shards | 最佳測試 C/B | 平均 compute | 中位數 compute | 最小 compute |
| ---: | ---: | ---: | ---: | ---: |
| 16 | 24/72 | 3.91 ms | 3.86 ms | 3.77 ms |
| 32 | 28/68 | 2.46 ms | 2.40 ms | 2.36 ms |
| 64 | 28/68 | 1.56 ms | 1.54 ms | 1.52 ms |
| 96 | 24/72 | 1.35 ms | 1.31 ms | 1.29 ms |
| 128 | 24/72 | 1.19 ms | 1.14 ms | 1.12 ms |
| 192 | 24/72 | 1.13 ms | 1.08 ms | 1.05 ms |
| 256 | 24/72 | 1.00 ms | 0.99 ms | 0.96 ms |
| 384 | 24/72 | 1.05 ms | 1.05 ms | 1.02 ms |
| 512 | 28/68 | 1.13 ms | 1.12 ms | 1.09 ms |

目前最佳測量組合：

- `MAX_QUEUE_SHARDS=512`
- `Q1 shards=256`
- `Q2 shards=256`
- `--wg-q1-lane-pop`
- `C=24/B=72`
- `--wg-c-output-batch 1`
- `--wg-no-q2-ready-first-pop`
- clean timestamp 中位數約 `0.99 ms`

相較 `Q1s16/Q2s16/C24/lane-pop` 約 `3.84 ms`，這約快 `3.9x`。相較早期 Q1-shard branch baseline 約 `41.41 ms`，這約快 `41.9x`。

## Counter 證據

代表性的 shader-counter runs：

| Case | Q1 deq fail/ok | Q2 enq fail/ok | Q2 deq fail/ok | Q2 ready fail | Q2 high-water |
| --- | ---: | ---: | ---: | ---: | ---: |
| Q1s16/Q2s16/C24 | 12.98x | 7.67x | 11.70x | 159K | 41K |
| Q1s32/Q2s32/C28 | 3.93x | 4.01x | 3.90x | 150K | 34K |
| Q1s256/Q2s256/C24 | 0.44x | 0.45x | 0.27x | 14K | 28K |

這支持 timing 結果：剩餘瓶頸是 queue atomic contention，更細的 sharding 會直接降低 contention。384/512 開始變慢，表示到那裡已經越過交叉點，額外 shard 帶來的 probing/indexing overhead 抵銷了 contention 降低的收益。

## 已排除假說

- Output vertex writes 不是主要瓶頸。
- Q2 ready-spin / publish ordering 不是主要瓶頸；ready-first 變體更慢。
- C writer batching 對這個設計沒有幫助。
- Shard imbalance 不是問題；16-shard runs 的 imbalance counters 接近 1.0。
- 這裡仍然不把 RGP wavefront occupancy 當作因果證據。Clean timestamp 和 shader counters 已足夠識別 queue-contention trend。

## 建議優化方向

1. 將高 shard 數整理成可維護的 implementation。實用 default candidate 是 `MAX_QUEUE_SHARDS=512`，runtime default `Q1/Q2 shards=256`。
2. 預設開啟 `--wg-q1-lane-pop`。
3. 不要把 Q2 ready-first pop 或 C output batching 合併成 default；如果未來還要做消融測試，可以保留成 experimental flags。
4. 下一步清理 experimental flags，並在 shader metrics off 的情況下重跑 final confirmation，再依穩定性和 memory footprint 決定 192 或 256 比較適合做 default。

## 後續：不對稱 Shard Sweep 與候選預設

接著做 asymmetric sweep（不對稱掃描，也就是 Q1 和 Q2 shard 數分開調），確認 `256/256` 是否真的兩個 queue 都需要。

固定 lane-pop on、output writes on、Q2 ready-first off、C output batch 1：

| Q1 shards | Q2 shards | C/B | 中位數 compute |
| ---: | ---: | ---: | ---: |
| 256 | 256 | 24/72 | 0.987 ms |
| 256 | 192 | 24/72 | 1.006 ms |
| 256 | 384 | 24/72 | 1.019 ms |
| 384 | 256 | 24/72 | 1.026 ms |
| 192 | 256 | 24/72 | 1.066 ms |
| 128 | 256 | 24/72 | 1.113 ms |
| 256 | 128 | 24/72 | 1.955 ms |

解讀：

- `Q1=256/Q2=256` 仍是目前測過的最佳 default。
- Q2 降到 128 特別差，代表 Q1 修好後，Q2 仍然需要足夠多的 shards。
- 任一邊提高到 384 都沒有幫助；256 接近有效區間的上緣，再往上 probing 和 indexing overhead 會開始主導。

之後 worktree defaults 改成：

- `Q1_QUEUE_SHARDS_DEFAULT = 256`
- `Q2_QUEUE_SHARDS_DEFAULT = 256`
- `q1LanePop = true`
- `NODE_C_START = 72`，也就是 `C=24/B=72`

不帶調參 flags 的確認，只使用 `--wg-timestamps-only`：

| Run | Q1/Q2 shards | 中位數 compute | 最小 compute |
| --- | ---: | ---: | ---: |
| no tuning flags | 256/256 | 0.984 ms | 0.962 ms |
| explicit `--wg-q1-lane-pop` | 256/256 | 0.984 ms | 0.962 ms |
| all tuning flags explicit | 256/256 | 0.985 ms | 0.963 ms |

有一次較早的 no-flag run 卡在約 `1.86 ms`，後續沒有重現。因為同一個 binary 立即重跑，以及完整顯式 flags 的 control run，都回到預期的 `~0.98 ms`，所以這次慢 run 先視為 anomalous run（異常測量），不當成證據。

候選預設的 shader counters：

| Metric | Value |
| --- | ---: |
| Q1 deq fail/ok | 0.44x |
| Q2 enq fail/ok | 0.45x |
| Q2 deq fail/ok | 0.27x |
| Q2 ready fail | 14K |
| Q2 high-water | 29K |

更新建議：

1. 使用 `MAX_QUEUE_SHARDS=256`，runtime default 也用 `Q1/Q2=256`。這保留最佳 default，同時不為了未來 sweep headroom 把 control block 加倍。
2. 將 Q1 lane-pop 設為 default。
3. 保留 `--wg-queue-shards`、`--wg-q2-shards` 和 `--wg-no-q1-lane-pop` 作為 ablation-study controls。Ablation study 指消融測試，也就是一次只改一個因素的對照測試。
4. 從 main optimized path 移除或隱藏 `--wg-q2-ready-first-pop` 和 `--wg-c-output-batch`；它們對排除假說有用，但不是優化候選。

## 後續：Patch 清理與測量穩定性

候選 patch 已清理成 main path 只保留有正向證據的變更：

- 保留 Q1/Q2 sharding。
- 將 Q1 lane-pop 作為 default。
- 保留 `--wg-queue-shards`、`--wg-q2-shards`、`--wg-node-c-start` 和 `--wg-no-q1-lane-pop` 作為 ablation-study controls。
- 從 optimized code path 移除已排除的實驗路徑：output-write toggle、Q2 ready-first pop、C output batching。

也比較了 `MAX_QUEUE_SHARDS=256` 和 `512`。由於 runtime default 是 `Q1/Q2=256`，把 max 提到 512 並不改善 compute time，但會讓 control block 加倍：

| MAX_QUEUE_SHARDS | Control block |
| ---: | ---: |
| 16 | 2.3 KiB |
| 256 | 36.1 KiB |
| 512 | 72.1 KiB |

更新後的 default 建議：main patch 使用 `MAX_QUEUE_SHARDS=256`。只有未來需要測超過 256 shards 時，再重新打開更高上限。

清理後候選版本確認：

| Run type | 中位數 compute | 最小 compute | Notes |
| --- | ---: | ---: | --- |
| cleaned no flags, repeat 1 | 0.985 ms | 0.966 ms | `Q1/Q2=256`, lane-pop default |
| cleaned no flags, repeat 2 | 0.983 ms | 0.966 ms | same binary |
| cleaned no flags, repeat 3 | 0.983 ms | 0.964 ms | same binary |
| cleaned no flags, repeat 4 | 0.983 ms | 0.962 ms | same binary |
| cleaned no flags, repeat 5 | 0.984 ms | 0.962 ms | same binary |
| cleaned no flags, repeat 6 | 0.984 ms | 0.961 ms | same binary |
| cleaned no flags, repeat 7 | 0.983 ms | 0.959 ms | same binary |
| cleaned no flags, repeat 8 | 0.982 ms | 0.962 ms | same binary |

測量風險：

- 少數單次 run 顯示長時間慢狀態，中位數接近 `1.86 ms`，但同一個 binary 立即重跑會回到 `~0.98 ms`。
- 上面多次短 run 中，只有大約 `1-7%` frames 慢於 `1.5 ms`。
- 目前先把 `1.86 ms` run 視為 measurement/state anomaly（測量或系統狀態異常）。在有新證據前，不要把它解釋成新的 shader bottleneck。

清理後候選版本的 counters 仍符合 queue-contention 結論：

| Metric | Value |
| --- | ---: |
| Q1 deq fail/ok | 0.42x |
| Q2 enq fail/ok | 0.45x |
| Q2 deq fail/ok | 0.28x |
| Q2 ready fail | 13K |
| Q2 high-water | 27K |

## 後續：整合回 main worktree 驗證

已將 cleaned candidate 從 `tmp/worktrees/q1-queue-shards` 整合回 main worktree，包含：

- `examples/workgraph_poc/workgraph_poc.cpp`
- `shaders/glsl/workgraph_poc/headless.comp`
- `shaders/glsl/workgraph_poc/headless.comp.spv`
- `shaders/workgraph_poc/headless.comp.spv`

主線 build 已通過：

- `cmake --build build --target workgraph_poc --config Release`

主線 clean timestamp 驗證，無調參 flags，只使用 `--wg-timestamps-only`：

| Run | 中位數 compute | P10 | P90 | 最小 compute | 慢於 1.5 ms |
| --- | ---: | ---: | ---: | ---: | ---: |
| repeat 1 | 0.9905 ms | 0.9804 ms | 1.0043 ms | 0.9668 ms | 1.0% |
| repeat 2 | 0.9915 ms | 0.9819 ms | 1.0056 ms | 0.9682 ms | 1.2% |
| repeat 3 | 0.9906 ms | 0.9810 ms | 1.0046 ms | 0.9593 ms | 1.2% |
| repeat 4 | 0.9902 ms | 0.9806 ms | 1.0042 ms | 0.9615 ms | 1.1% |

主線 shader-counter 驗證：

| Metric | Value |
| --- | ---: |
| Q1 deq fail/ok | 0.43x |
| Q2 enq fail/ok | 0.47x |
| Q2 deq fail/ok | 0.28x |
| Q2 ready fail | 17K |
| Q2 high-water | 34K |

整合後結論：

- Main worktree 的 source 與 cleaned candidate source 對齊。
- 主線 build 和 clean timestamp 都確認 `~0.99 ms` median。
- 偶爾仍會出現單次慢 run；這和前面觀察一致，先列為 measurement/state anomaly，不把它解釋成新 bottleneck。
