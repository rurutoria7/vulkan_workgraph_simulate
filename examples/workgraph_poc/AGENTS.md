# AGENTS.md

Host-side guidance for `examples/workgraph_poc/`.

## Current State

This example renders a deterministic Koch snowflake generated every frame by a
persistent compute dispatch.

- `MAX_DEPTH = 8`
- `EXPECTED_EDGES = 3 * 4^MAX_DEPTH = 196608`
- `EXPECTED_VERTICES = EXPECTED_EDGES * 2 = 393216`
- `QUEUE_SIZE = EXPECTED_EDGES`
- `NUM_WORKGROUPS = 96`
- `NODE_C_START = 72`

## Frame Flow

- Startup: `prepare()` -> `prepareCompute()` -> `prepareGraphicsPipeline()` ->
  `prepared = true`.
- Per frame: `render()` -> `prepareFrame()` -> `buildCommandBuffers()` ->
  `submitFrame()`.
- Command buffer order: GPU reset -> transfer-to-compute barrier -> compute
  dispatch -> compute-to-vertex barrier -> render pass.

## Resource Rules

- `controlBuf`, `q1Buf`, and `q2Buf` are `DEVICE_LOCAL` SSBOs with
  `TRANSFER_DST_BIT` for per-frame `vkCmdFillBuffer` reset.
- `vertexBuffer` is `DEVICE_LOCAL` and has storage + vertex usage.
- Descriptor bindings are fixed: control = 0, q1 = 1, q2 = 2, output = 3.
- Specialization constants IDs are fixed: queue size = 0, max depth = 1,
  Node C start = 2, max depth edges = 3.

## Invariants

- Keep C++ constants in sync with `shaders/glsl/workgraph_poc/headless.comp`.
- Keep `Task.payload[6]` layout identical to GLSL.
- Keep `vkCmdDraw(EXPECTED_VERTICES, 1, 0, 0)` unless the task explicitly needs
  dynamic-count readback or indirect draw.
- Do not add CPU polling for completion in the hot frame path.
