# RGP / Nsight Occupancy Cross-vendor Experiment Runbook

Date: 2026-05-28
Branch: `experiment/rgp-nsight-occupancy`
Baseline commit at planning time: `fb6ded10`

## Goal

Create a Chinese investigation report, with a PDF copy, that compares the same
`workgraph_poc` Vulkan persistent-thread workload on:

- AMD local machine, using RDP / RGP.
- NVIDIA Tailscale machine `sshuser@100.99.202.102`, using Nsight Graphics GPU
  Trace.

The report should include key screenshots and key data in the main text, and
place complete data, screenshots, environment details, and commands in the
appendix.

## Main Claim

The report should avoid the broad claim "RDP / RGP is inaccurate." The scoped
claim is:

> On this Vulkan persistent-thread workgraph workload, AMD RGP's Wavefront
> occupancy view may not fully represent the compute dispatch wavefront
> lifetime. Therefore, `wavefront = 0` / `in-flight threads = 0` should not be
> used alone to conclude that the shader has ended or that the remaining event
> duration is purely memory / atomic stall. NVIDIA Nsight Graphics GPU Trace on
> the same workload is used as a cross-tool, cross-vendor comparison.

## Workload Matrix

### Problem / Reproduction

Purpose: reproduce the original RGP symptom where the compute event continues
but Wavefront occupancy shows the middle / tail region as zero.

Candidate settings:

```text
--wg-timestamps-only
--wg-queue-shards 1
--wg-q2-shards 1
--wg-node-c-start 72
--wg-no-q1-lane-pop
--wg-no-q2-deq-batch
```

Notes:

- This is intended to be close to the pre-sharding / problem configuration.
- Existing May 2026 evidence for this class of run is in
  `reports/2026-05/metrics/rgp_occupancy_20260509/`.
- If the current code path cannot reproduce the old shape, document that
  explicitly and preserve the run as negative evidence instead of forcing the
  interpretation.

### Optimized / Current Main Path

Purpose: compare the currently adopted main path on both tools and GPUs.

Candidate settings:

```text
--wg-timestamps-only
--wg-queue-shards 256
--wg-q2-shards 256
--wg-node-c-start 72
--wg-q1-lane-pop
--wg-no-q2-deq-batch
```

Notes:

- This is expected to be much shorter than the reproduction workload.
- If profiler resolution makes the occupancy view hard to interpret, report it
  as a limitation rather than over-reading the trace.

## Required Pre-work

Add minimal `VK_EXT_debug_utils` command labels so RGP and Nsight can identify
the same regions:

- `WG GPU reset`
- `WG reset-to-compute barrier`
- `WG compute dispatch`
- `WG post-compute barrier`
- `WG metrics copy`
- `WG render`

Do not change shader behavior, queue behavior, timestamp definitions, or the
hot frame structure.

## Evidence To Capture

For each GPU / tool / workload:

- Exact executable path and command line.
- Git commit and branch.
- GPU name, driver version, OS version.
- App CSV from `--wg-metrics-file` or captured `WG_METRICS` stdout.
- Profiler capture file when practical.
- Timeline screenshot showing labeled compute dispatch.
- Occupancy / utilization screenshot for the compute dispatch.
- Event details screenshot showing dispatch duration and selected-range
  counters when available.

## Known NVIDIA Target

SSH target:

```text
sshuser@100.99.202.102
```

Observed environment during planning:

```text
Host: DESKTOP-VFA2S2A
OS: Microsoft Windows 10.0.26200.8390
GPU: NVIDIA GeForce RTX 2080
Driver: 591.86
VBIOS: 90.04.0b.80.1c
```

Nsight decision:

- Use Nsight Graphics GPU Trace as the NVIDIA main evidence.
- Use frame / event views only as auxiliary dispatch-location evidence.
- Do not make Nsight Compute the main experiment path unless Nsight Graphics GPU
  Trace is blocked.

## Report Location

Main report:

```text
reports/2026-05/metrics/rgp_nsight_occupancy_20260528/rgp_nsight_occupancy_cross_vendor_report_20260528.md
reports/2026-05/metrics/rgp_nsight_occupancy_20260528/rgp_nsight_occupancy_cross_vendor_report_20260528.pdf
```

Artifacts:

```text
reports/2026-05/metrics/rgp_nsight_occupancy_20260528/amd_rgp/
reports/2026-05/metrics/rgp_nsight_occupancy_20260528/nvidia_nsight/
reports/2026-05/metrics/rgp_nsight_occupancy_20260528/appendix/
reports/2026-05/metrics/rgp_nsight_occupancy_20260528/scripts/
```

## Commit Plan

1. `profiling labels / run controls`
2. `AMD capture data`
3. `NVIDIA capture data`
4. `analysis report`

