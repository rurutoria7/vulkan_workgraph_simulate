# Weekly Report — 2026/04/13

## 專案名稱
Vulkan Persistent Thread Producer-Consumer Network POC

## 研究定位（更正版）

本研究定位更正為：基於 software 實作盡可能快的 producer-consumer 架構。重點不是單純模仿 D3D Work Graphs 的 API，而是透過實作、profiling 與 metrics 找出瓶頸，並提出可落地的 optimization 方向。

## 研究意義

這能讓 MTK 預先知道：若未來要在 mobile GPU 上實現 producer-consumer，主要瓶頸可能會落在哪裡，例如 atomic contention、global memory traffic、queue pressure、producer/consumer imbalance、memory visibility 與 spin-wait，進而提供 runtime、driver、hardware 的優化方向。

## 本週工作摘要

本週的核心目標是「跑通整條路徑」——從零搭建一個可運作的 persistent thread producer-consumer 原型，確認技術可行性，為後續性能量測建立基礎。

---

### 一、環境建置與專案架構（Phase 1）

**Fork 與目錄結構**
- 從 SaschaWillems/Vulkan 範例庫 fork，建立獨立的 POC 目錄 `examples/workgraph_poc/`。
- 選擇此範例庫的原因：它提供了完整的 Vulkan 基礎設施（`VulkanTools.h` 中的 `VK_CHECK_RESULT` 錯誤檢查、`vks::initializers::*` 建構輔助函式），省去從零搭建 boilerplate 的時間。

**設計決策：不繼承 VulkanExampleBase**
- POC 類別 `VulkanExampleWorkgraphPOC` 不繼承框架的 `VulkanExampleBase`，而是手動建構完整 Vulkan 物件鏈。
- 原因：POC 是 headless compute 應用，不需要 swapchain、render pass、framebuffer 等渲染管線物件。直接管理物件可以減少不必要的依賴，也讓後續性能量測的路徑更乾淨。

**CMake 整合**
- 將 `workgraph_poc` 作為獨立的 CMake target 加入構建系統，可單獨編譯：`cmake --build build --target workgraph_poc --config Release`。
- Shader 編譯使用 `glslangValidator` 手動執行，SPIR-V 二進制檔（`.spv`）與 GLSL 原始碼一併提交至 repo。

---

### 二、Vulkan 物件鏈建構

完整的物件建構流程在構造函式中一次完成，依序為：

1. **VkInstance**：使用 Vulkan 1.1 API（`VK_API_VERSION_1_1`），不啟用 validation layer（headless 模式下暫時不需要）。
2. **VkPhysicalDevice**：選取系統中第一個 Vulkan 裝置（目標硬體為 AMD RX 7900 XTX）。
3. **VkDevice + VkQueue**：遍歷 queue family，找到支援 `VK_QUEUE_COMPUTE_BIT` 的 family index，建立單一 compute queue。
4. **VkCommandPool + VkCommandBuffer**：啟用 `VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT` flag，允許重置 command buffer。
5. **三個 SSBO（Storage Buffer）**：
   - `buffers.control`（32 bytes）：`ControlBlock` 結構，包含兩組 `QueueControl`（head/tail/count）、`stopFlag`、`totalProcessed`。
   - `buffers.queue1`（8192 bytes = 16 bytes × 512）：Node A → Node B 的環形佇列。
   - `buffers.queue2`（8192 bytes = 16 bytes × 512）：Node B → Node C 的環形佇列。
   - 三個 buffer 皆使用 `HOST_VISIBLE | HOST_COHERENT` 記憶體，支援 CPU 端 persistent mapping。
6. **Descriptor Set**：三個 `VK_DESCRIPTOR_TYPE_STORAGE_BUFFER` 分別綁定到 binding 0/1/2。
7. **Compute Pipeline**：載入預編譯的 `headless.comp.spv`，建立單一 compute pipeline。
8. **VkFence**：用於 host 端等待 GPU 完成。

---

### 三、Persistent Thread 機制實現（Phase 2）

**Shader 端持久循環**
- Compute shader 使用 `local_size_x = 32`，dispatch 為 `(32, 1, 1)`，共啟動 32 × 32 = 1024 個線程。
- 每個線程進入 `while (control.stopFlag == 0)` 無窮迴圈，持續執行直到 host 端寫入停止信號。
- 線程角色由 `gl_GlobalInvocationID.x % 32` 決定：
  - `== 0`：Node A（Producer），每個 workgroup 1 個生產者。
  - `1–15`：Node B（Transformer），每個 workgroup 15 個轉換者。
  - `16–31`：Node C（Consumer），每個 workgroup 16 個消費者。

