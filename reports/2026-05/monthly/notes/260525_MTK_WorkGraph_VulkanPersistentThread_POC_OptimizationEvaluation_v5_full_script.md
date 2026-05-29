# Optimization Evaluation v5 Full Speaker Script

Date: 2026-05-25

Deck:
`reports/2026-05/monthly/decks/260525_MTK_WorkGraph_VulkanPersistentThread_POC_OptimizationEvaluation_v5.pptx`

Numeric source:
`reports/2026-05/metrics/monthly_v5_normalized_measurements_20260525.csv`

Measurement rules:
`reports/2026-05/metrics/monthly_v5_measurement_notes_20260525.md`

## 使用方式

講法採用「先結論、再證據、最後轉場」。每頁只講一件事。表格只作為 audit，不逐格朗讀。

講者可以把英文技術名詞照投影片念，中文句子負責連接邏輯。數字主張限於 deck、CSV、measurement notes 已列內容。

## Slide 01 - Cover

改法理由：
封面先交代最終判斷與三個核心數字，讓聽眾立刻知道整份報告要證明的事。

完整講稿：
今天的結論直接：Vulkan persistent-thread WorkGraph POC 的主要瓶頸落在 shared queue atomics。當我們把 Q1 和 Q2 的 shared global queue state 拆開，並且讓 Q1 lane-pop 分散掃描起點，clean median 從 30.75 ms 降到 0.99 ms。

31.0x median speedup 來自同一 workload。這份簡報不把 counter-enabled runs 當作 duration claim；duration 只看 clean timestamp runs。Counters 用來解釋機制。

主路徑留下 Q1/Q2 sharding 和 lane-based Q1 pop。Request batching 先放到 rejected path，因為 default 256-shard suite 會回到 11 ms 區間。

## Slide 02 - Queue Atomics Explain The Measured Gains

改法理由：
摘要頁改成四個決策欄位：瓶頸、測量邊界、採用路徑、拒絕路徑。

完整講稿：
整份報告用四個框架讀。第一，bottleneck 是 shared Q1/Q2 queue pressure。Clean median 下降的時間點，對應到 queue pressure 被拆散或掃描起點被分散。

第二，measurement boundary 要先鎖住。Timestamp-only runs 負責 duration；counter-enabled runs 負責 CAS、backlog、ready-spin 的診斷。兩種 run 回答不同問題，分開使用。

第三，adopted path 包含 Q1 shards、Q2 shards、lane-based Q1 pop。這條路徑把 final repeat 推到約 0.99 ms median。

第四，Q2 request batching 被拒絕。它的想法合理，但 default 256-shard suite 進入 11 ms band，沒有通過主路徑採用規則。

## Slide 03 - Shared Queue State

改法理由：
架構頁把焦點從整條 pipeline 收回 Q1/Q2 的 shared global queue state。

完整講稿：
先看資料流。Node A seed edges once，Node B subdivide work，Node C write vertices，最後 VB Render 做 deterministic draw。

真正需要盯住的是 Q1 和 Q2。兩個 handoff point 有 count、head、tail、ready flag。多個 lanes 和 workgroups 會在 sharding 之前碰同一組欄位。

所以採用規則清楚：候選改動要降低 clean duration、queue contention，或兩者一起下降。若改動只是把成本轉到 polling、ready-spin、empty slots，主路徑拒絕。

## Slide 04 - Adoption Rule

改法理由：
本頁把 May 10 前後的測量方式分開，讓後續所有數字有一致判準。

完整講稿：
May 10 之前，POC 已經能產生正確 deterministic output，persistent workers 也透過 global atomic queues 交換工作。不過 duration runs 還帶著 diagnostic counters。

May 10 之後，採用規則改成 clean timestamp runs 決定 performance number。Counter-enabled runs 只用來診斷 CAS retries、backlog、polling。

後面每個優化都照同一條規則讀：clean timing 改善才有資格進 main path；counters 用來解釋為什麼改善，或為什麼 regression 發生。

## Slide 05 - Timing And Counters

改法理由：
測量類別被寫成操作契約，讓 performance claim 和 mechanism claim 分工。

完整講稿：
這頁把三種資料來源的角色分清楚。Clean timestamp runs 回答 performance claim，主讀數是 warmup 後的 median compute_ms。Duration、speedup、adoption 都看這一類。

Counter-enabled runs 回答 mechanism check。它們會看 CAS fail ratio、backlog、ready-spin、empty-pop。Counter instrumentation 會增加 atomic traffic，所以它們只做 queue contention diagnosis。

RGP occupancy 提供 context。它能幫我們確認 profiler boundary 和事件範圍，但 duration proof 仍然要回到 clean timestamp。這條分工保護 headline number。

## Slide 06 - Atomic Metrics Map

改法理由：
counter 名稱本身不夠。v5 保留 metric dictionary，讓聽眾知道每個 counter 對應哪一種 queue failure mode。

完整講稿：
後面的診斷要靠這張表。CAS fail per success 代表 lanes 或 workgroups 對同一個 queue state 重試。若 sharding 有效，retry rate 應該下降。

