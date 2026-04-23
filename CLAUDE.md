# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Purpose

This is a **Vulkan Persistent Thread Producer-Consumer Network POC**, forked from SaschaWillems/Vulkan. The research goal is implementing a Work Graph analogue on Vulkan using persistent compute shaders — measuring scheduling overhead and on-chip memory utilization on AMD RX 7900 XTX.

The POC lives in `examples/workgraph_poc/`. The rest of the repo is the upstream Sascha Willems Vulkan sample library, used as build infrastructure and reference. Do not modify upstream files unless necessary.

## Build & Run

Requires: CMake 3.10+, Vulkan SDK 1.4.304.1, MSVC (Visual Studio 2022), C++20.

```bash
# Configure (from repo root)
cmake -G "Visual Studio 17 2022" -A x64 -B build

# Build the POC target
cmake --build build --target workgraph_poc --config Release

# Run (from repo root, so shader paths resolve correctly)
build/bin/Release/workgraph_poc.exe
```

### Recompiling shaders

SPIR-V must be regenerated manually after editing GLSL sources:

```bash
# Compute shader
glslangValidator -V shaders/glsl/workgraph_poc/headless.comp -o shaders/workgraph_poc/headless.comp.spv

# Graphics shaders
glslangValidator -V shaders/glsl/workgraph_poc/koch.vert -o shaders/workgraph_poc/koch.vert.spv
glslangValidator -V shaders/glsl/workgraph_poc/koch.frag -o shaders/workgraph_poc/koch.frag.spv
```

Compiled `.spv` files are committed alongside GLSL sources. Both must stay in sync.

## Architecture of the POC (`examples/workgraph_poc/`)

### Current state: Koch Snowflake — per-frame compute + render

The POC generates a Koch snowflake fractal (MAX_DEPTH=4, 768 edges, 1536 vertices) via persistent compute threads **every frame**, then renders it as a windowed line-segment display. Compute runs inside the render loop so that frame time reflects compute cost and GPU profiling tools (e.g. Nsight Graphics) can capture the full pipeline.

### Host side (`workgraph_poc.cpp`)

`VulkanExample` inherits from `VulkanExampleBase` (windowed mode). It uses the base class's swapchain, render pass, and frame management.

**Startup flow**: `prepare()` → `prepareCompute()` → `prepareGraphicsPipeline()` → `prepared = true`

**Per-frame flow**: `render()` → `prepareFrame()` → `buildCommandBuffers()` → `submitFrame()`

#### `prepareCompute()` — one-time resource setup

Creates all compute resources that persist across frames (nothing is destroyed per-frame):

- **Four SSBOs** (all `DEVICE_LOCAL`):
  - `controlBuf` → `ControlBlock`: two `QueueControl` (head/tail/count) + `stopFlag` + `totalProcessed` + `vertexCount` + `seedDone`
  - `q1Buf` / `q2Buf` → ring buffers of `Task` (6×uint32), `QUEUE_SIZE = 4096`
  - `vertexBuffer` → output `vec2[]`, dual-usage `STORAGE_BUFFER | VERTEX_BUFFER`
- SSBOs use `TRANSFER_DST_BIT` for per-frame `vkCmdFillBuffer` reset
- **Descriptor set**: 4 SSBO bindings (control, q1, q2, output), allocated once
- **Specialization constants** (constant_id 0–3): `QUEUE_SIZE`, `MAX_DEPTH`, `NODE_C_START`, `MAX_DEPTH_EDGES` (= `EXPECTED_EDGES` = 768)
- **Pipeline**: single compute pipeline with specialization constants baked in

#### `buildCommandBuffers()` — per-frame command buffer

Each frame records a single command buffer with four phases:

1. **GPU Reset** — `vkCmdFillBuffer` zeroes `controlBuf`, `q1Buf`, `q2Buf` (no CPU↔GPU sync needed)
2. **Barrier** — transfer write → compute shader read/write (`TRANSFER_BIT` → `COMPUTE_SHADER_BIT`)
3. **Compute Dispatch** — `vkCmdDispatch(8, 1, 1)` — 256 persistent threads run Koch subdivision to completion
4. **Barrier** — compute shader write → vertex attribute read (`COMPUTE_SHADER_BIT` → `VERTEX_INPUT_BIT`)
5. **Render Pass** — `vkCmdDraw(EXPECTED_VERTICES, 1, 0, 0)` with hardcoded vertex count (1536, deterministic)

Vertex count is hardcoded as `EXPECTED_VERTICES` — no CPU readback needed since Koch snowflake output is deterministic.

#### GPU stop condition (shader)

