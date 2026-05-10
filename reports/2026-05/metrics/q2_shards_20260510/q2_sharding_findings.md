# Q2 sharding findings

日期：2026-05-10

## 目的

前一輪最佳組合是 `Q1 shards=16, C=38/B=58`，clean timestamp 約 `13.70 ms`。Shader counters 顯示剩餘壓力集中在 Q2 global queue：

- Q2 enqueue CAS fail / ok 約 47.8x。
- Q2 dequeue CAS fail / ok 約 255.9x。
- Q2 high-water 約 53k。

因此這輪實作 `--wg-q2-shards N`，把 Q2 的 `count/head/tail` 和 metrics 也切成 shard（分片）。

## Q2 shards sweep

固定 `Q1 shards=16, C=38/B=58`，clean timestamp，shader counters 關閉。

| Q2 shards | 平均 compute | vs Q2 shards=1 |
| ---: | ---: | ---: |
| 1 | 16.95 ms | 1.00x |
| 2 | 10.24 ms | 1.66x |
| 4 | 8.38 ms | 2.02x |
| 8 | 6.87 ms | 2.47x |
| 16 | 6.08 ms | 2.79x |

注意：加入 Q2 shard array 後，`Q2 shards=1` 的 control layout 已不同於前一輪舊版單 Q2 queue，所以它是同一版程式內的 baseline，不應直接等同前一輪 `13.70 ms`。

## B/C 配比重新掃描

Q2 sharding 後，最佳 writer 數量變少，因為 Q2 dequeue/enqueue contention 大幅下降。

固定 `Q1 shards=16, Q2 shards=16`。

| C workgroups | B workgroups | 平均 compute |
| ---: | ---: | ---: |
| 4 | 92 | 6.35 ms |
| 8 | 88 | 5.81 ms |
| 12 | 84 | 5.62 ms |
| 16 | 80 | 5.57 ms |
| 20 | 76 | 5.59 ms |
| 24 | 72 | 5.66 ms |
| 32 | 64 | 5.89 ms |
| 38 | 58 | 6.09 ms |
| 40 | 56 | 6.17 ms |
| 48 | 48 | 6.71 ms |
| 56 | 40 | 7.36 ms |
| 64 | 32 | 8.42 ms |

目前最佳：`Q1 shards=16, Q2 shards=16, C=16/B=80`，平均 compute 約 `5.57 ms`。

相較前一輪最佳 `Q1 shards=16, Q2 unsharded, C=38/B=58` 約 `13.70 ms`，約 `2.46x` improvement。相較最早 Q1 shard branch 的 `Q1 shards=1, C=24/B=72` 約 `41.41 ms`，約 `7.43x` improvement。

## Counter 結構

Shader counters 會擾動 duration，所以只用來看結構。

| Metric | Q1s16 Q2s1 C38 | Q1s16 Q2s16 C16 |
| --- | ---: | ---: |
| Q1 enqueue fail / ok | 1.19x | 1.90x |
| Q1 dequeue fail / ok | 35.81x | 60.76x |
| Q1 dequeue empty | 104M | 0.83M |
| Q2 enqueue fail / ok | 55.34x | 1.77x |
| Q2 dequeue fail / ok | 294.45x | 5.50x |
| Q2 dequeue empty | 3.15M | 6.12M |
| Q2 ready fail | 17k | 193k |
| Q2 high-water | 68k | 1.5k |

解讀：

- Q2 sharding 幾乎消掉 Q2 global queue contention。
- Q2 high-water 從數萬降到約 1.5k，writer 不再嚴重落後。
- 最佳 C 數量從 38-40 降到 16，因為 Q2 shard 後 writer 端不需要那麼多 workgroups 才能追上。
- 剩餘壓力主要是 Q1 dequeue polling / CAS fail，以及 Q2 ready-spin。這些是下一輪 queue protocol / polling 策略的目標。

## 建議

目前最有價值的候選優化：

- `Q1 shards=16`
- `Q2 shards=16`
- `C=16/B=80`，也就是 `nodeCStart=80`

下一步：

1. 將 Q2 sharding 整理成可維護的主線 patch。
2. 針對 Q1 dequeue polling 做優化，避免所有 B workers 對多個空 shard 做大量 probes。
3. 檢查 queue publish protocol，降低 `count` 先可見造成的 ready-spin。