Attempts versus success 反映 control path 是否在消耗操作但沒有產生 throughput。Queue high-water 告訴我們 producer-consumer balance，這也是 B/C balance 和 Q2 pressure 的依據。

Ready-spin 和 empty-pop 用來解釋 request batching regression。當 clean duration 改善，同時相關 counter pressure 同方向下降，我們才把機制解釋拉高到可信結論。

## Slide 07 - Bottleneck Timeline

改法理由：
timeline 頁改成主圖優先，右側三個大數字給整體結果，底部 evidence strip 保留可追溯的每一步。

完整講稿：
先讀左上的 curve。Baseline 是 30.75 ms。B/C ratio 調整後到 27.89 ms，這是一個 setup improvement，主要突破來自後續 queue structure。

接著 Q1 sharding 把 median 拉到 13.74 ms。Q2 sharding 再降到 5.57 ms。Q1 lane-pop 到 3.85 ms。High shard scaling 到 0.99 ms。

右側三個大數字整理整條路徑：起點 30.75 ms，final repeat 0.99 ms，median speedup 31.0x。每一次大幅下降都對應 queue split 或 scan-diversification step。Request batching 不進 adopted path。

## Slide 08 - B/C Tuning

改法理由：
B/C tuning 被定位成 setup fix，讓後續 sharding 的主貢獻更清楚。

完整講稿：
B/C tuning 先處理 writer balance。C=40/B=56 讓 Q2 high-water 從約 50k 降到約 9k，median 從 30.75 ms 改善到 27.89 ms。

這一步有幫助，因為 Node C writers 增加後，Q2 backlog 被壓低。不過 median 只改善 2.86 ms，shared atomic hotspot 還在。

因此後面的測試用 C=40/B=56 作為結構性 queue changes 的前置設定。這條基準能排除 writer imbalance 對 sharding 判讀的干擾。

## Slide 09 - Q1 Sharding

改法理由：
Q1 sharding 頁把因果順序放前面：先拆 Q1，然後 Q2 壓力成為下一個瓶頸。

完整講稿：
Q1 sharding 把 single global atomic queue 拆成 16 個小 queue。Q1s16/Q2s1 的 median 到 13.74 ms。

Q1 改善能和 B/C tuning 疊加，所以收益來源指向 queue contention。它改的是 queue locality，workload 維持同一條比較線。

當 Q1 的壓力下降，Q2 count、head、tail 就變成下一個 shared queue target。下一頁會看到 Q2 sharding 如何接住該問題。

## Slide 10 - Q2 Sharding

改法理由：
本頁先說決策，再讓表格證明：Q2 要先 shard，再談 batching。

完整講稿：
Q2 sharding 移除第二個 global queue wall。表格顯示 Q2 從 1 shard 增加到 16 shards 時，median 持續改善。

在 B/C rebalance 之後，Q1s16/Q2s16 約 5.57 ms median。在該 sweep 裡，它相對 Q2s1 是 2.79x。

這裡的模式和 Q1 相同：先 split shared queue state，讓同一組 atomic fields 的競爭下降，再回頭 retune worker balance。這也是後面拒絕 request batching 的前提。先把 queue state 拆開，再測 batch claim。

## Slide 11 - Q1 Lane-Pop

改法理由：
v5 保留 exact scan-start rule，讓 lane-pop 與 batching 的差異可被審查。

完整講稿：
Lane-pop 沒有改 queue protocol。它只改每個 lane 從哪個 Q1 shard 開始掃。

舊規則是 `wgId % Q1_QUEUE_SHARDS`。同一個 workgroup 內的 lanes 會從相同 shard 起步。新規則是 `(wgId * local_size_x + localId) % Q1_QUEUE_SHARDS`，讓 lane-level scan starts 分散。

因為 protocol 不變，所以改善可以更直接地歸因到 shard-selection pressure 降低。這一步把 median 推到 3.85 ms。

## Slide 12 - High Shard Scaling

改法理由：
本頁把 256/256 講成 measured knee，並把範圍限定在此 architecture 與 workload。

完整講稿：
equal Q1/Q2 shard sweep 的最佳 median 出現在 256/256。圖上可以看到，shard count 增加到 256 時，median 持續下降。

384 和 512 沒有改善 tail，還開始支付更多 probe 和 indexing overhead。這表示 256/256 是此 architecture 與 workload 下的 measured knee。

所以 main default 採用 256 Q1 shards 和 256 Q2 shards。採用範圍只涵蓋此 GPU、此 workload、此 POC 測量結果。

## Slide 13 - Request Batching Hypothesis

改法理由：
request batching 先被公平描述，再進入採用測試，讓 rejected path 由資料驅動。

完整講稿：
Request batching 的假設合理。問題是 per-item dequeue CAS 會製造 retry traffic。想法是讓 subgroup 一次 claim 多個 slots，降低每個 consumed item 的 CAS attempts。

期待結果也清楚：CAS attempts per consumed item 應該下降。風險也明確：如果 batch claim 太早或 slot readiness 不匹配，成本會轉成 ready-spin 或 empty slots。

