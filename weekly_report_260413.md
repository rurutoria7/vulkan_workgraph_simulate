# 週進度報告 - 2026/04/13

## 1. 專案背景 (Project Background)
本週啟動了 **Vulkan Workgraph POC** 專案，旨在基於 Vulkan 實現一個高效的 **Persistent Thread Producer-Consumer Network**。該技術是現代 GPU 調度（如 D3D12/Vulkan Work Graphs）的軟體實現基礎，用於模擬 GPU 內部的任務動態分發與自動化調度。

## 2. 本週達成目標 (Achievements)

### 2.1 環境搭建與驗證
- **源碼庫集成**: 成功引入 Sascha Willems 的 Vulkan 範例庫作為開發基礎。
- **硬體驗證**: 在 **AMD Radeon RX 7900 XTX** 上完成初步測試，確認支援 Vulkan 1.3 並具備極高的計算吞吐量。

### 2.2 基礎設施構建 (Phase 1)
- **獨立範例開發**: 建立了 `examples/workgraph_poc` 目錄，並成功接入全域 CMake 構建系統。
- **數據佈局設計**: 定義了 `ControlBlock` 與 `TaskQueue` 的 SSBO 佈局，實現了 Host 與 Device 之間的共享控制面。
- **技術驗證**: 通過基礎的斐波那契計算驗證了 Pipeline 與 Descriptor Set 的 Binding 正確性。

### 2.3 持久化線程模型實現 (Phase 2)
- **Persistent Kernel**: 實現了基於 `while` 循環的持久化 Compute Shader，打破了傳統「Dispatch-Wait」的執行模式。
- **生產者-消費者模型**: 
    - **Producer**: 固定一個線程負責生成任務數據並推入環形緩衝區。
    - **Consumer**: 其餘線程並行競爭任務，實現了原子化的任務領取邏輯。
- **實時監控系統**: 實現了 C++ 端對 GPU 內存的 Persistent Mapping，可在控制台實時觀測任務處理進度、Head/Tail 指標變化。
- **動態終止**: 成功實現了 CPU 向 GPU 發送 `stopFlag` 訊號以安全關閉持久線程的功能。

## 3. 技術數據 (Technical Data)
- **初步性能測試**: 在 7900 XTX 上，單一 Workgroup (32 threads) 每秒可處理數百萬次簡單原子操作任務。
- **並發處理數**: 已成功驗證 4,400,000+ 次任務的正確閉環處理。

## 4. 待解決問題 (Pending Issues)
- **原子操作競態**: 在極高併發下，`count` 指標存在下溢 (Underflow) 風險，需在 Phase 3 中引入更嚴謹的 Slot 預留機制。

## 5. 下週計劃 (Next Steps)
- **Phase 3: Node Network**: 擴展為多級隊列模型，模擬更複雜的任務依賴樹。
- **負載均衡優化**: 增加 Dispatch 的工作組數量，測試全局 Occupancy 下的數據一致性。
- **Subgroup 優化**: 考慮引入 Subgroup 原子操作以減少全局記憶體競爭。
