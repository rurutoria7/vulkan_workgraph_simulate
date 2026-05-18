# WorkGraph Vulkan POC Metrics Report - 2026-05-04

本報告紀錄目前工作樹版本的 `workgraph_poc` 執行結果。時間：2026-05-04 15:57:03 +08:00。

## 執行條件

- Build target: `workgraph_poc`
- Build config: `Release`
- Build command: `cmake --build build --target workgraph_poc --config Release --parallel 12`
- Run mode: `--benchmark --benchwarmup 2 --benchruntime 10 --wg-metrics-stdout --wg-metrics-interval 30`
- GPU: AMD Radeon RX 7900 XTX
- Driver version: `8388996`
- Metrics sample count: 8 筆，每 30 個 completed GPU frame 輸出一次

原始資料：

- `workgraph_metrics_20260504_10s.csv`: 轉換後的 `WG_METRICS` CSV
- `workgraph_metrics_20260504_10s_stdout.txt`: 原始 stdout
- `workgraph_benchmark_20260504_10s.csv`: benchmark（基準測試）frame-time CSV

數字格式：下方統計數字使用 scientific notation（科學記號，例如 `4.64192e1 = 46.4192`）。原始 CSV 保留十進位格式，方便重新計算。

## GPU timestamp summary

| Metric（度量數據） | Avg | Min | Median | Max | 說明 |
|---|---:|---:|---:|---:|---|
| GPU frame ms | 4.64192e1 | 4.49856e1 | 4.62364e1 | 4.95977e1 | reset + compute + metrics copy + render |
| Compute ms | 4.62947e1 | 4.49209e1 | 4.61712e1 | 4.95320e1 | 主要耗時幾乎都在 compute dispatch |
| Metrics copy ms | 8.14e-2 | 1.78e-2 | 2.21e-2 | 4.042e-1 | readback copy 成本很小，但偶爾有 spike |
| Render ms | 2.77e-2 | 2.62e-2 | 2.77e-2 | 2.85e-2 | render pass 幾乎不是瓶頸 |
| Edges / ms | 4.2505245e3 | 3.9693127e3 | 4.2582605e3 | 4.3767578e3 | `totalProcessed / compute_ms` |
| Vertices / ms | 8.5010490e3 | 7.9386255e3 | 8.5165210e3 | 8.7535156e3 | `vertexCount / compute_ms` |

## Correctness counters

每筆 sample 都維持相同結果：

- `edges = 1.96608e5`
- `vertices = 3.93216e5`
- `node_a_seed = 3.0e0`
- `node_b_tasks = 2.62143e5`
- `node_b_subdivide = 6.5535e4`
- `node_b_final = 1.96608e5`
- `node_c_output = 1.96608e5`
- `stop_writes = 1.0e0`
- `q1_enq_full = 0.0e0`, `q2_enq_full = 0.0e0`

這代表目前的 deterministic Koch workload 在 10 秒 benchmark 中沒有掉 task，也沒有 queue full。

## Queue / atomic contention

| Counter | Avg | Min | Median | Max | 觀察 |
|---|---:|---:|---:|---:|---|
| `q1_enq_cas_fail` | 3.147754e6 | 2.970769e6 | 3.106510e6 | 3.457616e6 | Q1 producer tail 競爭明顯 |
| `q1_deq_cas_fail` | 3.9997561e7 | 3.8719617e7 | 3.9467665e7 | 4.2723393e7 | 最大熱點，約 1.5258e2 fails / successful dequeue |
| `q2_enq_cas_fail` | 5.12024e5 | 4.73122e5 | 5.21413e5 | 5.37057e5 | Q2 enqueue 競爭較低 |
| `q2_deq_cas_fail` | 1.407294e7 | 1.3867168e7 | 1.4040592e7 | 1.4385344e7 | 仍有大量 dequeue CAS 競爭 |
| `q1_high_water` | 9.7039e4 | 9.0999e4 | 9.6413e4 | 1.11227e5 | 平均約 queue capacity 的 4.936e1%，最高 5.657e1% |
| `q2_high_water` | 1.69375e2 | 8.4e1 | 1.71e2 | 2.38e2 | Q2 occupancy 很低，Node C 常在等工作 |
| `q1_ready_max_spin` | 3.875e0 | 1.0e0 | 2.5e0 | 1.1e1 | ready flag spin 不高 |
| `q2_ready_max_spin` | 1.7875e1 | 1.6e1 | 1.75e1 | 2.4e1 | ready flag spin 有但不是主要瓶頸 |

比例觀察：

- Q1 enqueue CAS fail / success: 1.201e1
- Q1 dequeue CAS fail / success: 1.5258e2
- Q1 dequeue empty / success: 1.5e-1
- Q2 enqueue CAS fail / success: 2.60e0
- Q2 dequeue CAS fail / success: 7.158e1
- Q2 dequeue empty / success: 1.1917e2

## Benchmark frame-time summary

Benchmark CSV 摘要：

- Runtime: 1.00631079e4 ms
- Frames: 2.10e2
- FPS: 2.08683e1
- Avg CPU-side frame time: 4.79196e1 ms
- P50: 4.73208e1 ms
- P90: 9.49696e1 ms
- P95 lower-rank: 9.62267e1 ms
- P95 nearest-rank: 9.66476e1 ms
- P95 linear interpolation: about 9.64582e1 ms
- Min: 1.048e-1 ms
- Max: 1.014781e2 ms
- `< 1 ms` frames: 4.5e1
- `> 80 ms` frames: 4.7e1

注意：benchmark frame time 是 CPU-side render loop 測到的時間，受到 fence / swapchain pacing 影響。這批資料主要分成三群：`< 1 ms` 有 4.5e1 筆、`40-60 ms` 有 1.18e2 筆、`> 80 ms` 有 4.7e1 筆；`1-40 ms` 與 `60-80 ms` 都是 0.0e0 筆。若要看 compute workload 本身，GPU timestamp 的 `compute_ms` 較可信。

## 結論

1. 正確性穩定：每次 sample 都產生 `1.96608e5` edges 與 `3.93216e5` vertices。
2. Compute 是主耗時：平均 `compute_ms = 4.62947e1 ms`，render 平均只有 `2.77e-2 ms`。
3. 主要瓶頸在 queue atomic：Q1 dequeue CAS fail 平均約 `4.00e7`，是最明顯的 contention source。
4. Q2 occupancy 很低：`q2_high_water` 平均只有 `1.69375e2`，但 Q2 dequeue empty / success 約 `1.1917e2`，代表 Node C worker 常空轉。
5. Q1/Q2 OK 數量平衡：Q1 每筆 sample 都是 `enq_ok = deq_ok = 262143`，Q2 每筆 sample 都是 `enq_ok = deq_ok = 196608`；瓶頸不是 task 數量不對稱，而是 CAS 競爭與 empty polling。
6. 下一輪 ablation study（消融研究，意思是一次只改一個因素來確認影響）建議優先測：降低 Node C worker 數、減少總 workgroups、per-workgroup/local queue batching、或把 Q1/Q2 消費者角色更明確分配。
