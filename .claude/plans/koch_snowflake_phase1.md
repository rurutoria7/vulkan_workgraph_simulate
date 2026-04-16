# 科赫雪花 Phase 1：Persistent Thread 產生頂點資料

## 概述

利用現有的 persistent thread producer-consumer 架構，將科赫雪花的遞迴細分映射為工作圖：
- **Node A**（Producer）：播種初始等邊三角形的 3 條邊
- **Node B**（Subdivider）：取出邊 → 若 depth < MAX_DEPTH 則細分為 4 條子邊並**回推到 Queue 1**（feedback loop）；若 depth == MAX_DEPTH 則轉發到 Queue 2
- **Node C**（Writer）：從 Queue 2 取出最終邊，寫入 Output Vertex Buffer

這同時實現了 PLAN.md Phase 3 尚未完成的 **branching 邏輯**（1 個任務 → 4 個子任務）。

## 參數

- **MAX_DEPTH = 4**：3×4^4 = 768 條最終邊，1536 個頂點
- **輸出格式**：Line List（每條邊 2 個 vec2，渲染用 VK_PRIMITIVE_TOPOLOGY_LINE_LIST）
- **QUEUE_SIZE**：512 → 4096（容納細分過程中的峰值任務數）

## 科赫細分算法

對於邊 P1→P2：
```
dir = (P2 - P1) / 3
A = P1 + dir
B = P1 + 2 * dir
C = A + rotate(dir, +60°)
  C.x = A.x + dir.x * cos(60°) - dir.y * sin(60°)
  C.y = A.y + dir.x * sin(60°) + dir.y * cos(60°)

產生 4 條新邊：P1→A, A→C, C→B, B→P2
```

## 資料結構修改

### Task 結構擴展（C++ 與 GLSL 必須同步）

```cpp
// C++ 端
struct Task {
    uint32_t payload[6];  // 從 [4] 擴展為 [6]
    // [0]: floatBitsToUint(p1.x)
    // [1]: floatBitsToUint(p1.y)
    // [2]: floatBitsToUint(p2.x)
    // [3]: floatBitsToUint(p2.y)
    // [4]: depth
    // [5]: padding
};
```

### ControlBlock 擴展

```cpp
struct ControlBlock {
    QueueControl q1;
    QueueControl q2;
    uint32_t stopFlag;
    uint32_t totalProcessed;
    uint32_t vertexCount;   // 新增：output buffer 原子寫入計數器
    uint32_t seedDone;      // 新增：確保播種只執行一次
};
```

### 新增 Output Buffer（binding 3）

```glsl
layout(std430, binding = 3) buffer OutputVertices {
    vec2 vertices[];  // Line List：每 2 個 vec2 = 一條線段
} output_vb;
```

大小：768 邊 × 2 頂點 × 8 bytes = 12,288 bytes

## Shader 執行緒角色

保持 `local_size_x = 32`，調整分配比例：

| 執行緒 | 角色 | 說明 |
|---|---|---|
| `gId % 32 == 0` | Node A | 播種 3 條初始邊，用 atomicCompSwap(seedDone) 確保只做一次 |
| `1 ≤ gId % 32 < 24` | Node B | 23 個執行緒處理細分（瓶頸，4x 扇出） |
| `24 ≤ gId % 32 < 32` | Node C | 8 個執行緒寫入 output buffer |

## 終止條件

Host 端監控 `vertexCount`，當達到 1536（768×2）時設定 `stopFlag = 1`。

## 具體修改清單

### 1. `shaders/glsl/workgraph_poc/headless.comp`

- 擴展 Task struct：`uint payload[4]` → `uint payload[6]`
- 新增 `uint vertexCount` 和 `uint seedDone` 到 ControlBlock
- 新增 binding 3 output buffer
- Node A：用 `atomicCompSwap(seedDone, 0, 1)` 保護，播種等邊三角形 3 條邊到 Q1（depth=0，座標用 floatBitsToUint 打包）
- Node B：pop Q1 → 若 `depth < 4` 則計算科赫細分 4 條子邊並 push 回 Q1（feedback loop）；若 `depth == 4` 則 push 到 Q2
- Node C：pop Q2 → `atomicAdd(vertexCount, 2)` 分配 slot → 寫入 2 個 vec2 到 output buffer
- 調整執行緒角色邊界（0 / 1-23 / 24-31）

### 2. `examples/workgraph_poc/workgraph_poc.cpp`

- 擴展 `Task` struct（payload[6]）和 `ControlBlock` struct（+vertexCount, +seedDone）
- `QUEUE_SIZE` 從 512 改為 4096
- 新增 output vertex buffer（binding 3，HOST_VISIBLE | HOST_COHERENT，大小 12288 bytes）
- Descriptor set 從 3 個 SSBO 改為 4 個（新增 binding 3）
- 監控邏輯改為檢查 `vertexCount >= 1536` 後自動設定 stopFlag
- 結束後讀回 output buffer 並印出頂點座標（驗證用）

### 3. 編譯 shader

```bash
glslangValidator -V shaders/glsl/workgraph_poc/headless.comp -o shaders/workgraph_poc/headless.comp.spv
```

### 4. 修正 CLAUDE.md

- 移除「No vks::initializers」的錯誤描述（實際上有使用）
- 更新架構描述反映科赫雪花的工作流程

## 風險

- **Deadlock**：Node B 的 feedback loop 若 Q1 滿了（push 4 條子邊推不進去），會在 while 迴圈中卡住。QUEUE_SIZE=4096 對 MAX_DEPTH=4（峰值 ~768 tasks）有足夠餘量。
- **C++/GLSL 結構同步**：Task payload 大小和 ControlBlock 欄位偏移必須完全一致。
