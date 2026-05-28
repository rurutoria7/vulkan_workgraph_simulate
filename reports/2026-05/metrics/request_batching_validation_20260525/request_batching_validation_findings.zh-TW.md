# Request batching 驗證結果

日期：2026-05-25

分支：`experiment-request-batching-validation`

## Observation：數據看到什麼

- 原始資料：132 個 raw CSV、`run_manifest.csv`、`request_batching_validation_summary.csv`。
- 方法：每個 case 跑 3 次 repeat，每次 150 個 benchmark frames，summary 丟掉每次前 30 個 recorded frames。
- Timing run 使用 `--wg-timestamps-only`；counter run 使用 `--wg-metrics`。
- Correctness 規則：counter run 必須符合 `edges=196608`、`vertices=393216`、`stop_writes=1`。process failed 的 run 不算成功優化。
- 重要限制：這次 256-shard off baseline 的 median 是 `1.943 ms`，不是歷史上的 `~0.99 ms`。所以本報告只拿同一個 binary 的 batching on/off 做相對判斷，不更新 main path 的最佳絕對數字。

### 舊 unsharded branch 重現

Q2 比較 baseline 是舊版 `q1_on_q2_off`。

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

舊版 Q2 batching 重現了主要型態：Q2 dequeue CAS fail per ok 大幅下降，從 `145.3` 降到 limit 32 的 `1.25`，但 compute median 仍然變差。最佳 Q2 batch case 仍比 Q2 off 慢 `+8.9%`。

### Main sharded，16-shard 高 contention case

Flags：`--wg-queue-shards 16 --wg-q2-shards 16 --wg-q1-lane-pop --wg-node-c-start 72`。

| case | median ms | p90 ms | median delta | p90 delta | correctness |
|---|---:|---:|---:|---:|---|
| Q2 batch off | 4.244 | 4.906 | 0.0% | 0.0% | ok |
| limit 1 | 7.369 | 7.498 | +73.6% | +52.8% | ok |
| limit 2 | 5.128 | 5.277 | +20.8% | +7.6% | ok |
| limit 4 | 4.255 | 4.673 | +0.3% | -4.7% | ok |
| limit 8 | 4.174 | 5.102 | -1.7% | +4.0% | ok |
| limit 16 | 4.262 | 5.390 | +0.4% | +9.9% | ok |
| limit 32 | 4.117 | 5.263 | -3.0% | +7.3% | ok |

Q2 batching 把 Q2 dequeue CAS fail per ok 從 `12.16` 降到大約 `0.26-0.47`，但沒有產生至少 10% 的 median 改善。median 有改善的兩個 case，limit 8 和 32，p90 都變差。

### Main sharded，256-shard adopted default case

Flags：`--wg-queue-shards 256 --wg-q2-shards 256 --wg-q1-lane-pop --wg-node-c-start 72`。

| case | median ms | p90 ms | median delta | p90 delta | correctness |
|---|---:|---:|---:|---:|---|
| Q2 batch off | 1.943 | 1.969 | 0.0% | 0.0% | ok |
| limit 1 | n/a | n/a | n/a | n/a | failed all repeats |
| limit 2 | n/a | n/a | n/a | n/a | failed all repeats |
| limit 4 | n/a | n/a | n/a | n/a | failed all repeats |
| limit 8 | 11.711 | 11.755 | +502.7% | +497.0% | ok |
| limit 16 | 11.428 | 11.466 | +488.1% | +482.3% | ok |
| limit 32 | 11.278 | 11.303 | +480.4% | +474.0% | ok |

limit 1/2/4 在所有 timing 和 counter repeat 都以 code `-1` 結束。limit 8/16/32 correctness 正常，但約比 batching off 慢 5.8 倍。

counter 型態是主要反證：Q2 dequeue CAS fail per ok 從 `0.275` 降到約 `0.031`，但 Q2 empty dequeue per ok 從 `5.83` 暴增到約 `116`。Batch slots per batch 約只有 `0.26`，代表多數 batch probe 是 empty 或 underfilled。

## Hypothesis：可能原因

- 舊 unsharded Q2 batching 會降低 CAS contention，但成本轉移到 empty polling、partial batch、batch coordination。即使 CAS counter 改善，end-to-end compute time 仍變差。
- 在 16-shard stress case，contention 足夠高，所以 batching 可以降低 CAS traffic；但收益太小，tail latency 也常變差。這表示 batching overhead、ready 行為、empty polling 抵消了 atomic 減量。
- 在 256-shard path，簡單 subgroup batching 不適合。Shard 太多，workgroup-local batch probe 很難穩定填滿，所以 shader 花太多時間掃 empty 或 underfilled shard batch。

## Conclusion：可以確認什麼，不能確認什麼

- 舊資料的關鍵結論可重現：Q2 batching 可以降低 CAS failures，但不能降低 compute time。
- 對目前 256-shard main path，`request batching not working` 仍明確成立。小 limit 會 failed；較大 limit 會大幅變慢。
- 對 16-shard high-contention case，batching 不是成功優化。它可能有低 shard 診斷價值，因為會降低 CAS traffic；但不符合至少 10% median 改善且 p90 不變差的採用標準。
- 這個結果不能證明所有 wave-local architecture 都不好。它只否定這種 request/dequeue batching 形狀。

## Recommendation：是否值得納入下一步主線

- 不要把 Q2 request/dequeue batching 納入 main optimized path。
- 在做不同 wave-local architecture 之前，不值得再花時間做小幅 batching tweak。失敗模式是結構性的：empty polling、partial batch、underfilled batch 佔主導。
- 保留這份資料作為 negative controlled experiment：`batching reduces some atomic counters, but does not improve end-to-end compute under the accepted criteria`。
