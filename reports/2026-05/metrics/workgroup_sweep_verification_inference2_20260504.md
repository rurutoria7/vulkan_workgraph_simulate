# Workgroup Sweep Verification - Inference 2

日期: 2026-05-04

## 目標

驗證 inference 2: 「對單一 counter 的 CAS fail，會導致有效速率下降」。

這裡的重點是固定 task count，只改 persistent compute dispatch 的 workgroup count。Koch workload 保持 `MAX_DEPTH=8`，所以每次 frame 應該都產生 `196608` edges 和 `393216` vertices。若降低 workgroup count 之後，CAS fail/success 明顯下降，而且 `compute_ms` 也下降，代表 CAS fail/retry 很可能是主要瓶頸之一。

## 實驗設定

- 固定 Koch task 數: `MAX_DEPTH=8`
- 固定 queue size 和 shader 演算法
- 改動項目: dispatch workgroup count
- 預設行為不變: `NUM_WORKGROUPS=96`, `NODE_C_START=72`
- 新增 CLI:
  - `--wg-workgroups N`: 設定 dispatch workgroup count
  - `--wg-node-c-start N`: 手動設定 Node C 起始 workgroup id
  - 如果沒有指定 `--wg-node-c-start`，host 會維持預設比例 `Node B : Node C = 72 : 24 = 3 : 1`

## 建置

```powershell
cmake --build build --target workgraph_poc --config Release --parallel 12
```

如果有改 `shaders/glsl/workgraph_poc/headless.comp`，要重建 SPIR-V 並同步 legacy mirror:

```powershell
glslangValidator -V shaders/glsl/workgraph_poc/headless.comp -o shaders/glsl/workgraph_poc/headless.comp.spv
Copy-Item -LiteralPath shaders/glsl/workgraph_poc/headless.comp.spv -Destination shaders/workgraph_poc/headless.comp.spv -Force
```

## Sweep 範例

從 repo root 執行，避免 shader path 解析錯誤。

```powershell
build/bin/Release/workgraph_poc.exe --benchmark --benchwarmup 2 --benchruntime 10 --wg-metrics-stdout --wg-metrics-interval 30 --wg-workgroups 96
build/bin/Release/workgraph_poc.exe --benchmark --benchwarmup 2 --benchruntime 10 --wg-metrics-stdout --wg-metrics-interval 30 --wg-workgroups 72
build/bin/Release/workgraph_poc.exe --benchmark --benchwarmup 2 --benchruntime 10 --wg-metrics-stdout --wg-metrics-interval 30 --wg-workgroups 48
build/bin/Release/workgraph_poc.exe --benchmark --benchwarmup 2 --benchruntime 10 --wg-metrics-stdout --wg-metrics-interval 30 --wg-workgroups 32
build/bin/Release/workgraph_poc.exe --benchmark --benchwarmup 2 --benchruntime 10 --wg-metrics-stdout --wg-metrics-interval 30 --wg-workgroups 24
```

若要固定 Node B/Node C 分配，可以加上 `--wg-node-c-start N`。例如 `--wg-workgroups 48 --wg-node-c-start 36` 表示 36 個 Node B workgroups、12 個 Node C workgroups。

## CSV-friendly 輸出

加上 `--wg-metrics-stdout` 後，stdout 會輸出:

```text
WG_METRICS_HEADER frame,gpu_frame_ms,reset_ms,reset_barrier_ms,compute_ms,...
WG_METRICS 1,48.1234,0.0080,0.0074,48.0500,...
```

匯入試算表時可以把 `WG_METRICS_HEADER ` 和 `WG_METRICS ` 前綴移除，後面就是逗號分隔欄位。

## 主要觀察欄位

- `compute_ms`: compute dispatch 的 GPU timestamp 時間
- `q1_deq_cas_fail_per_ok`: `q1_deq_cas_fail / q1_deq_ok`
- `q2_deq_cas_fail_per_ok`: `q2_deq_cas_fail / q2_deq_ok`
- `q2_deq_empty_per_ok`: `q2_deq_empty / q2_deq_ok`
- `edges`, `vertices`: 正確性檢查，應接近或等於 `196608`, `393216`
- `wg_workgroups`, `node_c_start`: 紀錄實際 workgroup 配置

## 判讀方式

支持 inference 2 的結果:

1. workgroup count 下降時，`q1_deq_cas_fail_per_ok` 或 `q2_deq_cas_fail_per_ok` 明顯下降。
2. 同時 `compute_ms` 也下降。
3. `edges` 和 `vertices` 保持正確，表示不是因為少做 task 才變快。

如果 CAS fail ratio 下降但 `compute_ms` 沒有改善，就不能直接說 CAS retry 是主因，可能還有其他瓶頸，例如 Q2 empty polling、Node B/Node C 分配不平衡、memory bandwidth，或 metrics/timestamp overhead。

這個實驗屬於 ablation study（消融實驗）的一種：固定 task 數和演算法，只改 worker 數量來降低 contention（競爭/搶同一個 counter），觀察效能是否同步改善。
