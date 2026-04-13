# Persistent Thread Producer-Consumer Network (Vulkan POC)

## 1. 專案目標 (Project Goal)
基於 Sascha Willems 的 Vulkan 範例庫，製作一個 Persistent Thread Producer-Consumer Network 的技術原型 (POC)。
模擬 Work Graphs 的調度行為，透過持久運行的 Compute Shader 線程在 GPU 內部完成任務的生成與消費。

## 2. 核心架構 (Core Architecture)
- **Persistent Kernels**: 透過單次 `vkCmdDispatch` 啟動，線程在 Shader 內部使用 `while` 循環運行。
- **Shared Memory Queue**: 使用 SSBO (Storage Buffer) 作為工作隊列。
- **Synchronization**: 
  - 使用 `atomicAdd` 管理隊列指標 (Head/Tail/Count)。
  - 使用 `memoryBarrierBuffer()` 與 `coherent` 修飾符確保數據一致性。
- **Control Block**: 全局狀態控制（停止信號、處理計數、進度監控）。

## 3. 數據結構 (Data Structures)
### ControlBlock (SSBO)
- `head`: 消費者讀取位置 (uint32)
- `tail`: 生產者寫入位置 (uint32)
- `count`: 當前存活任務數 (uint32)
- `stopFlag`: 由 Host 控制的終止信號 (uint32)
- `processedCount`: 已處理任務總數 (uint32)

### Task (SSBO)
- `payload`: 任務數據結構（初版設計為簡單的 `uint32[4]`）

## 4. 開發階段 (Development Phases)

### Phase 1: Foundation (基礎設施)
- [ ] 建立 `examples/workgraph_poc` 資料夾。
- [ ] 設定 CMake 構建腳本。
- [ ] 實現簡單的 Producer 生成、Consumer 消費的線性邏輯（非持久化）。
- [ ] 驗證原子操作與內存一致性。

### Phase 2: Persistent Threading & Flow Control (持久化與流控)
- [ ] 修改 Shader 加入 `while` 永續循環。
- [ ] 實現基於 Occupancy 的工作組分配（避免死鎖）。
- [ ] 實現 Host 端的實時狀態監控（Persistent Mapping）。

### Phase 3: Node Network (多節點網絡)
- [ ] 擴展隊列，支持多級任務流 (Node A -> Node B)。
- [ ] 實現 Branching 邏輯（一個任務觸發多個子任務）。

### Phase 4: Optimization (性能優化)
- [ ] 使用 Subgroup 操作加速同步。
- [ ] 針對 RX 7900 XTX 進行 Occupancy 與吞吐量調優。

## 5. 運行環境 (Environment)
- **GPU**: AMD Radeon RX 7900 XTX
- **OS**: Windows 10/11
- **Vulkan SDK**: 1.4.304.1
