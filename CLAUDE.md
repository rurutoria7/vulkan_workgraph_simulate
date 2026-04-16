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

### Recompiling the compute shader

SPIR-V must be regenerated manually after editing the GLSL source:

```bash
glslangValidator -V shaders/glsl/workgraph_poc/headless.comp -o shaders/workgraph_poc/headless.comp.spv
```

The compiled `.spv` is committed alongside the GLSL source. Both files must stay in sync.

## Architecture of the POC (`examples/workgraph_poc/`)

### Current task: Koch Snowflake generation

The POC generates a Koch snowflake fractal (MAX_DEPTH=4, 768 edges, 1536 vertices) using persistent compute threads. The fractal subdivision is mapped as a 3-node work graph with a feedback loop.

### Host side (`workgraph_poc.cpp`)

`VulkanExampleWorkgraphPOC` is a standalone headless Vulkan compute application. It does **not** inherit from `VulkanExampleBase` — it owns all Vulkan objects directly. It links against the `base` library for `vks::initializers::*` helpers, `VK_CHECK_RESULT` (from `VulkanTools.h`), and `CommandLineParser.hpp`.

Key Vulkan object setup:
- **Instance / Device / Queue**: Targets the first Vulkan device's compute queue family.
- **Four SSBOs** (all `HOST_VISIBLE | HOST_COHERENT`, persistently mapped):
  - `buffers.control` → `ControlBlock`: two `QueueControl` (head/tail/count) + `stopFlag` + `totalProcessed` + `vertexCount` + `seedDone`
  - `buffers.queue1` / `buffers.queue2` → ring buffers of `Task` (6×uint32), `QUEUE_SIZE = 4096` entries each, **zero-initialized**
  - `buffers.output` → output vertex buffer (`vec2[]`, Line List format)
- **Specialization constants**: `QUEUE_SIZE` (constant_id=0), `MAX_DEPTH` (constant_id=1), `NODE_C_START` (constant_id=2).
- **Dispatch**: `vkCmdDispatch(NUM_WORKGROUPS, 1, 1)` — currently 8 workgroups × 32 threads = 256 persistent threads.
- **Monitoring**: CPU polls `vertexCount` until it reaches 3×4^MAX_DEPTH×2, then sets `stopFlag`.

### Shader side (`shaders/glsl/workgraph_poc/headless.comp`)

`local_size_x = 32`. **Workgroup-level role assignment** (no divergence within a wavefront):

| Workgroup | Role | Queue interaction |
|---|---|---|
| WG 0 | **Node A + B** | Thread 0 seeds 3 edges (once, `seedDone` guarded), then all threads do Node B |
| WG 1 to NODE_C_START-1 | **Node B** (Subdivider) | Pop Q1 → Koch subdivide → push 4 sub-edges back to Q1 (feedback) or forward to Q2 |
| WG NODE_C_START to end | **Node C** (Writer) | Pop Q2 → write 2×vec2 to output buffer |

All SSBO buffers are declared `coherent`. Per-slot ready flag (`payload[5]`) uses **atomic operations** (`atomicExchange` for write, `atomicCompSwap` for read+clear) to guarantee cross-CU visibility on RDNA 3.

## Cross-Workgroup Memory Model (RDNA 3)

Critical lesson learned during development — when persistent threads communicate across workgroups (different CUs):

| Access type | Same Wavefront | Cross-CU |
|---|---|---|
| Plain load/store | Visible (SIMD serialization) | **Unreliable** |
| `coherent` load/store | Visible | Bypasses L0/L1, **mostly visible but not guaranteed** |
| Atomic (CAS/Exchange) | Visible | **Guaranteed** (L2 global atomics path) |

**Rule**: All shared state in cross-workgroup persistent thread communication must use atomic operations. `coherent` alone is insufficient for reliable store visibility on RDNA 3.

See `weekly_report_260416.md` Section III for detailed bug analysis.

## Key Constraints

- **`QUEUE_SIZE`, `MAX_DEPTH`, `NODE_C_START`** must match between C++ (`#define`) and shader (specialization constants, constant_id 0/1/2).
- **Task `payload[6]`** layout: `[0-3]` = floatBitsToUint(p1.x, p1.y, p2.x, p2.y), `[4]` = depth, `[5]` = per-slot ready flag (atomic). Struct size must match C++/GLSL.
- **Queue buffers must be zero-initialized** on the host side (ready flags start at 0).
- **`vks::initializers::*`** helpers (from `base/VulkanInitializers.hpp`) are used for creating Vulkan info structs.
- **`VK_CHECK_RESULT`** (from `base/VulkanTools.h`) wraps every Vulkan call.
- GLSL sources live in `shaders/glsl/workgraph_poc/`, compiled SPIR-V in `shaders/workgraph_poc/`.

## Development Roadmap (see PLAN.md)

- **Phase 1 & 2**: Done — persistent threading, CAS-based queue, CPU monitoring.
- **Phase 3**: Done — Node A → B → C pipeline with **feedback loop** and **workgroup-level roles**. Koch snowflake generation verified (768 edges exact).
- **Phase 4**: Next — rendering pipeline (Part 2), LDS (shared memory) queues, Subgroup optimization for RX 7900 XTX occupancy tuning.
