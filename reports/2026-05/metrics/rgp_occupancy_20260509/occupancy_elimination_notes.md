# RGP wavefront occupancy 排除紀錄

日期：2026-05-09

## 問題

Batch dequeue 版本在 RGP 的 Wavefront occupancy 圖中，CS wavefront 只出現在前段；中後段 event / dispatch bar 還在，但 RGP Details 顯示 total wavefronts / total threads 為 0。原始 counter trace 同時看到 cache request，造成「0 occupancy 但還有 memory R/W」的矛盾。

## 重要觀察

- App 端 GPU timestamp 的 compute 區間是 `TimestampAfterResetBarrier -> TimestampAfterCompute`，也就是 compute dispatch 本身，不包含 render。
- `--wg-timestamps-only` 會保留 GPU timestamp，但關閉 shader-side metrics counter（統計用 atomic）。
- RDP/RDS connection 沒接上時不會產生新 `.rgp`；本輪有效 trace 都有新的檔名、時間戳與檔案大小。

## 消融實驗

消融實驗（ablation study）是一次只移除或改變一個因素，用來排除假說。

| 實驗 | 證據 | 結果 |
| --- | --- | --- |
| Wavefront-only RGP：關閉 counters / instruction tracing / shader instrumentation | `wavefront_only_occupancy_rgp.png`, `wavefront_only_event_timing_rgp.png` | Dispatch 仍約 38.7 ms，occupancy 仍只在前段。排除 profiler counter / instruction tracing 干擾是主因。 |
| 移除 output vertex buffer writes | `batch_no_output_writes_timestamps.csv`, `no_output_writes_occupancy_rgp.png` | App timestamp 仍約 30 ms，RGP occupancy 形狀仍相同。排除 output buffer 寫入 / drain 是主因。 |
| Scalar dequeue 對照 | `scalar_wavefront_only_occupancy_rgp.png`, `scalar_wavefront_only_event_timing_rgp.png` | Scalar 也出現前段 occupancy、中後段空白；dispatch 更長。排除 batch dequeue 專屬 bug。 |
| Depth 7 workload scaling | `depth7_batch_timestamps.csv`, `depth7_batch_wavefront_only_event_timing_rgp.png`, `depth7_batch_wavefront_only_occupancy_rgp.png` | Clean timestamp 約 7.63 ms，RGP dispatch 約 10.28 ms，隨 workload 明顯縮短；但 occupancy 仍只在前段。排除 dispatch duration 完全是假時間或固定 overhead。 |
| 人工 ALU tail：stopFlag 後加 20M 次有輸出副作用的純運算 loop | `batch_debug_alu_tail_20m_timestamps.csv`, `debug_alu_tail_20m_wavefront_only_event_timing_rgp.png`, `debug_alu_tail_20m_wavefront_only_occupancy_rgp.png` | Clean timestamp 從約 30 ms 增加到約 45 ms，RGP dispatch 約 59.6 ms；但 occupancy 仍只在前段。這表示 RGP occupancy 沒有顯示人工加長的 shader 執行尾段。 |
| 極簡 ALU-only shader：無 queue、無 atomic，只做固定長度運算 | `alu_only_probe.comp`, `alu_only_probe_timestamps.csv`, `alu_only_probe_wavefront_only_event_timing_rgp.png`, `alu_only_probe_wavefront_only_occupancy_rgp.png` | Clean timestamp 約 29.44 ms，RGP dispatch 約 38.80 ms；但 occupancy 仍只在前段。這把疑點從 persistent queue / atomic pattern 進一步移到 RGP wavefront occupancy 視圖或 capture 限制。 |

## 已排除

- 不是單純 RDP/RDS 沒連上或誤開舊 trace：有效 trace 均為新產生。
- 不是 UI scale 或只看錯區間：RGP Details 的 selected range 顯示 total wavefronts / threads 真的為 0。
- 不是 counters / instruction tracing / shader instrumentation 造成的主要現象。
- 不是 output vertex buffer writes 造成的長尾。
- 不是 batch dequeue 專屬現象，scalar dequeue 也有同型態。
- 不是 workload 沒有縮放；depth7 的 timestamp 與 RGP event duration 都明顯縮短。
- 不是「wavefront 真的在前段完全結束」的簡單解釋：人工 ALU tail 明確增加 compute timestamp，但 RGP occupancy 仍沒有顯示尾段 wavefront。
- 不是 persistent queue / atomic 特有現象：極簡 ALU-only shader 也出現相同的 event duration 與 occupancy 不一致。

## 目前合理解釋

目前資料最支持的解釋是：RGP 的 Wavefront occupancy 視圖在這組 capture 設定 / app 形態下對 compute dispatch 的 wavefront lifetime 可觀測性不足，至少不能把它當成完整 dispatch lifetime。RGP event timing 與 app GPU timestamp 都能看到 workload scaling、人工 ALU tail、以及 ALU-only shader 的長 dispatch；但 Wavefront occupancy 只呈現前段 wavefront，導致中後段看起來是 0 occupancy。

因此，「in-flight threads = 0」目前只能解讀成「RGP occupancy 視圖在該區間沒有觀測到 wavefront」，不能直接推論 shader 已經沒有任何 wavefront 在執行，也不能直接推論是 atomic / memory R/W 單獨塞住。

## Bug 判斷

目前沒有證據指向 batch queue 邏輯 bug。更像是 RGP Wavefront occupancy 對 compute dispatch 的 trace / presentation 限制或工具行為。App 端計數、timestamp workload scaling、移除 output writes、scalar 對照、人工 ALU tail、ALU-only shader 對照都不支持「batch 演算法在 0 occupancy 下卡 memory」這個解釋。

## 後續可驗證方向

- 用極簡 compute shader 做同樣的人工 ALU tail，看 RGP occupancy 是否也只顯示前段。
- 嘗試調大 SQTT buffer / 改 RGP capture mode，確認不是 trace buffer 或 shader engine sampling 的限制。
- 若 AMD 文件或 RGP issue 能確認 Wavefront occupancy 的統計來源，再更新本解釋。
