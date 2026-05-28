# Optimization Evaluation v4 Speaker Notes

Date: 2026-05-25

Deck:
`reports/2026-05/monthly/decks/260525_MTK_WorkGraph_VulkanPersistentThread_POC_OptimizationEvaluation_v4.pptx`

Numeric source:
`reports/2026-05/metrics/monthly_v4_normalized_measurements_20260525.csv`

Measurement rules:
`reports/2026-05/metrics/monthly_v4_measurement_notes_20260525.md`

Reference standard:
`C:\Users\Kevin\Downloads\Dost_Jeremiah_ApexLegendsDevSupport.pdf`

## 使用原則

每頁只負責一個判斷或一段證據。講者先說結論，再指向圖表或數字，最後交代下一頁需要接住的問題。

v4 保留原本白底視覺系統。顏色只標示資訊角色：

- Orange: 問題、瓶頸、起始狀態。
- Blue: 證據、邊界、測量規則。
- Green: 採用路徑、保留設定。
- Red: 拒絕路徑、未通過採用規則的結果。
- Gray: 中性背景。

v4 沒有調整數值、程式碼、來源路徑、測量規則。修改集中在證據排序、第一眼重點、講稿節奏。

## Slide 01 - Cover

改法理由：
封面直接給出決策句：queue atomics 是瓶頸，sharding 解開瓶頸。三個數字盒先交代起點、終點、倍率，讓聽眾在進入細節前知道報告要證明什麼。

講法：
先講結果。主路徑從 30.75 ms median 降到 0.99 ms median，同一 workload 得到 31.0x median speedup。採用路徑是 Q1/Q2 sharding 加上 Q1 lane-pop。Request batching 留在 rejected path，原因是 default 256-shard setup 會回到 11 ms 區間。

## Slide 02 - Queue Atomics Explain The Measured Gains

改法理由：
舊版像狀態清單。v4 改成決策框架：瓶頸、測量邊界、採用路徑、拒絕路徑。

講法：
主張要收窄。Clean timestamp runs 決定效能數字；counter-enabled runs 解釋 duration 為什麼移動。資料指向 shared Q1/Q2 queue pressure。會降低 queue contention 的改動留下；Q2 request batching 在 default 256-shard path 失敗。

## Slide 03 - Shared Queue State

改法理由：
架構頁只標出真正共享的 global queue state，把焦點從整條 pipeline 收回 Q1/Q2。

講法：
Node A seed work，Node B subdivide，Node C write vertices。共享壓力集中在 Q1/Q2 的 count、head、tail、ready flags。候選優化要降低 clean duration 或 queue contention；若只是把成本移到 polling 或 ready-spin，就不進 main path。

## Slide 04 - Adoption Rule

改法理由：
v4 把 May 10 前後的測量規則分清楚。修正測量方式進入判讀主線。

講法：
May 10 之前，POC 已能運作，但 duration runs 仍帶著 diagnostic counters。修正後，clean timestamp runs 決定 performance number；counter-enabled runs 診斷 CAS retries、backlog、polling。只有 clean timing 變好，改動才進入主路徑。

## Slide 05 - Timing And Counters

改法理由：
v4 把測量類別寫成操作契約。表格直接說明每種 run 的證明範圍與限制。

講法：
Clean runs 用來決定採用；counter runs 用來診斷 queue failure mode；RGP 提供範圍與背景。headline duration 只吃 clean timestamp，counter instrumentation 留在診斷層。

## Slide 06 - Atomic Metrics Map

改法理由：
counter 名稱本身不足以支撐討論。v4 把每個 metric 對應到決策用途，後面看數據時不用重新解釋。

講法：
CAS failures 看 retry pressure。Attempts versus success 看 polling。Queue high-water 看 producer-consumer balance。Ready-spin 和 empty-pop 解釋 request batching regression。Clean duration 和相關 counter 同方向移動時，診斷可信度提高。

## Slide 07 - Bottleneck Timeline

改法理由：
v4 參考 Apex 範例的資訊排序：一個主證據物件、少數大數字、再給結論。詳細 timeline 從主區塊移到底部 evidence strip，第一眼先讀曲線與結論。

講法：
先讀曲線。大幅下降出現在 B/C tuning、Q1 sharding、Q2 sharding、Q1 lane-pop、high shard scaling。final repeat 是 0.99 ms median。Request batching 不屬於採用路徑。

## Slide 08 - B/C Tuning

改法理由：
B/C tuning 屬於 setup fix，主要突破來自後續 queue structure。兩個數字盒說明 C=40/B=56 為什麼值得先固定。

講法：
增加 Node C writers 後，Q2 high-water 從約 50k 降到約 9k。Median 從 30.75 ms 改善到 27.89 ms。效能有動，但 shared atomic hotspot 仍存在。先用 C=40/B=56，再測結構性 queue change。

## Slide 09 - Q1 Sharding

改法理由：
v4 把因果順序拉出來：先 split Q1，接著 Q2 壓力浮現。

講法：
Q1 sharding 把 single global atomic queue 拆成 16 個小 queue。Q1s16/Q2s1 達到 13.74 ms median。結果能和 B/C tuning 疊加，收益來源指向 queue contention。Q1 壓力下降後，下一個目標就是 Q2。

