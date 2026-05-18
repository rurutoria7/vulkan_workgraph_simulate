# AGENTS.md

Shader guidance for `shaders/glsl/workgraph_poc/`.

## Shader Model

Main compute shader: `headless.comp`.

- `local_size_x = 32`
- Node A seeds the three initial snowflake edges once.
- Node B consumes Q1, subdivides edges, and feeds Q1 or Q2.
- Node C consumes Q2, writes output vertices, and sets `stopFlag` after
  `MAX_DEPTH_EDGES` final edges.
- The GPU self-terminates; do not introduce CPU polling unless explicitly
  requested.

## Queue And Task Layout

`Task.payload[6]` is shared ABI with C++:

- `payload[0..3]`: `floatBitsToUint(p1.x, p1.y, p2.x, p2.y)`
- `payload[4]`: depth
- `payload[5]`: per-slot ready flag

Queue buffers are zero-reset by the host command buffer every frame.

## Memory Model

Persistent threads communicate across workgroups and CUs on RDNA 3. Plain
loads/stores are not reliable for cross-CU visibility, and `coherent` alone is
not enough for correctness.

- Use atomics for queue counts, heads, tails, stop flags, and ready flags.
- Producers publish payload data, call `memoryBarrierBuffer()`, then use
  `atomicExchange(payload[5], 1u)`.
- Consumers claim ready slots with `atomicCompSwap(payload[5], 1u, 0u)`.

## Rebuild Rules

After executable GLSL changes, rebuild committed SPIR-V:

```bash
glslangValidator -V shaders/glsl/workgraph_poc/headless.comp -o shaders/glsl/workgraph_poc/headless.comp.spv
glslangValidator -V shaders/glsl/workgraph_poc/koch.vert -o shaders/glsl/workgraph_poc/koch.vert.spv
glslangValidator -V shaders/glsl/workgraph_poc/koch.frag -o shaders/glsl/workgraph_poc/koch.frag.spv
```

Keep `shaders/workgraph_poc/headless.comp.spv` in sync with
`shaders/glsl/workgraph_poc/headless.comp.spv` while the legacy mirror exists.
