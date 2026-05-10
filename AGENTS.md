# AGENTS.md

Guidance for Codex agents working in this repository.

## Scope

This repository is a Vulkan persistent-thread producer-consumer network POC,
forked from SaschaWillems/Vulkan. The research target is a Vulkan analogue of
D3D Work Graphs using persistent compute shaders on AMD RX 7900 XTX.

- Active POC code: `examples/workgraph_poc/`
- Runtime GLSL/SPIR-V path: `shaders/glsl/workgraph_poc/`
- Reports, decks, screenshots: `reports/`
- Report helper scripts: `tools/reports/`

Treat the rest of the repository as upstream Sascha Willems infrastructure and
reference code. Do not modify upstream files unless the requested change
genuinely requires it.

## Local Index

- `examples/workgraph_poc/AGENTS.md`: host-side frame flow, resource ownership,
  deterministic draw invariants.
- `shaders/glsl/workgraph_poc/AGENTS.md`: shader queue model, workgroup roles,
  RDNA 3 memory visibility rules.
- `reports/README.md`: report taxonomy (分類方式), deck locations, maintenance
  rules.
- `reports/PPTX_NAMING.md`: formal PowerPoint naming convention.

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
`build_and_run_workgraph_poc.bat` wraps configure, build, and run for Windows.

## Shader Rebuilds

The runtime default is `shaderDir = "glsl"`, so the app loads SPIR-V from
`shaders/glsl/workgraph_poc/`. After executable GLSL changes, regenerate the
committed SPIR-V files manually:

```bash
glslangValidator -V shaders/glsl/workgraph_poc/headless.comp -o shaders/glsl/workgraph_poc/headless.comp.spv
glslangValidator -V shaders/glsl/workgraph_poc/koch.vert -o shaders/glsl/workgraph_poc/koch.vert.spv
glslangValidator -V shaders/glsl/workgraph_poc/koch.frag -o shaders/glsl/workgraph_poc/koch.frag.spv
```

`shaders/workgraph_poc/headless.comp.spv` is a legacy mirror of the compute
shader binary. Keep it in sync while it remains in the repository.

## Global Rules

- Prefer changes inside `examples/workgraph_poc/` and
  `shaders/glsl/workgraph_poc/`.
- Keep persistent-thread shared state atomic across workgroups; `coherent` alone
  is not enough for RDNA 3 cross-CU correctness.
- Avoid CPU-GPU synchronization in the hot frame path unless required.
- Preserve the profiling-friendly frame structure:
  GPU reset -> compute dispatch -> barrier -> render pass.
- Keep report assets out of the repository root; use `reports/` and
  `tools/reports/` instead.

## Profiling Analysis Rules

- Do not force-fit explanations for profiling results. If the evidence is
  incomplete, say what is unknown and propose a targeted check instead of
  filling the gap with a plausible story.
- Keep observations, hypotheses, and conclusions separate. Mark speculation
  explicitly, and only promote it to a conclusion after checking code, trace
  settings, and controlled measurements.
- Before attributing unchanged duration to a new bottleneck, first rule out
  measurement artifacts such as enabled metrics, shader instrumentation,
  synchronization, frame selection, or profiler sampling scope.
- If a later measurement fix contradicts an earlier report explanation, update
  the report rather than preserving the old interpretation.
