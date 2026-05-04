# AGENTS.md

Guidance for Codex agents working in this repository.

## Project Scope

This repository is a Vulkan persistent-thread producer-consumer network POC,
forked from SaschaWillems/Vulkan. The research target is a Vulkan analogue of
D3D Work Graphs using persistent compute shaders, with scheduling overhead and
on-chip memory behavior measured on AMD RX 7900 XTX.

The active POC code lives in `examples/workgraph_poc/`. Treat the rest of the
repository as upstream Sascha Willems infrastructure and reference code. Do not
modify upstream files unless the requested change genuinely requires it.

Report decks, screenshots, and profiling notes live under `reports/`. Helper
scripts for report generation live under `tools/reports/`.

## Build And Run

Requirements:

- CMake 3.10+
- Vulkan SDK 1.4.304.1
- MSVC / Visual Studio 2022
- C++20

Common commands from the repository root:

```bash
cmake -G "Visual Studio 17 2022" -A x64 -B build
cmake --build build --target workgraph_poc --config Release
build/bin/Release/workgraph_poc.exe
```

Run the executable from the repository root so shader paths resolve correctly.

## Shader Rebuilds

The runtime default is `shaderDir = "glsl"`, so the app loads SPIR-V from
`shaders/glsl/workgraph_poc/`. After editing GLSL sources, regenerate the
committed SPIR-V files manually:

```bash
glslangValidator -V shaders/glsl/workgraph_poc/headless.comp -o shaders/glsl/workgraph_poc/headless.comp.spv
glslangValidator -V shaders/glsl/workgraph_poc/koch.vert -o shaders/glsl/workgraph_poc/koch.vert.spv
glslangValidator -V shaders/glsl/workgraph_poc/koch.frag -o shaders/glsl/workgraph_poc/koch.frag.spv
```

`shaders/workgraph_poc/headless.comp.spv` is a legacy mirror of the compute
shader binary. Keep it in sync while it remains in the repository.

## POC Architecture

Current state: Koch snowflake generation and line rendering at depth 8.

- `VulkanExample` inherits from `VulkanExampleBase` and uses the base swapchain,
  render pass, and frame management.
- Startup flow: `prepare()` -> `prepareCompute()` -> `prepareGraphicsPipeline()`
  -> `prepared = true`.
- Per-frame flow: `render()` -> `prepareFrame()` -> `buildCommandBuffers()` ->
  `submitFrame()`.
- Compute runs every frame inside the render loop so profiling tools can capture
  reset, dispatch, synchronization, and render in one frame.
- Output is deterministic: `MAX_DEPTH=8`, 196,608 edges, 393,216 vertices.

`prepareCompute()` creates persistent compute resources:

- `controlBuf`: `ControlBlock` with two `QueueControl` values, `stopFlag`,
  `totalProcessed`, `vertexCount`, and `seedDone`.
- `q1Buf` / `q2Buf`: ring buffers of `Task`, where each task is six `uint32`
  values.
- `vertexBuffer`: output `vec2[]`, used as both storage and vertex buffer.
- All compute buffers are `DEVICE_LOCAL` and include `TRANSFER_DST_BIT` for
  GPU-side reset via `vkCmdFillBuffer`.
- Descriptor set has four SSBO bindings: control, q1, q2, output.
- Specialization constants use constant IDs 0-3: `QUEUE_SIZE`, `MAX_DEPTH`,
  `NODE_C_START`, `MAX_DEPTH_EDGES`.

`buildCommandBuffers()` records per-frame work:

- Zero `controlBuf`, `q1Buf`, and `q2Buf` with `vkCmdFillBuffer`.
- Barrier from transfer write to compute shader read/write.
- Dispatch compute with `vkCmdDispatch(96, 1, 1)`.
- Barrier from compute shader write to vertex input read.
- Render with `VK_PRIMITIVE_TOPOLOGY_LINE_LIST` and hardcoded
  `EXPECTED_VERTICES`.

## Shader Model

Main shader: `shaders/glsl/workgraph_poc/headless.comp`.

- `local_size_x = 32`.
- `QUEUE_SIZE = 196608`, `MAX_DEPTH = 8`, `NODE_C_START = 72`,
  `MAX_DEPTH_EDGES = 196608`.
- Workgroup 0 performs Node A seed work and Node B subdivision.
- Workgroups 1 to `NODE_C_START - 1` perform Node B subdivision.
- Workgroups `NODE_C_START` and later perform Node C output writes.
- Node C increments `totalProcessed`; once processed edges reach
  `MAX_DEPTH_EDGES`, it sets `stopFlag`.
- The GPU self-terminates. Do not add CPU polling unless explicitly requested.

## Memory Model Constraints

Persistent threads communicate across workgroups and CUs on RDNA 3. Plain
loads/stores are not reliable for cross-CU visibility, and `coherent` alone is
not enough for correctness.

Use atomic operations for all shared state involved in cross-workgroup
communication. In particular, the per-slot ready flag in `Task.payload[5]` must
use atomic exchange / compare-and-swap semantics.

## Key Invariants

- `QUEUE_SIZE`, `MAX_DEPTH`, `NODE_C_START`, and `MAX_DEPTH_EDGES` must match
  between C++ defines and GLSL specialization constants.
- `Task.payload[6]` layout is:
  - `payload[0..3]`: `floatBitsToUint(p1.x, p1.y, p2.x, p2.y)`
  - `payload[4]`: depth
  - `payload[5]`: per-slot ready flag
- Queue buffers are zero-reset on the GPU every frame.
- No `HOST_VISIBLE` memory is used for the compute buffers.
- `vertexBuffer` is created in `prepareCompute()` and lives for the application
  lifetime.
- Prefer `vks::initializers::*` helpers for Vulkan info structs.
- Wrap Vulkan calls with `VK_CHECK_RESULT`.
- GLSL sources and default runtime SPIR-V live in
  `shaders/glsl/workgraph_poc/`.

## Development Roadmap

Completed:

- Persistent threading and CAS-based queues.
- Node A -> B -> C pipeline with feedback loop.
- Koch snowflake generation at depth 8.
- GPU self-termination.
- Windowed graphics pipeline rendering the snowflake per frame.
- Per-frame compute reset and dispatch integrated into the render loop.
- All compute buffers moved to `DEVICE_LOCAL` memory.

Next likely work:

- GPU timestamp queries with `vkCmdWriteTimestamp`.
- CAS failure counters.
- LDS/shared-memory queues for intra-workgroup communication.
- Subgroup optimization and RX 7900 XTX occupancy tuning.

## Working Rules

- Prefer changes inside `examples/workgraph_poc/` and
  `shaders/glsl/workgraph_poc/`.
- If editing GLSL, rebuild SPIR-V or clearly report that it was not rebuilt.
- Keep the deterministic Koch vertex count path unless the user asks for a
  dynamic-count readback or indirect draw path.
- Avoid CPU-GPU synchronization in the hot frame path unless required by the
  task.
- Preserve the current profiling-friendly frame structure:
  GPU reset -> compute dispatch -> barrier -> render pass.
