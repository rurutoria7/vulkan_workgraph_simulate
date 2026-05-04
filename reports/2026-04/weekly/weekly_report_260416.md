# Weekly Report — 2026/04/16

## 專案名稱
Vulkan Persistent Thread Producer-Consumer Network POC

## 研究定位（更正版）

本研究定位更正為：基於 software 實作盡可能快的 producer-consumer 架構。重點不是單純模仿 D3D Work Graphs 的 API，而是透過實作、profiling 與 metrics 找出瓶頸，並提出可落地的 optimization 方向。

## 研究意義

這能讓 MTK 預先知道：若未來要在 mobile GPU 上實現 producer-consumer，主要瓶頸可能會落在哪裡，例如 atomic contention、global memory traffic、queue pressure、producer/consumer imbalance、memory visibility 與 spin-wait，進而提供 runtime、driver、hardware 的優化方向。

## 本週工作摘要

本週完成了兩項重要工作：（1）將科赫雪花（Koch Snowflake）作為具體的計算任務接入 persistent thread 管線，驗證 branching（1→4 子任務）的 feedback loop 機制；（2）將線程角色分配從 thread-level 重構為 workgroup-level，消除 wavefront 內的 branch divergence。過程中發現並解決了三個跨 workgroup 記憶體可見性問題。

---

### 一、科赫雪花：具體任務接入（Phase 3 收尾）

**任務定義**
- 以等邊三角形為起始，每條邊進行 Koch 細分：`P1→P2` 分裂為 `P1→A, A→C, C→B, B→P2`，其中 `C = A + rotate(dir, +60°)`。
- `MAX_DEPTH=4` 時，3 條初始邊遞迴產生 3×4⁴ = 768 條最終邊，對應 1536 個頂點。

**資料結構擴展**
- `Task.payload` 從 `uint[4]` 擴展為 `uint[6]`：`[0-3]` = floatBitsToUint(p1.x, p1.y, p2.x, p2.y)，`[4]` = depth，`[5]` = per-slot ready flag。
- `ControlBlock` 新增 `vertexCount`（output buffer 原子寫入計數器）與 `seedDone`（播種一次性保護）。
- 新增第四個 SSBO（binding 3）：output vertex buffer，Line List 格式（`vec2[]`）。

**三節點 + Feedback Loop**
```
Node A --seed 3 edges--> Queue 1 --+--> Node B --subdivide--> Queue 1 (feedback, depth < 4)
                                    +--> Node B --forward----> Queue 2 (depth == 4)
                                                               Queue 2 --> Node C --write--> Output Buffer
```
- Node A 只播種一次（`atomicCompSwap(seedDone, 0, 1)` 保護），之後不再產生任務。
- Node B 從 Q1 取出邊：若 `depth < MAX_DEPTH`，細分為 4 條子邊推回 Q1（branching）；若 `depth == MAX_DEPTH`，轉發到 Q2。
- Node C 從 Q2 取出最終邊，用 `atomicAdd(vertexCount, 2)` 分配 output slot，寫入 2 個 `vec2`。
- Host 端透過 specialization constants 傳入 `QUEUE_SIZE`（4096）和 `MAX_DEPTH`（4）。

**驗證結果**
- 初版（thread-level 角色）：正確產生 768 邊 / 1536 頂點，耗時 ~2.6 秒（含 CPU 10ms polling 開銷）。

---

### 二、Workgroup-Level 角色分配（消除 Divergence）

**問題：Wavefront 內部的 Branch Divergence**
- 原設計以 `gId % 32` 分配角色，同一 wavefront（RDNA 3 wave32 = 32 threads）的線程走 3 條不同分支。
- GPU 必須依序執行每條分支並 mask 其他線程，每個 cycle 浪費大量 SIMD lane。

**重構方案**
- 角色改由 `gl_WorkGroupID.x` 決定，同一 workgroup 內所有線程做相同工作。
- Dispatch 從 `(32,1,1)` 縮減為 `(8,1,1)`：WG 0-5 做 Node B（Subdivider），WG 6-7 做 Node C（Writer）。
- Node A 的播種在 main loop 之前由 WG 0 thread 0 執行，之後 WG 0 加入 Node B 工作。
- 新增 specialization constant `NODE_C_START`（constant_id=2），控制 Node C 的起始 workgroup index。

---

### 三、跨 Workgroup 記憶體可見性問題（三個 Bug）

重構為 workgroup-level 角色後，producer（Node B）與 consumer（Node C）分別運行在不同 CU 上，連續暴露了三個記憶體可見性 bug。這些 bug 在 thread-level 角色時被同一 wavefront 的隱式序列化掩蓋。

#### Bug 1：缺少 `coherent` 修飾符

**症狀**：`Q2: 768` 但 `Processed: 0`、`Vertices: 0` — Node C 完全無法消費 Q2 的任務。

**原因**：Buffer 沒有宣告 `coherent`。非原子讀取（如 `uint cur = control.q2.count;`）在 Node C 的 CU 上從本地 L0/L1 cache 讀到舊值 0，永遠不進入消費邏輯。在同一 wavefront 內，所有線程共享 L0 cache，所以 Node B 的原子寫入對 Node C 立即可見；跨 CU 後 L0/L1 不共享。