## Slide 10 - Q2 Sharding

改法理由：
v4 先講決策，再讓表格做 audit：先 shard Q2，再討論 batching。

講法：
Q2 sharding 移除第二個 global queue wall。Q2 從 1 shard 增加到 16 shards 時，median 持續改善。B/C rebalance 後，Q1s16/Q2s16 約 5.57 ms median，在該 sweep 裡相對 Q2s1 是 2.79x。

## Slide 11 - Q1 Lane-Pop

改法理由：
v4 明確切開 lane-pop 與 batching。頁面保留 exact scan-start rule，讓機制差異可被審查。

講法：
Lane-pop 只改每個 lane 從哪個 shard 開始掃。舊規則是 `wgId % Q1_QUEUE_SHARDS`；新規則是 `(wgId * local_size_x + localId) % Q1_QUEUE_SHARDS`。Queue protocol 不變，所以改善來自 shard-selection pressure 降低。

## Slide 12 - High Shard Scaling

改法理由：
v4 把 256/256 定位成 measured knee，範圍限定在此 architecture 與 workload。chart 承擔主要論證，table 提供追溯細節。

講法：
equal-shard sweep 的最佳 median 出現在 256/256。384 和 512 沒有改善 tail，還開始支付更多 probe 和 indexing overhead。採用 256/256，因為這組設定是此 architecture 與 workload 下的 measured knee。

## Slide 13 - Request Batching Hypothesis

改法理由：
v4 先公平描述 request batching 要解的問題，再給採用風險與檢驗條件。

講法：
假設合理：用一次 subgroup claim 保留多個 slot，降低每個 consumed item 的 dequeue CAS attempts。採用規則仍然嚴格。它必須在 default 256-shard suite 降低 clean median 和 P90，且 ready-spin / empty-slot work 維持低位。

## Slide 14 - Request Batching Result

改法理由：
v4 把決策和兩個強制數字放到頁面上方。表格留在下方做 audit，第一眼讀拒絕理由。

講法：
default 256-shard suite 的 off baseline 是 1.9432 ms。Batching 讓 valid L8/L16/L32 rows 回到 11 ms range。Stress cases 可以改善，但 main protocol 沒有通過採用規則。當前主路徑拒絕 batching；等 queue protocol 改變後再回來測。

## Slide 15 - Main Defaults

改法理由：
v4 把 defaults 寫成前面決策的結果，並標示每個設定的來由。

講法：
main path 使用 256 Q1 shards、256 Q2 shards、Q1 lane-pop on、C/B workers 24/72。這組設定同時 split both queues、diversify Q1 scan starts，並保留 final path 中最好的 clean-run C/B mix。

## Slide 16 - Final Confirmation

改法理由：
v4 把 repeated runs 放在穩定性檢查脈絡，讓數字承擔穩定性證據。

講法：
final confirmation 有四次 clean repeats。Median 維持在約 0.99 ms，P90 維持在約 1.00 ms。median band 約 0.0014 ms，表示採用路徑在 clean timestamp runs 裡穩定。

## Slide 17 - Next Experiment

改法理由：
結論頁從摘要推進到下一個實驗。

講法：
目前已確認 bottleneck class，保留有效 queue changes，並把 Q2 request batching 移出 current protocol。下一個有價值的實驗應該減少進入 global queues 的次數，再重新評估 batch claims。

## Slide 18 - Wave-Local Retention

改法理由：
v4 把下一步寫成 measurement contract：baseline、prototype、comparison metrics 都要明確。

講法：
baseline 使用 256-shard main path。Prototype 是 wave-local retention with spill fallback。比較 global atomic count、wave-local handoff count、spill ratio、clean duration、P90/tail stability。只有 queue protocol 改變後，request batching 才值得重新放回實驗矩陣。

## Slide 19 - Code Surfaces

改法理由：
appendix 保留精確 code surface，讓聽眾能審查採用與拒絕的機制各自碰到哪些 shader 區域。

講法：
第一段 snippet 對應 shard push 和 ready publish。第二段 snippet 對應 lane-based Q1 scan start。第三段 snippet 對應已拒絕的 Q2 batch CAS claim。若聽眾追問機制如何回到 shader code，再使用 appendix。

## Slide 20 - Numeric Provenance

改法理由：
最後一頁關閉 audit loop，列出 normalized table、measurement notes、research backbone、request batching summary。

講法：
所有 headline numbers 都來自 normalized measurement table。Warmup rules 和 source-file mapping 在 notes file。Research backbone 是 bottleneck timeline 的 canonical source。Request batching summary 支撐 controlled batching result。

## 講者紀律

- 數字主張限於 deck 或 CSV 已列內容。
- RGP occupancy 只能作為背景，dispatch-lifetime proof 要回到 clean timing。
- Request batching 的結論限於 current main protocol 與 default 256-shard suite。
- 256/256 是此 architecture 與 workload 下的 measured knee。
- 主訊息：先拆 shared global queue state；protocol 改變後，再討論 batch claims。
