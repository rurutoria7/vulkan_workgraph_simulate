# WorkGraph / Vulkan 會議整理

註：本文件依據自動轉寫稿整理，`WorkGraph`、`Vulkan`、`compute shader`、`workgroup`、`wavefront`、`atomic` 等術語已依語境校正；少數人名、個別詞與細節仍建議對照原始錄影再次確認。

## 會議主題

- 報告過去一個月的進度：在 `Vulkan` 上以 `persistent thread / persistent shader` 的方式，實作一個類似 `WorkGraph` 的 `producer-consumer network`。
- 使用科赫雪花（Koch snowflake）碎形生成作為 POC 與測試案例，觀察工作排程、同步、記憶體傳遞與效能瓶頸。
- 討論目前 prototype 的正確性、同步需求、停止條件、排程策略與效能分析結果。

## 三到五句摘要

- 團隊已經做出一個可在 GPU 上跑起來的 WorkGraph 類型 POC，並成功用遞迴式的碎形任務驗證基本概念。
- 目前實作以單一 `compute shader` 承載多個角色節點，透過 queue 與 atomic 操作讓不同 workgroup 彼此傳遞工作。
- 目前最大問題不是「做不出來」，而是效能與排程效率還不理想，尤其在 leaf node、大量 atomic、memory traffic 與跨 compute unit 同步上看起來有明顯瓶頸。
- 與會者提出多個改善方向，包括把 graph 視為 state/DFS 流程、減少 atomic 次數、批次保留工作、動態調整 node 角色分配，以及改進 dispatcher / locator 的設計。
- 會後另有行政事項：確認 5 月底開會時間、申請書送審與文件回饋時程。

## 技術內容整理

### 1. 目前 POC 架構

- 在 `Vulkan` 上模擬 `WorkGraph` 的行為，核心概念是用 `persistent shader` 持續消耗與產生工作。
- 系統將 `NodeA`、`NodeB`、`NodeC` 寫在同一個 `compute shader` 中，由 CPU dispatch 一次 compute work 後持續執行。
- CPU 端流程大致為：clear buffer -> 設定 pipeline/resource barrier -> dispatch compute shader -> 等待寫入 vertex buffer 完成 -> 交給後續 render pipeline 繪製。
- 測試案例使用科赫雪花碎形，目的是用少量節點模擬大量 node 間的工作傳遞與遞迴展開。

### 2. Node 分工

- `NodeA`：初始化工作，將初始邊資料寫入第一個 queue。
- `NodeB`：從第一個 queue 取出一條邊，拆成四條邊；若尚未到終止條件則再丟回第一個 queue，否則送往第二個 queue。
- `NodeC`：從第二個 queue 取出已確定的線段，寫入 `vertex buffer`，供後續繪圖使用。
- 當前實作有固定的 workgroup 角色分配，例如 `NodeA` 執行一次、`NodeB` 與 `NodeC` 各自分配不同數量的 workgroup。

### 3. 同步與正確性問題

- 為避免 consumer 在 producer 尚未寫完 payload 前就取走資料，資料結構中加入了 `ready flag`。
- 討論中明確指出，跨 workgroup / 跨 compute unit 的資料同步不能只靠一般 store/load；需要透過 `atomic` 與適當的 `memory barrier` 維持一致性。
- 逐字稿提到即使在 `SSBO` 上加 `coherent`，保證範圍仍可能只侷限於單一 compute unit，因此 cross-CU 同步仍需額外處理。
- 目前做法可運作，但 atomic 與同步成本可能已成為後續效能瓶頸。

### 4. 停止條件與排程問題

- 現階段 GPU 端停止條件仍偏臨時性：因為科赫雪花的總邊數可預估，所以用計數器統計完成數量，再把 `stop flag` 設為 1。
- 與會者認為更一般化、也更正確的方式，應是檢查 queue 是否已空，且所有 compute unit 都不再產生新工作，才能判定真正完成。
- 目前 workgroup 角色分配是靜態的，但討論指出工作量會隨階段轉移：一開始可能 `NodeB` 重，後期可能 `NodeC` 重，因此需要動態調整。
- 若 producer 與 consumer 能落在同一個 compute unit 或 workgroup，payload 有機會留在 L1 cache，不必頻繁刷回 global memory，可能明顯改善效率。

### 5. 效能觀察

- prototype 已成功執行，但效能與 `WorkGraph` 原生機制相比仍不理想。
- 討論中提到：
  - 深度較淺時可跑起來，但深度拉高後 FPS 明顯下降。
  - occupancy 圖上有些階段顯示沒有活躍 wavefront，但 memory request / rewrite 仍持續很久。
  - 團隊懷疑 leaf node 階段的大量 atomic、memory contention、queue/retry 行為是主要原因。
