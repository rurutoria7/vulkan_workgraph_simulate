# Q1 sharding + B/C 配比交叉實驗

日期：2026-05-10

## 目的

上一輪找到兩個獨立線索：

- B/C 配比：`C=40/B=56` 比原本 `C=24/B=72` 快約 8.5%。
- Q1 sharding：單獨掃 Q1 shards 時，`16 shards` 明顯降低 compute time。

本輪測試兩者能不能疊加。

## 結果

Clean timestamp，shader counters 關閉。

| Q1 shards | C workgroups | B workgroups | 平均 compute |
| ---: | ---: | ---: | ---: |
| 4 | 24 | 72 | 28.04 ms |
| 4 | 32 | 64 | 22.93 ms |
| 4 | 40 | 56 | 23.24 ms |
| 4 | 48 | 48 | 24.25 ms |
| 4 | 56 | 40 | 21.09 ms |
| 8 | 24 | 72 | 25.17 ms |
| 8 | 32 | 64 | 18.66 ms |
| 8 | 40 | 56 | 17.90 ms |
| 8 | 48 | 48 | 19.53 ms |
| 8 | 56 | 40 | 21.12 ms |
| 16 | 24 | 72 | 22.96 ms |
| 16 | 32 | 64 | 15.75 ms |
| 16 | 40 | 56 | 14.05 ms |
| 16 | 48 | 48 | 15.46 ms |
| 16 | 56 | 40 | 16.79 ms |

Refine sweep for `Q1 shards=16`:

| C workgroups | B workgroups | 平均 compute |
| ---: | ---: | ---: |
| 36 | 60 | 13.95 ms |
| 38 | 58 | 13.70 ms |
| 40 | 56 | 14.08 ms |
| 42 | 54 | 14.21 ms |
| 44 | 52 | 14.48 ms |

目前最佳：`Q1 shards=16, C=38/B=58`，平均 compute 約 `13.70 ms`。

## 與先前結果比較

- 原本 Q1 shard branch 的 `Q1 shards=1, C=24/B=72` 約 `41.41 ms`。
- 單獨 `Q1 shards=16, C=24/B=72` 約 `22.96 ms`。
- 單獨 B/C 配比優化（非 Q1 shard branch）最佳約 `27.97 ms`。
- 組合後 `Q1 shards=16, C=38/B=58` 約 `13.70 ms`。

結論：Q1 sharding 和 B/C 配比優化可以疊加，而且疊加後是目前最強的候選優化。

## Counter 結構

Shader counters 會擾動 duration，所以只用來看壓力結構。

| Metric | Q1s16 C24 | Q1s16 C38 |
| --- | ---: | ---: |
| Q1 enqueue fail / ok | 1.29x | 1.26x |
| Q1 dequeue fail / ok | 36.14x | 35.89x |
| Q1 dequeue empty | 314M | 77M |
| Q2 enqueue fail / ok | 103.14x | 47.80x |
| Q2 dequeue fail / ok | 328.63x | 255.85x |
| Q2 high-water | 161k | 53k |

解讀：

- Q1 sharding 已經把 Q1 enqueue contention 從百倍量級壓到約 1.3x。
- 調整 B/C 配比後，Q2 backlog 顯著下降，但 Q2 global queue 仍是主要壓力。
- 剩餘瓶頸不像 profiler 問題，而是 queue atomic contention：尤其是 Q2 enqueue/dequeue，以及 B 端 polling Q1 shards 的空轉。

## 下一步

優先方向：

1. 將 `Q1 shards=16` 與 `C=38/B=58` 整理成可採用的模式。
2. 做 Q2 sharding，而不是 naive Q2 batch dequeue。
3. 檢查 Q1 dequeue polling 策略，避免 B workers 對空 shard 做過多 CAS / empty probe。