The GPU stops itself — there is **no CPU polling loop**. Node C increments `totalProcessed` via `atomicAdd` for each edge written. When `processed >= MAX_DEPTH_EDGES` (specialization constant = 768), Node C executes `atomicExchange(control.stopFlag, 1u)`. All threads exit their `while (stopFlag == 0)` loop.

#### `prepareGraphicsPipeline()` — graphics pipeline setup

Builds a simple graphics pipeline:
- Topology: `VK_PRIMITIVE_TOPOLOGY_LINE_LIST`
- Vertex input: `vec2` per vertex (stride = 8 bytes), location 0
- Shaders: `workgraph_poc/koch.vert.spv`, `workgraph_poc/koch.frag.spv`
- No descriptor sets (vertex data is self-contained in `vertexBuffer`)

### Shader side (`shaders/glsl/workgraph_poc/headless.comp`)

`local_size_x = 32`. **Workgroup-level role assignment** (no divergence within a wavefront):

| Workgroup | Role | Queue interaction |
|---|---|---|
| WG 0 | **Node A + B** | Thread 0 seeds 3 edges once (`seedDone` guarded by `atomicCompSwap`), then all threads do Node B |
| WG 1 to NODE_C_START-1 | **Node B** (Subdivider) | Pop Q1 → Koch subdivide → push 4 sub-edges back to Q1 (depth < MAX_DEPTH) or forward to Q2 (depth == MAX_DEPTH) |
| WG NODE_C_START to end | **Node C** (Writer) | Pop Q2 → write 2×vec2 to output buffer → if totalProcessed ≥ MAX_DEPTH_EDGES, set stopFlag |

All SSBO buffers declared `coherent`. Per-slot ready flag (`payload[5]`) uses **atomic operations** (`atomicExchange` for write, `atomicCompSwap` for read+clear) to guarantee cross-CU visibility on RDNA 3.

## Cross-Workgroup Memory Model (RDNA 3)

Critical lesson learned — when persistent threads communicate across workgroups (different CUs):

| Access type | Same Wavefront | Cross-CU |
|---|---|---|
| Plain load/store | Visible (SIMD serialization) | **Unreliable** |
| `coherent` load/store | Visible | Bypasses L0/L1, **mostly visible but not guaranteed** |
| Atomic (CAS/Exchange) | Visible | **Guaranteed** (L2 global atomics path) |

**Rule**: All shared state in cross-workgroup persistent thread communication must use atomic operations. `coherent` alone is insufficient for reliable store visibility on RDNA 3.

See `weekly_report_260416.md` Section III for detailed bug analysis (3 bugs uncovered and fixed).

## Key Constraints

- **`QUEUE_SIZE`, `MAX_DEPTH`, `NODE_C_START`, `MAX_DEPTH_EDGES`** must match between C++ (`#define`) and shader (specialization constants, constant_id 0–3).
- **Task `payload[6]`** layout: `[0-3]` = floatBitsToUint(p1.x, p1.y, p2.x, p2.y), `[4]` = depth, `[5]` = per-slot ready flag (atomic). Struct size must match C++/GLSL.
- **Queue buffers are zero-reset every frame** via `vkCmdFillBuffer` on the GPU (ready flags start at 0).
- **All SSBOs are `DEVICE_LOCAL`** with `TRANSFER_DST_BIT` for GPU-side reset. No `HOST_VISIBLE` memory is used.
- **`vertexBuffer`** is created during `prepareCompute()` and persists for the lifetime of the application.
- **`vks::initializers::*`** helpers (from `base/VulkanInitializers.hpp`) are used for all Vulkan info structs.
- **`VK_CHECK_RESULT`** (from `base/VulkanTools.h`) wraps every Vulkan call.
- GLSL sources: `shaders/glsl/workgraph_poc/`. Compiled SPIR-V: `shaders/workgraph_poc/`.

## Development Roadmap (see PLAN.md)

- **Phase 1 & 2**: Done — persistent threading, CAS-based queue, CPU monitoring.
- **Phase 3**: Done — Node A → B → C pipeline with feedback loop and workgroup-level roles. Koch snowflake generation verified (768 edges exact).
- **Phase 4 Part 1**: Done — GPU self-termination (no CPU polling), graphics pipeline renders Koch snowflake as line segments in a windowed app. Per-frame compute integrated into render loop (`vkCmdFillBuffer` reset → compute dispatch → pipeline barrier → render pass), capturable by Nsight Graphics. All buffers `DEVICE_LOCAL`.
- **Phase 4 Part 2 (next)**: GPU timestamp queries (`vkCmdWriteTimestamp`) for compute timing; CAS failure counters; LDS (shared memory) queues for intra-workgroup communication; subgroup optimization for RX 7900 XTX occupancy tuning.