採用測試因此收斂。Batch claim 必須在 default 256-shard suite 降低 clean median 和 P90，並且讓 ready-spin / empty-slot work 維持低位。

## Slide 14 - Request Batching Result

改法理由：
結果頁把兩個強制數字放在最上方，讓拒絕理由先於表格。

完整講稿：
default 256-shard suite 的 off baseline 是 1.9432 ms。Batching 後，valid L8、L16、L32 rows 都進入 11 ms range。

右邊的 stress 16 suite 可以看到部分 case 改善，代表 request batching 仍可能適合其他情境。但 main protocol 的採用條件看 default 256-shard suite。該條件沒有通過。

因此結論是：current main protocol 拒絕 batching。等 queue protocol 真的改變，例如先做 wave-local retention 或其他 queue-protocol change，再回來重測 batch claims。

## Slide 15 - Main Defaults

改法理由：
defaults 頁把設定寫成前面決策的結果，而非單純列 knob。

完整講稿：
main path 的設定來自前面的採用結果。Q1 shards 是 256，Q2 shards 是 256，Q1 lane-pop 開啟，C/B workers 是 24/72。

這組 defaults 對應三個 measured mechanisms。第一，split both queues。第二，diversify Q1 scan starts。第三，保留 final path 裡 best clean run 的 B/C mix。

如果之後要改 default，應該回到同樣的採用規則：clean timing 先變好，counter diagnosis 再解釋機制。

## Slide 16 - Final Confirmation

改法理由：
final repeats 被放在穩定性檢查脈絡，用四次 run 支撐 final path。

完整講稿：
final confirmation 有四次 clean repeats。每次 sample count 約一千筆，median 維持在約 0.99 ms，P90 維持在約 1.00 ms。

右下角的結論是 median band 約 0.0014 ms。該 band 支撐 adopted path 的穩定性。

這裡仍然維持 measurement boundary：這組穩定性檢查來自 clean timestamp runs。Counter-enabled runs 可以解釋機制，但 final performance claim 看 clean repeats。

## Slide 17 - Next Experiment

改法理由：
結論頁導向下一個實驗，讓研究路線從已證明的瓶頸自然推進。

完整講稿：
目前已經證明三件事。第一，shared queue atomics 驅動 measured bottleneck。第二，Q1/Q2 sharding 加 Q1 lane-pop 應該保留在 main path。第三，Q2 request batching 沒有通過 default-suite test。

current architecture 已經到約 0.99 ms median。下一個有價值的方向應減少 trips to global queues。

所以下一步應該測 wave-local retention 或等價的 queue-protocol change。先減少 global spill，再重新評估 batching 是否有位置。

## Slide 18 - Wave-Local Retention

改法理由：
下一步被寫成 measurement contract，讓後續實驗能直接接到 baseline、prototype、comparison metrics。

完整講稿：
baseline 使用 current 256-shard main path，也就是 Q1/Q2 = 256/256。Prototype 是 wave-local retention with spill fallback。

比較指標要包含 global atomic count、wave-local handoff count、spill ratio、clean duration，以及 P90/tail stability。這組指標同時回答兩個問題：global queue trips 是否下降，tail 是否被新的 spill path 破壞。

Request batching 可以回來，但時機要放在 queue-protocol change 之後。下一輪仍然維持 timing 和 atomic-counter diagnosis 分離。

## Slide 19 - Appendix: Code Surfaces

改法理由：
appendix 保留 code surface，讓採用與拒絕的機制能回到 shader code 審查。

完整講稿：
如果聽眾追問機制如何回到 code，本頁回答三個點。

第一段是 shard push 和 ready publish。它對應 adopted path 裡的 shard selection plus ready publish。第二段是 Q1 lane-pop scan start，對應 `(worker % Q1_QUEUE_SHARDS)` 該 lane-based 起點。

第三段是 rejected Q2 batch CAS claim。該 code 代表 request batching 要測的機制。拒絕原因落在採用規則：default 256-shard suite 的 clean timing 沒有通過。

## Slide 20 - Appendix: Numeric Source Table

改法理由：
最後一頁收束 provenance，讓聽眾知道 headline numbers、measurement rules、research backbone、batching result 各自來自哪裡。

完整講稿：
所有 headline numbers 都來自 normalized measurements。Measurement notes 記錄 warmup rules 和 source-file mapping。

Main backbone 指向 `research_backbone_20260510.md`。Request batching summary 指向 validation summary CSV。

如果會後要 audit，請先看 normalized table，再看 notes file。Timeline 解釋回到 research backbone；request batching 的控制結果回到 batching validation summary。

## 講者紀律

- 不新增 deck 或 CSV 之外的數字主張。
- RGP occupancy 只作為 context；dispatch duration proof 回到 clean timestamp runs。
- Request batching 的拒絕範圍限於 current main protocol 與 default 256-shard suite。
- 256/256 的範圍限於此 architecture 與 workload。
- 主訊息：先拆 shared global queue state，再討論 batch claims。
