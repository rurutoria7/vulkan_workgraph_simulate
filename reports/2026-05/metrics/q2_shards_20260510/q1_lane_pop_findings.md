# Q1 lane-pop 實驗結果

日期：2026-05-10

## 問題

在 Q1/Q2 sharding（分片）之後，剩下的 counters（計數器）仍然指向 Q1 dequeue polling / CAS contention，也就是 Q1 出隊端的輪詢與 CAS 競爭。

舊的 Q1 pop 策略用 `wgId % Q1_QUEUE_SHARDS` 選擇起始 shard，所以同一個 workgroup 裡的所有 lane 都會從同一個 Q1 shard 開始掃。這個實驗要確認：如果把同一個 workgroup 裡的 lane 分散到不同 Q1 shard 開始 pop，是否能降低 dequeue contention。

這是 ablation study（消融測試，一次只改一個因素的對照實驗）：每次比較只改 Q1 pop 的起始 shard 策略，Q1/Q2 shard 數量與 B/C workgroup 比例都固定。

## 測試變更

在 `q1-queue-shards` worktree 新增 flag：

- `--wg-q1-lane-pop`：Q1 pop 從 `(wgId * local_size_x + localId) % Q1_QUEUE_SHARDS` 開始。
- `--wg-no-q1-lane-pop`：舊行為，Q1 pop 從 `wgId % Q1_QUEUE_SHARDS` 開始。

Shader counters 不當作 clean duration。乾淨時間只使用 `--wg-timestamps-only`。

## 時間結果

固定 baseline：`Q1 shards=16`、`Q2 shards=16`、`C=16/B=80`。

| 模式 | 平均 compute | 中位數 compute | 最小 compute |
| --- | ---: | ---: | ---: |
| Q1 lane-pop off | 6.01 ms | 5.60 ms | 5.54 ms |
| Q1 lane-pop on | 4.39 ms | 4.00 ms | 3.90 ms |

開啟 lane-pop 後做較長 sweep，固定 `Q1 shards=16`、`Q2 shards=16`：

| C workgroups | B workgroups | 平均 compute | 中位數 compute | 最小 compute |
| ---: | ---: | ---: | ---: | ---: |
| 16 | 80 | 4.00 ms | 3.99 ms | 3.79 ms |
| 20 | 76 | 3.92 ms | 3.88 ms | 3.76 ms |
| 24 | 72 | 3.85 ms | 3.84 ms | 3.73 ms |
| 32 | 64 | 3.88 ms | 3.87 ms | 3.79 ms |

目前最佳測量組合：

- `Q1 shards=16`
- `Q2 shards=16`
- `--wg-q1-lane-pop`
- `C=24/B=72`，也就是 `--wg-node-c-start 72`
- Clean timestamp：約 `3.85 ms` 平均、`3.84 ms` 中位數

相較前一輪最佳組合 `Q1 shards=16`、`Q2 shards=16`、`C=16/B=80`、沒有 lane-pop，約 `5.57 ms`，這輪約快 `1.45x`。相較早期 Q1 shard branch baseline `Q1 shards=1`、`C=24/B=72`、約 `41.41 ms`，總改善約 `10.75x`。

## Counter 證據

Counters 是另外用 shader metrics enabled 收集，所以表格中的 timing 不是 clean duration。

| Case | Q1 deq fail/ok | Q1 deq empty | Q1 enq fail/ok | Q2 enq fail/ok | Q2 deq fail/ok | Q2 high-water |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| C16, lane-pop off | 60.54x | 0.96M | 1.89x | 1.74x | 5.46x | 1,493 |
| C16, lane-pop on | 15.38x | 9.58M | 6.46x | 6.89x | 7.82x | 65,915 |
| C24, lane-pop on | 12.98x | 4.67M | 6.04x | 7.11x | 11.51x | 35,865 |

解讀：

- 預期效果成立：lane-pop 大幅降低 Q1 dequeue CAS contention。
- lane-pop 也讓 B workers 消耗與生產得更快。當 C writers 太少時，例如 `C=16`，Q2 backlog 會變大。
- 重新平衡到 `C=24` 後，Q2 backlog 降低，也得到目前最好的 clean timestamp。
- 剩下的瓶頸不再是單一 global Q2 queue。現在比較像是更快的 Q1 consumption、Q2 publish/ready 行為，以及 C writers 是否足夠 drain Q2 之間的平衡問題。

## 已排除假說

- 「Q1 dequeue contention 已經被 Q1 sharding 解決。」不完全正確。同樣 Q1 shard 數下，lane-distributed pop 明顯更快。
- 「lane-pop 可以取代高 shard 數。」不支持。開啟 lane-pop 且 `C=24/Q2s16` 時，Q1 shards 仍然強烈影響時間：Q1s4 約 `12.09 ms`，Q1s8 約 `6.38 ms`，Q1s16 約 `3.94 ms`。
- 「lane-pop 可以取代 Q2 sharding。」不支持。開啟 lane-pop 且 `Q1s16/C24` 時，Q2s4 約 `7.65 ms`，Q2s8 約 `4.80 ms`，Q2s16 約 `3.96 ms`。
- 這裡仍然不把 RGP wavefront occupancy 當成因果證據；前面的 probes 顯示，即使是人工 ALU tail，RGP 也可能只顯示 early-only occupancy。

## 下一步

1. 把 Q1 sharding + Q2 sharding + Q1 lane-pop 當作目前主要優化組合。
2. 新增更清楚的 queue-pressure metric，或是 Q2 backlog 的 per-shard histogram，因為 total high-water 現在和 duration 的關係不再那麼直接。
3. 接著調查 Q2 ready-spin / publish ordering。目前 ready flag protocol 可能讓 consumer 先看到已 claim 的 slot，但 payload 還沒有 ready，這可能解釋 sharding 後仍有 ready wait。
4. 如果 counters 之後仍顯示過多 empty/probe work，再測 limited Q1 pop probing。
