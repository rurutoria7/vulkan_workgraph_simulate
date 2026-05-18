# Bottleneck investigation notes

日期：2026-05-10

## 目前瓶頸分類

瓶頸分類（taxonomy）是把可能原因分組，避免把所有現象都硬塞到單一解釋。

1. Profiler / measurement：RGP Wavefront occupancy 目前不能當作完整 compute dispatch lifetime。
2. Atomic contention：queue `count/head/tail/ready` 的 atomic CAS 失敗和 ready-spin。
3. Work distribution：Node B subdivider 和 Node C writer 的 workgroup 配比。
4. Queue protocol：目前 producer 先增加 `count`，再寫 payload / ready flag；consumer 可能先 claim 到尚未 ready 的 slot。

## Profiler 結論

極簡 ALU-only shader 無 queue、無 atomic，clean timestamp 約 29.44 ms，RGP event timing 約 38.80 ms，但 Wavefront occupancy 仍只顯示前段。因此 RGP occupancy 的「中後段 0 wavefront」不能作為瓶頸判斷依據。

可用的時間來源目前以 app GPU timestamp 為主；RGP event timing 可輔助看 event 結構，但不要用 Wavefront occupancy 判斷 dispatch 是否仍有 active wavefront。

## Atomic counter 觀察

Shader counters 會擾動時間，所以只用來看結構，不用來比較 duration。

原始 batch 設定（Q1 batch on, Q2 scalar, C=24/B=72）主要壓力：

- Q1 enqueue CAS fail / ok 約 130x。
- Q2 enqueue CAS fail / ok 約 68x。
- Q2 dequeue CAS fail / ok 約 147x。
- Q2 high-water 約 50k，代表 writer 追不上 producer，Q2 有明顯堆積。

這表示目前不是單一 atomic，而是 B/C 配比和 global Q1/Q2 atomics 共同造成壓力。

## Q2 batch dequeue 消融實驗

消融實驗（ablation study）是一次只改一個因素，觀察結果是否支持假說。

假說：Q2 dequeue CAS fail 很高，所以把 Q2 dequeue 改成 subgroup batch claim 會變快。

結果：假說不成立。

| 模式 | Clean timestamp 平均 compute |
| --- | ---: |
| Q2 scalar baseline | 30.13 ms |
| Q2 batch limit 1 | 40.33 ms |
| Q2 batch limit 2 | 38.02 ms |
| Q2 batch limit 4 | 35.99 ms |
| Q2 batch limit 8 | 33.91 ms |
| Q2 batch limit 16 | 32.87 ms |
| Q2 batch limit 32 | 33.03 ms |

Counters 顯示 Q2 batch 雖然降低 dequeue CAS fail，但把 Q2 ready-spin 放大很多。原因可能是目前 queue protocol 讓 `count` 先對 consumer 可見，payload / ready flag 還沒 publish 完，batch consumer 一次 claim 太多 slot 後在 ready flag 上空轉。

結論：不要採用直接 Q2 batch dequeue；若要優化 Q2，需要先改 queue publish protocol 或 shard Q2，而不是只在 consumer 端 batch。

## B/C workgroup 配比 sweep

保持總 workgroups = 96，Q1 batch on，Q2 scalar，改變 Node C writer 數量。

| C workgroups | B workgroups | Clean timestamp 平均 compute | vs C=24 |
| ---: | ---: | ---: | ---: |
| 4 | 92 | 62.31 ms | 0.49x |
| 8 | 88 | 43.03 ms | 0.71x |
| 12 | 84 | 36.54 ms | 0.83x |
| 16 | 80 | 33.00 ms | 0.92x |
| 20 | 76 | 31.53 ms | 0.96x |
| 24 | 72 | 30.35 ms | 1.00x |
| 28 | 68 | 29.61 ms | 1.03x |
| 32 | 64 | 28.44 ms | 1.07x |
| 40 | 56 | 27.97 ms | 1.09x |
| 48 | 48 | 28.59 ms | 1.06x |
| 56 | 40 | 29.21 ms | 1.04x |
| 64 | 32 | 30.25 ms | 1.00x |
| 72 | 24 | 32.43 ms | 0.94x |
| 80 | 16 | 36.55 ms | 0.83x |

最佳點在 C=40 / B=56，約 27.97 ms，比 C=24 / B=72 快約 8.5%。

Counters 對比：

| Metric | C=24/B=72 | C=40/B=56 |
| --- | ---: | ---: |
| Q1 enqueue fail / ok | 130.5x | 110.0x |
| Q2 enqueue fail / ok | 67.8x | 31.8x |
| Q2 dequeue fail / ok | 147.1x | 128.4x |
| Q2 high-water | 50,403 | 9,214 |

結論：C=24 writer 不夠，Q2 堆積造成 producer 端 enqueue 競爭和整體延遲。增加 C 到 40 能降低 Q2 backlog 和 Q2 enqueue contention，但剩餘 Q2 dequeue CAS fail 仍高。

## 目前建議

短期可採用：

- 將 Node C writer 數量調到 40（`nodeCStart = 56`），這是目前 clean timestamp 最穩定的直接優化。
- 保留 Q1 dequeue batch。
- 不採用 naive Q2 dequeue batch。

下一輪優化方向：

- Q1 sharding：Q1 enqueue CAS fail 仍約 110x，是下一個主要 atomic 熱點。
- Q2 queue protocol：把「slot ready」和「queue count 可見」解耦，避免 consumer claim 尚未 ready 的 slot。
- Q2 sharding：降低 global `q2.count/head/tail` 的單點 contention。