**修復**：所有 SSBO 加上 `coherent` 修飾符，強制 load/store 穿透 L0/L1 直接走 L2。

#### Bug 2：Producer-Consumer 資料競爭（count 先於 data）

**症狀**：預期 768 邊，實際產生 960 邊（多了 192 = 3×4³），且第一筆頂點為 (0,0)→(0,0)。

**原因**：Queue 協議中，producer 先 CAS increment `count`，再 `atomicAdd(tail)` 取得 slot，最後寫入 data。Consumer 看到 `count > 0` 後 CAS decrement 並取得同一 slot，但 producer 尚未完成資料寫入，consumer 讀到舊的/未初始化的資料。

- 讀到 `depth=4` 的資料被誤判為更低 depth → 多了不該存在的細分。
- 讀到全 0 資料 → (0,0)→(0,0) 的垃圾頂點。

**修復**：利用 `payload[5]` 作為 per-slot ready flag。Producer 寫完 payload[0-4] 後設 `payload[5] = 1`；consumer 在讀取前 spin 等待 `payload[5] == 1`，讀取後設回 0。Host 端同時 zero-initialize queue buffers。

#### Bug 3：Plain store/load 的 ready flag 不可靠

**症狀**：511/768 邊成功處理後卡住，Q1: 0、Q2: 0 — 257 個任務「消失」（count 已扣但 consumer 等不到 ready flag）。

**原因**：`payload[5] = 1u` 是 plain store。即使 buffer 宣告為 `coherent`，RDNA 3 的 plain store 可能停留在 store buffer 或 write-combining buffer 中，不一定及時通過 L2 atomics unit 對其他 CU 可見。同樣，consumer 的 plain load 也可能讀到 stale 值。

**修復**：ready flag 的讀寫全部改用原子操作：
- Producer：`atomicExchange(payload[5], 1u)` 取代 plain store。
- Consumer：`atomicCompSwap(payload[5], 1u, 0u)` 取代 plain load + store，一步完成「檢查 ready + 清除 flag」。
- 原子操作走 L2 global atomics path，保證跨 CU 的即時可見性。

#### 記憶體模型總結

| 存取方式 | 同一 Wavefront 內 | 同一 CU 內（跨 Wavefront） | 跨 CU |
|---|---|---|---|
| Plain load/store | 隱式可見（SIMD 序列化） | L0 cache 共享，通常可見 | **不可靠**，需 coherent |
| Coherent load/store | 可見 | 可見 | 穿透 L0/L1，**大多數情況可見** |
| Atomic（CAS/Exchange） | 可見 | 可見 | 走 L2 atomics，**保證可見** |

**結論**：在 persistent thread 跨 workgroup 通訊中，所有共享狀態的讀寫都應使用原子操作，`coherent` + plain store/load 不夠可靠。

---

### 四、最終程式結構

| 檔案 | 說明 |
|---|---|
| `examples/workgraph_poc/workgraph_poc.cpp` | Host 端：4 個 SSBO、3 個 specialization constants、自動偵測完成 |
| `shaders/glsl/workgraph_poc/headless.comp` | Shader：coherent buffers、atomic ready flag、workgroup-level 角色 |
| `shaders/workgraph_poc/headless.comp.spv` | 編譯後的 SPIR-V |

**Dispatch**：`vkCmdDispatch(8, 1, 1)` — 8 workgroups × 32 threads = 256 threads
- WG 0-5：Node B（Subdivider），其中 WG 0 兼任播種
- WG 6-7：Node C（Writer）

**驗證結果**：768 邊 / 1536 頂點精確產生，無垃圾資料，Q1/Q2 正確歸零，耗時 ~2.76 秒。

---

## 現階段已知問題與風險

1. **Node B 的 feedback loop 在高 MAX_DEPTH 時可能 deadlock**：若 Q1 滿了而所有 Node B 線程都在 pushQ1 自旋等待，沒有線程能消費 Q1 騰出空間。目前 QUEUE_SIZE=4096 對 MAX_DEPTH=4 有足夠餘量，但 MAX_DEPTH=6+ 可能有問題。
2. **CAS 競爭開銷未量化**：尚無 timestamp query 或 per-thread failure counter。
3. **全部在 global memory**：佇列仍位於 SSBO，producer-consumer handoff 可能受 global memory traffic 與 queue contention 限制。
4. **線程角色靜態分配**：WG 0-5 vs WG 6-7 的比例固定，無 work stealing。
5. **渲染管線尚未實現**：output vertex buffer 目前僅由 CPU 讀回驗證。

## 下週計畫

1. **Phase 2 — 渲染管線**：
   - 新增 graphics pipeline，將 output vertex buffer 直接渲染為線段（`VK_PRIMITIVE_TOPOLOGY_LINE_LIST`）。
   - 可能需要從 headless 切換為 windowed 模式，或渲染到 offscreen framebuffer 再輸出為圖片。

2. **性能量測**：
   - 加入 `vkCmdWriteTimestamp` 量測 GPU 端實際計算時間（排除 CPU polling 開銷）。
   - 量測 CAS 失敗率。

3. **Phase 4 探索 — LDS 佇列**：
   - 評估將 intra-workgroup 通訊移至 shared memory 的可行性，確認這是否能降低 producer-consumer 的 global queue traffic。