**無鎖環形佇列同步（CAS-based Ring Buffer）**
- 同步機制分兩步：
  1. **容量檢查 + 預約**：使用 `atomicCompSwap(count, expected, expected ± 1)` 進行 CAS 操作。成功表示線程已「預約」到一個 slot，`count` 的更新是原子的，不會超賣。
  2. **Slot 分配**：CAS 成功後，使用 `atomicAdd(head/tail, 1) % QUEUE_SIZE` 取得實際讀寫位置。
- 每次讀寫完成後呼叫 `memoryBarrierBuffer()` 確保資料對其他線程可見。
- Queue buffer 使用 `coherent` 修飾符。

**這個設計的已知限制**（後續需要解決）：
- CAS 在高競爭下會頻繁失敗，線程空轉浪費 GPU cycle。
- `QUEUE_SIZE` 必須在 shader（specialization constant）與 C++（`#define`）兩端手動保持一致。
- 目前所有佇列都在 global memory（SSBO），延遲較高。

---

### 四、多節點網絡實現（Phase 3 — 部分完成）

**三節點拓撲**
```
Node A (Producer) --[Queue 1]--> Node B (Transformer) --[Queue 2]--> Node C (Consumer)
```

- **Node A**：檢查 Queue 1 是否有空位 → CAS 預約 → `atomicAdd(q1.tail)` 取得寫入 slot → 寫入 payload `777`。
- **Node B**：檢查 Queue 1 是否有任務 → CAS 領取 → `atomicAdd(q1.head)` 取得讀取 slot → 讀取 payload → 加工（`data + 1`）→ 透過相同的 CAS 流程推入 Queue 2。Node B 在 Queue 2 滿時會自旋等待（`while (!pushed && !stopFlag)`），這是一個潛在的性能瓶頸。
- **Node C**：檢查 Queue 2 是否有任務 → CAS 領取 → 讀取 payload → `atomicAdd(totalProcessed, 1)` 累計已處理數量。

**Host 端即時監控**
- 使用 `vkMapMemory` 對 control buffer 做 persistent mapping，CPU 端直接讀取 GPU 寫入的 `totalProcessed`、`q1.count`、`q2.count`。
- 監控迴圈每 100ms 輪詢一次，以 `\r` 原地刷新輸出。
- 使用者按任意鍵觸發 `mappedControl->stopFlag = 1`，GPU 端所有線程在下一次迴圈迭代時偵測到信號並退出。
- 之後 `vkWaitForFences` 等待 GPU 完全結束。

---

### 五、驗證結果

- 程式可正確編譯（MSVC, Vulkan SDK 1.4.304.1）、運行於 AMD RX 7900 XTX。
- 觀察到 `totalProcessed` 持續增長，確認任務從 Node A → Node B → Node C 的完整流轉正常運作。
- CPU 端監控可即時反映 GPU 端的佇列狀態（`Q1 count`、`Q2 count`），確認 `HOST_COHERENT` persistent mapping 機制運作正常。
- 按鍵停止後 GPU 端可正常退出，無 hang 或 device lost。

---

## 現階段已知問題與風險

1. **CAS 競爭開銷未量化**：目前沒有 timestamp query 或 performance counter，無法量化 CAS 失敗率與空轉開銷。
2. **Node B 的阻塞式轉發**：Node B 在 Queue 2 滿時會自旋等待，可能導致整個 pipeline 反壓（back-pressure），嚴重時可能影響 Node A 的產出率。
3. **全部在 global memory**：目前兩條佇列都位於 SSBO（device global memory），延遲約為 LDS 的 10–100 倍。這正是 Phase 4 要驗證的重點。
4. **線程角色靜態分配**：Producer / Transformer / Consumer 的比例固定為 1:15:16，尚未驗證這是否為最佳比例。
5. **沒有錯誤恢復機制**：如果 CAS 進入 livelock（理論上不太可能但未排除），沒有 timeout 或 fallback 路徑。

## 下週計畫

1. **Phase 3 收尾**：
   - 實現 branching 邏輯（一個任務觸發多個子任務），驗證 DAG 結構的任務流。
   - 考慮增加一個 specialization constant 控制線程角色比例，方便實驗。

2. **Phase 4 啟動 — LDS（Shared Memory）佇列**：
   - 將 Task Queue 移至 `shared` memory（LDS），量測 producer-consumer handoff 是否受 global memory traffic 限制。
   - 這是用來定位瓶頸與驗證優化方向的實驗，不再表述為「persistent thread 是否比 execute indirect 更低延遲」。

3. **性能量測基礎建設**：
   - 加入 `vkCmdWriteTimestamp` / pipeline statistics query，建立 baseline 數據。
   - 量測 CAS 失敗率（可在 shader 中加入 per-thread failure counter）。

4. **Subgroup 優化探索**：
   - 調查 AMD RDNA3 的 subgroup 操作（`subgroupBallot`、`subgroupElect`）是否能減少原子操作競爭。