- 將 `NodeB` / `NodeC` 的 workgroup 比例互換後，結果沒有明顯改善，表示瓶頸不只是靜態比例選得不好，可能還有更深層的同步或 dispatch 問題。

## 討論出的可能優化方向

### 1. 將 graph 視為 state / DFS 式執行

- 有與會者建議將 graph 看成一種 state machine，採類似 DFS 的展開方式：
  - 往下探索時 push state。
  - 子節點完成後再 pop 回上一層。
- 這種方式可能同時幫助：
  - 控制工作展開寬度，避免 queue 過度膨脹。
  - 讓已經不再需要的 parent / ancestor 資料提早釋放。
  - 降低 memory footprint 與 retry / lock 風險。

### 2. 批次取得工作，降低 atomic 壓力

- 會中多次提到「不要每個 thread / lane 都各自 atomic 一次」。
- 較好的方向可能是：
  - 先以 workgroup 或 wave 為單位批次保留一批工作。
  - 之後由 group 內成員按 index 分攤實際寫入位置。
- 這可望減少 atomic 次數、縮短等待區段，也比較接近硬體或原生 WorkGraph 可能採用的做法。

### 3. 重想 dispatcher / locator 的設計

- 目前的 dispatcher 偏向「把工作丟出去」的簡單分流，但與會者認為之後應往更中心化或更有策略的 dispatcher / locator 前進。
- 目標是讓工作分派、角色選擇、批次取得與同步方式可以一起設計，而不是每完成一小段工作就重新競爭一次資源。

### 4. Queue / backing buffer 大小與 deadlock 風險

- queue 開太小會讓很多工作不斷 retry，甚至在有環的 network 下造成 deadlock。
- queue 開太大則會增加 memory footprint。
- 討論中提到 `WorkGraph` 相關設計似乎有提供估算最小 backing buffer 的資訊或 API，這可能正是為了處理類似問題。

### 5. 跨硬體驗證

- 有人提問目前 workgroup size 設為 32，是否在其他 GPU（尤其 `NVIDIA`）上測過。
- 因不同廠牌顯卡的 wave / warp 寬度與排程特性不同，後續值得測試：
  - `AMD` 與 `NVIDIA` 的差異。
  - workgroup size 設為 `32` 或 `64` 的影響。

## 目前較明確的結論

- 這個方向在概念上可行，且團隊已成功做出可執行的 GPU POC。
- 真正困難點集中在同步、排程、queue 管理、atomic 開銷與記憶體局部性，而不是單純 shader 邏輯本身。
- 現有 profiling 已經提供不少線索，足以作為下一輪優化的依據。
- 目前還無法完全確定效能差的單一根因，但 atomic / memory contention 是最被懷疑的來源。

## 待辦事項

- 文謙持續整理本次會議記錄與後續要排查的項目。
- 針對 `atomic` 與 `vertex buffer` / output 階段，再檢查目前每個 lane / 每個 work item 的 atomic 粒度是否過細。
- 評估是否改成「批次保留工作 + group 內分攤」的配置方式。
- 研究將 graph 轉成 state / DFS 風格執行模型的可行性。
- 評估更一般化的 GPU 停止條件，不再依賴預先知道任務總量。
- 測試不同 GPU 或不同 workgroup size（例如 32 vs 64）的表現差異。
- 將目前 profiling 觀察到的瓶頸整理後，必要時再向外部同事或其他專家請教。

## 行政與時程

- 下次 5 月會議時間看起來傾向月底，逐字稿中多次出現「29」，推測傾向 `5/29`，但建議再以行事曆確認。
- 有與會者提到中間有一段時間要出差，因此希望把時間抓得保守一點。
- 另有一份申請書／文件已完成初稿，發言者表示會在當天寄給老師與相關人員確認內容。
- 其他與會者預計於週末查看內容，若有修改意見會儘快回覆。
- 文件提交期限提到是「30 號、下週四前」，建議再對照實際日曆確認最終截止日期。

## 未決問題 / 後續追蹤

- 如何在不大量增加 atomic 與 contention 的前提下，維持 producer-consumer 的正確同步？
- 如何根據 runtime workload 動態調整 `NodeA` / `NodeB` / `NodeC` 的 workgroup 分配？
- 如何讓 producer-consumer 盡可能留在同一快取層級，減少 global memory traffic？
- profile 中出現「沒有活躍 wavefront、但仍持續有 memory activity」的區段，根因仍需查明。
- 若改用批次 reservation、中心化 dispatcher/locator，是否能縮小與原生 `WorkGraph` 的差距？
